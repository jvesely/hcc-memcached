#include "process.h"

#include <chrono>
#include <iostream>
#include <deque>
#include <thread>

#include <hc.hpp>
#include <hc_syscalls.h>
#include <sys/syscall.h>

static void cpu_process(int socket, ::std::atomic_uint *on)
{
	while(*on) {
		::std::cout << "Hello from " << ::std::this_thread::get_id() << "\n";
		::std::this_thread::sleep_for(::std::chrono::seconds(1));
	}
}

int async_process_cpu(int socket, ::std::atomic_uint *on_switch)
{
	unsigned thread_count = ::std::thread::hardware_concurrency();
	::std::cout << "Launching " << thread_count << " CPU threads\n";

	::std::deque<::std::thread> threads;
	while (thread_count--)
		threads.push_back(::std::thread(cpu_process, socket, on_switch));
	for (auto &t: threads)
		t.join();
	return 0;
}
int async_process_gpu(int socket, ::std::atomic_uint *on_switch)
{
	// HCC is bad with global variables
	auto &sc = syscalls::get();
	::std::string hello("Hello from the GPU\n");

	auto textent = hc::extent<1>::extent(1);
	parallel_for_each(textent, [&](hc::index<1> idx) [[hc]] {
		while (*on_switch) {
			sc.send(SYS_write, {(uint64_t)1,
			                    (uint64_t)hello.c_str(),
			                    hello.size()});
			break;
		}
	}).wait();
	return 0;
}
