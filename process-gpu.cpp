#include "process.h"

#include <hc.hpp>
#include <hc_syscalls.h>
#include <sys/syscall.h>

#include <sys/socket.h>
#include <netinet/in.h>

int async_process_gpu(const params *p)
{
	// HCC is bad with global variables
	auto &sc = syscalls::get();
	::std::vector<char> buffer(p->buffer_size);
	struct sockaddr_in address = {0};
	uint64_t addr = (uint64_t)&address;
	uint64_t socket = p->gpu_socket;
	socklen_t address_len = sizeof(address);
	unsigned groups = (20480 / p->bucket_size);

	auto textent = hc::extent<1>::extent(groups * p->bucket_size).tile(p->bucket_size);
	parallel_for_each(textent, [&](hc::tiled_index<1> idx) [[hc]] {
		while (p->on_switch) {
			address_len = sizeof(address);
			if (idx.local[0] == 0) {
			ssize_t data_len = sc.send(SYS_recvfrom,
			                           {(uint64_t)socket,
			                            (uint64_t)buffer.data(),
			                            buffer.size(), MSG_TRUNC,
			                            addr,
			                            (uint64_t)&address_len});
			if (data_len > 0)
				sc.send(SYS_sendto, {(uint64_t)socket,
				                     (uint64_t)buffer.data(),
				                     ::std::min<size_t>(data_len,
				                               buffer.size()),
						     0, addr, address_len});
			}
			idx.barrier.wait();
		}
	}).wait();
	return 0;
}
