#include "process.h"

#include "memcached-protocol.h"
#include "packet-stream.h"
#include "hash_table.h"

#include <hc.hpp>
#include <hc_syscalls.h>
#include <sys/syscall.h>

#include <sys/socket.h>
#include <netinet/in.h>

extern "C" void __hsa_vcache_wbinvl1_vol(void)[[hc]];
extern "C" void __hsa_dcache_inv(void)[[hc]];

void cache_invalidate_l1()[[hc]]
{
	__hsa_vcache_wbinvl1_vol();
	__hsa_dcache_inv();
}

int async_process_gpu(const params *p, hash_table *storage)
{
	// HCC is bad with global variables
	auto &sc = syscalls::get();

	uint64_t socket = p->gpu_socket;

	unsigned groups = (20480 / p->bucket_size);
	using buffer_t = ::std::vector<char>;
	::std::vector<buffer_t> buffers(groups, buffer_t(p->buffer_size));
	::std::vector<struct sockaddr_in> addresses(groups);
	socklen_t address_len = sizeof(struct sockaddr_in);

	auto textent = hc::extent<1>::extent(p->bucket_size).tile(p->bucket_size);
	parallel_for_each(textent, [&](hc::tiled_index<1> idx) [[hc]] {
		buffer_t &buffer = buffers[idx.tile[0]];
		uint64_t buffer_ptr = (uint64_t)buffer.data();
		uint64_t addr = (uint64_t)&addresses[idx.tile[0]];

		while (p->on_switch) {
			address_len = sizeof(struct sockaddr_in);
			size_t response_size = 8;
			packet_stream packet(buffer.data() + 8, buffer.size() - 8);
			volatile tile_static ssize_t data_len;
			if (idx.local[0] == 0) {
				data_len = sc.send(SYS_recvfrom,
				                   {socket, buffer_ptr,
				                    buffer.size(), MSG_TRUNC,
			                            addr,
			                            (uint64_t)&address_len});
				if (data_len > buffer.size()) { // truncated
					packet << "CLIENT ERROR 'too big'\r\n";
					response_size += packet.get_size();
					sc.send(SYS_sendto, {socket,
					                     buffer_ptr,
					                     response_size,
							     0, addr,
					                     address_len});
				}
			}

			idx.barrier.wait_with_tile_static_memory_fence();
			if (data_len > buffer.size() || data_len < 0)
				continue;

			// Make sure we read the most up to date data
			cache_invalidate_l1();

			volatile tile_static uint64_t key_begin;
			volatile tile_static uint64_t key_end;
			volatile tile_static bool any_found;
			// This is an ugly workaround
			const char *begin = buffer.data() + 12;
			if (idx.local[0] == 0) {
				any_found = false;
				// work around HCC bugs. end will be nullptr
				// if the command is not "get"
				const char *end =
					memcached_command::parse_get_key_end(
						buffer.data(), data_len);

				key_begin = (uint64_t)begin;
				key_end = (uint64_t)end;
				if (key_begin >= key_end) {
					packet << "ERROR\r\n";
					response_size += packet.get_size();
					sc.send(SYS_sendto, {socket,
					                     buffer_ptr,
					                     response_size,
							     0, addr,
					                     address_len});
				}

			}
			idx.barrier.wait_with_tile_static_memory_fence();
			if (key_begin >= key_end)
				continue;

			size_t key_size = key_end - key_begin;
			bool found = false;
			auto &bucket = storage->get_bucket(begin,
			                                   key_end-key_begin);
			bucket.read_lock();
			const auto &e =
				bucket.get_element_array()[idx.local[0]];
			if (e.key.size() == key_size &&
			    0 == ::std::strncmp(e.key.data(), begin, key_size)) {
				found = true;
				packet << "VALUE " << e.key;
				packet << " 0"; //ignore flags
				packet << " ";
				packet << (uint16_t)e.data.size();
				packet << "\r\n" << e.data;
				packet << "\r\nEND\r\n";
			}
			bucket.read_unlock();
			if (found) {
				any_found = true;
				response_size += packet.get_size();
				// Can this be non-blocking ?
				sc.send(SYS_sendto, {socket, buffer_ptr,
				                     response_size,
						     0, addr, address_len});
			}

			idx.barrier.wait_with_tile_static_memory_fence();

			if (idx.local[0] == 0 && !any_found) {
				packet << "NOT FOUND\r\n";
				response_size += packet.get_size();
				// Can this be non-blocking ?
				sc.send(SYS_sendto, {socket, buffer_ptr,
				                     response_size,
						     0, addr, address_len});
			}
		}
	}).wait();
	return 0;
}
