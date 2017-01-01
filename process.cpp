#include "process.h"

#include <chrono>
#include <deque>
#include <iostream>
#include <thread>
#include <vector>

#include <hc.hpp>
#include <hc_syscalls.h>
#include <sys/syscall.h>

#include <sys/socket.h>
#include <netinet/in.h>

static void cpu_process(const params *p)
{
	int socket = p->cpu_socket;
	const ::std::atomic_uint *on = &p->on_switch;
	::std::thread::id my_id = ::std::this_thread::get_id();
	struct sockaddr_in address = {0};
	struct sockaddr *addr = (struct sockaddr *)&address;
	socklen_t address_len = sizeof(address);
	::std::vector<char> buffer(p->buffer_size);
	while (*on) {
		address_len = sizeof(address);
		size_t data_len = recvfrom(socket, buffer.data(), buffer.size(),
		                           MSG_TRUNC, addr, &address_len);
		if (p->verbose)
			::std::cout << "Data for " << my_id << ":"
			            << data_len << "\n";
		if (data_len > 0)
			sendto(socket, buffer.data(), ::std::min(data_len,
			                                         buffer.size()),
			       0, addr, address_len);
	}
}

int async_process_cpu(const params *p)
{
	::std::deque<::std::thread> threads;
	while (threads.size() < p->thread_count)
		threads.push_back(::std::thread(cpu_process, p));
	::std::cout << "Launched " << threads.size() << " CPU threads\n";
	for (auto &t: threads)
		t.join();
	return 0;
}
int async_process_gpu(const params *p)
{
	// HCC is bad with global variables
	auto &sc = syscalls::get();
	::std::vector<char> buffer(p->buffer_size);
	struct sockaddr_in address = {0};
	uint64_t addr = (uint64_t)&address;
	uint64_t socket = p->gpu_socket;
	socklen_t address_len = sizeof(address);

	auto textent = hc::extent<1>::extent(1);
	parallel_for_each(textent, [&](hc::index<1> idx) [[hc]] {
		while (p->on_switch) {
			address_len = sizeof(address);
			ssize_t data_len = sc.send(SYS_recvfrom,
			                           {(uint64_t)socket,
			                            (uint64_t)buffer.data(),
			                            buffer.size(), MSG_TRUNC,
			                            addr,
			                            (uint64_t)&address_len});
			if (data_len > 0)
				sc.send(SYS_write, {((uint64_t)1),
				                    (uint64_t)buffer.data(),
				                    ::std::min<size_t>(data_len,
				                               buffer.size())});
		}
	}).wait();
	return 0;
}
