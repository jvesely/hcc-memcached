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
	if (p->gpu_socket < 0)
		return -1;
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
			size_t response_size = 0;
			volatile tile_static ssize_t data_len;
			volatile tile_static bool any_found;
			if (idx.local[0] == 0) {
				any_found = false;
				data_len = sc.send(SYS_recvfrom,
				                   {socket, buffer_ptr,
				                    buffer.size(), MSG_TRUNC,
			                            addr,
			                            (uint64_t)&address_len});
				// Make sure we read the most up to date data
				cache_invalidate_l1();
			}
			mc_binary_packet cmd = mc_binary_packet::parse_udp(
				buffer.data(), ::std::min<size_t>(buffer.size(), data_len));

			idx.barrier.wait_with_tile_static_memory_fence();
			if (data_len > buffer.size()) { //truncated
				if (idx.local[0] == 0) {
					response_size = cmd.set_response(
						mc_binary_header::RE_VALUE_TOO_LARGE);
					sc.send(SYS_sendto, {socket,
					                     buffer_ptr,
					                     response_size,
							     0, addr,
					                     address_len});
				}
				continue;
			}
			if (cmd.get_cmd() != mc_binary_header::OP_GET) {
				if (idx.local[0] == 0) {
					response_size = cmd.set_response(
						mc_binary_header::RE_NOT_SUPPORTED);
					sc.send(SYS_sendto, {socket,
					                     buffer_ptr,
					                     response_size,
							     0, addr,
					                     address_len});
				}
				continue;
			}


			string_ref key = cmd.get_key();

			bool found = false;
			auto &bucket = storage->get_bucket(key.data(),
			                                   key.size());
			bucket.read_lock();
			const auto &e =
				bucket.get_element_array()[idx.local[0]];
			if (e.key.size() == key.size() &&
			    0 == ::std::strncmp(e.key.data(), key.data(), key.size())) {
				found = true;
				response_size = cmd.set_response(
					mc_binary_header::RE_OK, e.key, e.data);
			}
			bucket.read_unlock();
			if (found) {
				any_found = true;
				sc.send(SYS_sendto, {socket, buffer_ptr,
				                     response_size,
						     0, addr, address_len});
			}

			idx.barrier.wait_with_tile_static_memory_fence();

			if (idx.local[0] == 0 && !any_found) {
				response_size = cmd.set_response(
					mc_binary_header::RE_NOT_FOUND);
				sc.send(SYS_sendto, {socket, buffer_ptr,
				                     response_size,
						     0, addr, address_len});
			}
		}
	}).wait();
	return 0;
}
