#pragma once
#include <atomic>
#include <ostream>
#include <thread>

#include <unistd.h>
#include <sys/socket.h>

#ifdef __HCC__
#include <hc.hpp>
#endif


struct params {
	size_t buffer_size = 4096;
	int cpu_socket = -1;
	int gpu_socket = -1;
	bool verbose = false;
	unsigned thread_count = ::std::thread::hardware_concurrency();
	unsigned bucket_size = 128;
	::std::atomic_uint on_switch;

	params():on_switch(0) {};
	~params()
	{ close_all(); }

	bool isValid() const
	{ return (cpu_socket != -1 || gpu_socket != -1) && buffer_size > 0
	         && (bucket_size <= 1024) && (bucket_size > 0)
	         && (bucket_size % 64 == 0); }

	static void open_udp_socket(int &socket, int port);

	void open_gpu_socket(int port)
	{ open_udp_socket(gpu_socket, port); }

	void open_cpu_socket(int port)
	{ open_udp_socket(cpu_socket, port); }

	void close_all() {
		/* shutdown wakes up bloecked recieves */
		if (gpu_socket != -1) shutdown(gpu_socket, SHUT_RDWR);
		gpu_socket = -1;
		if (cpu_socket != -1) shutdown(cpu_socket, SHUT_RDWR);
		cpu_socket = -1;
	}

};
static inline ::std::ostream & operator << (::std::ostream &O, const params &p)
{
	O << "[" << p.gpu_socket << "r, " << p.cpu_socket << "w, "
	  << p.buffer_size << "B, " << p.thread_count << "T, "
	  << p.bucket_size << "WG"  << (p.verbose ? ", v":"") << "]";
	return O;
}

#ifndef __HC__
#  ifdef __HCC__
#    error HCC should define __HC__
#  else
#    define __HC__
#  endif
#endif

int async_process_cpu(const params *p);
int async_process_gpu(const params *p);
