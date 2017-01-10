#include "process.h"

#include <hc.hpp>
#include <hc_syscalls.h>
#include <sys/syscall.h>

#include <sys/socket.h>
#include <netinet/in.h>

int async_process_gpu(const params *p)
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
		uint64_t addr = (uint64_t)&addresses[idx.tile[0]];

		while (p->on_switch) {
			address_len = sizeof(struct sockaddr_in);
			volatile tile_static ssize_t data_len;
			if (idx.local[0] == 0) {
				data_len = sc.send(SYS_recvfrom,
				                   {(uint64_t)socket,
				                    (uint64_t)buffer.data(),
				                     buffer.size(), MSG_TRUNC,
			                             addr,
			                            (uint64_t)&address_len});
			}
			idx.barrier.wait_with_tile_static_memory_fence();

			size_t response_size =
				::std::min<size_t>(data_len, buffer.size());
			if (idx.local[0] == 0) {
				sc.send(SYS_sendto, {(uint64_t)socket,
			                             (uint64_t)buffer.data(),
			                             response_size,
					             0, addr, address_len});
			}
		}
	}).wait();
	return 0;
}
