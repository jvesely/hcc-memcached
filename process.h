#pragma once
#include <atomic>
#include <ostream>
#include <thread>

#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>

struct params {
	size_t buffer_size = 4096;
	bool verbose = false;
	unsigned thread_count = ::std::thread::hardware_concurrency();
	::std::atomic_uint on_switch;

	struct sockaddr_in address = {
		.sin_family = AF_INET,
		.sin_port = 0,
		.sin_addr = {INADDR_ANY},
	};

	params():on_switch(0) {};

	bool isValid() const
	{ return address.sin_port != 0 && buffer_size > 0; }
};

static inline ::std::ostream & operator << (::std::ostream &O, const params &p)
{
	O << "[" << ntohs(p.address.sin_port) << ", "
	  << p.buffer_size << "B, "
	  << p.thread_count << "T"
	  << (p.verbose ? ", v":"") << "]";
	return O;
}

int async_process_cpu(const params *p);
