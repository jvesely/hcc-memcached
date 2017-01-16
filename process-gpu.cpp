#include "process.h"

#include "memcached-protocol.h"
#include "packet-stream.h"
#include "hash_table.h"

#include <iostream>
#include <numeric>

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

void print_error(syscalls &sc, const ::std::string &m) __HC__
{
	sc.send(SYS_write, {2, (uint64_t)m.c_str(), m.size()});
}

int async_process_gpu(const params *p, hash_table *storage)
{
	if (p->gpu_socket < 0)
		return -1;
	// HCC is bad with global variables
	auto &sc = syscalls::get();

	uint64_t socket = p->gpu_socket;

	unsigned groups = p->gpu_work_groups == -1 ? (20480 / p->bucket_size)
	                                           : p->gpu_work_groups;
	using buffer_t = ::std::vector<char>;
	::std::vector<struct sockaddr_in> addresses(groups);
	::std::vector<buffer_t> buffers(groups, buffer_t(p->buffer_size));
	::std::vector<socklen_t> address_len(groups, sizeof(struct sockaddr_in));

	::std::vector<size_t> packets(groups, 0);
	::std::string e_no_buffer("Buffer pointer is zero\n");
	::std::string e_truncated("Request packet truncated\n");
	::std::string e_notget("Unsupported operation\n");
	::std::string e_corrupted("Reuqest packet corrupt\n");

	auto textent = hc::extent<1>::extent(groups * p->bucket_size).tile(p->bucket_size);
	parallel_for_each(textent, [&](hc::tiled_index<1> idx) [[hc]] {
		buffer_t &buffer = buffers[idx.tile[0]];
		uint64_t buffer_ptr = (uint64_t)buffer.data();
		uint64_t addr = (uint64_t)&addresses[idx.tile[0]];
		if (buffer.data() == nullptr ||
		    buffer.size() != p->buffer_size) {
			print_error(sc, e_no_buffer);
			return;
		}

		volatile tile_static ssize_t data_len;
		volatile tile_static uint64_t element;
		// TODO: this should be ::std::atomic_uint
		// TODO implement atomic load/store in LDS
		tile_static unsigned hash;

		size_t tlid = idx.tile[0];
		bool is_lead = (idx.local[0] == 0);

		while (p->on_switch) {
			address_len[tlid] = sizeof(struct sockaddr_in);
			size_t response_size = 0;
			if (is_lead) {
				hc::atomic_exchange(&hash, 0);
				element = 0;
				data_len = sc.send(SYS_recvfrom,
				                   {socket, buffer_ptr,
				                    buffer.size(), MSG_TRUNC,
			                            addr,
			                            (uint64_t)&address_len[tlid]});
				// Make sure we read the most up to date data
				cache_invalidate_l1();
				if (data_len > 0)
					++packets[tlid];
			}
			mc_binary_packet cmd = mc_binary_packet::parse_udp(
				buffer.data(), ::std::min<size_t>(buffer.size(), data_len));

			idx.barrier.wait_with_tile_static_memory_fence();
			if (data_len <= 0) //error receiving data
				continue;

			if (data_len > buffer.size()) { //truncated
				if (is_lead) {
					print_error(sc, e_truncated);
					response_size = cmd.set_response(
						mc_binary_header::RE_VALUE_TOO_LARGE);
					sc.send(SYS_sendto, {socket,
					                     buffer_ptr,
					                     response_size,
							     0, addr,
					                     address_len[tlid]});
				}
				continue;
			}
			if (cmd.get_cmd() != mc_binary_header::OP_GET) {
				if (is_lead) {
					print_error(sc, e_notget);
					response_size = cmd.set_response(
						mc_binary_header::RE_NOT_SUPPORTED);
					sc.send(SYS_sendto, {socket,
					                     buffer_ptr,
					                     response_size,
							     0, addr,
					                     address_len[tlid]});
				}
				continue;
			}


			char * buffer_end = buffer.data() + buffer.size();
			string_ref key = cmd.get_key();
			if (key.data() > buffer_end ||
			    (key.data() + key.size()) > buffer_end) {
				if (is_lead) {
					print_error(sc, e_corrupted);
					response_size = cmd.set_response(
						mc_binary_header::RE_INTERNAL_ERROR);
					sc.send(SYS_sendto, {socket,
					                     buffer_ptr,
					                     response_size,
							     0, addr,
					                     address_len[tlid]});
				}
				continue;
			}
			int id = idx.local[0];
			// Keep this in sync with the HT implementation
			for (unsigned offset = 0; (offset + id) < key.size();
			     offset += idx.tile_dim[0]) {
				unsigned i = id + offset;
				uint32_t lval = key.data()[i] << ((i % 2) * 8);
				hc::atomic_fetch_xor(&hash, lval);
			}
			idx.barrier.wait_with_tile_static_memory_fence();

			bool found = false;
			auto &bucket = storage->get_bucket(hash);
			if (is_lead)
				bucket.read_lock();
			idx.barrier.wait();
			const auto &e =
				bucket.get_element_array()[idx.local[0]];
			if (e.key.size() == key.size() &&
			    0 == ::std::strncmp(e.key.data(), key.data(), key.size())) {
				element = (uint64_t)&e;
				found = true;
				response_size = cmd.set_response(
					mc_binary_header::RE_OK,
					e.key.size(), e.data.size());
			}
			idx.barrier.wait_with_tile_static_memory_fence();

			bucket::element *el = (bucket::element*)element;
			// copy the key
			for (unsigned offset = 0;
			     el && (offset + id) < el->key.size();
			     offset += idx.tile_dim[0]) {
				unsigned i = id + offset;
				buffer.data()[32 + i] = el->key.data()[i];
			}

			// copy the data
			for (unsigned offset = 0;
			     el && (offset + id) < el->data.size();
			     offset += idx.tile_dim[0]) {
				unsigned i = id + offset;
				buffer.data()[32 + el->key.size() + i]
					= el->data.data()[i];
			}

			idx.barrier.wait();
			if (is_lead)
				bucket.read_unlock();
			if (found) {
				sc.send(SYS_sendto, {socket, buffer_ptr,
				                     response_size,
						     0, addr, address_len[tlid]});
			}

			idx.barrier.wait_with_tile_static_memory_fence();

			if (element == 0) {
				// This barrier should prevent hcc from
				// miscompiling and corrupting the condition
				// vector
				idx.barrier.wait();
				if (is_lead) {
					response_size = cmd.set_response(
						mc_binary_header::RE_NOT_FOUND);
					sc.send(SYS_sendto, {socket, buffer_ptr,
					                     response_size,
							     0, addr,
					                     address_len[tlid]});
				}
			}
		}
	}).wait();
	for (size_t i = 0; i < packets.size(); ++i)
		::std::cout << "GPU group " << i << " processed "
		            << packets[i] << " packets.\n";
	::std::cout << "All groups processed "
	            << ::std::accumulate(packets.begin(), packets.end(), 0)
	            << " packets\n";
	return 0;
}
