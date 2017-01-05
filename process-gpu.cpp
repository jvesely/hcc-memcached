#include "process.h"

#include "packet-stream.h"

#include <hc.hpp>
#include <hc_syscalls.h>
#include <sys/syscall.h>

#include <sys/socket.h>
#include <netinet/in.h>

int async_process_gpu(const params *p)
{
	// HCC is bad with global variables
	auto &sc = syscalls::get();

	struct sockaddr_in address = {0};
	uint64_t addr = (uint64_t)&address;
	uint64_t socket = p->gpu_socket;
	socklen_t address_len = sizeof(address);

	unsigned groups = (20480 / p->bucket_size);
	using buffer_t = ::std::vector<char>;
	::std::vector<buffer_t> buffers(groups, buffer_t(p->buffer_size));

	auto textent = hc::extent<1>::extent(p->bucket_size).tile(p->bucket_size);
	parallel_for_each(textent, [&](hc::tiled_index<1> idx) [[hc]] {
		buffer_t &buffer = buffers[idx.tile[0]];
		uint64_t buffer_ptr = (uint64_t)buffer.data();

		while (p->on_switch) {
			address_len = sizeof(address);
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
			if (data_len > buffer.size())
				continue;

			if (idx.local[0] == 0) {
				packet << "ERROR\r\n";
				response_size += packet.get_size();
				sc.send(SYS_sendto, {socket, buffer_ptr,
				                     response_size,
				                     0, addr, address_len});
			}
		}
	}).wait();
	return 0;
}
