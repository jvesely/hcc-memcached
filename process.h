#pragma once
#include <atomic>
#include <ostream>

#include <unistd.h>

#ifdef __HCC__
#include <hc.hpp>
#endif


struct params {
	int read_socket = -1;
	int write_socket = -1;
	::std::atomic_uint on_switch;

	params():on_switch(0) {};
	~params()
	{ if (read_socket != -1) close(read_socket);
	  if (write_socket != -1) close(write_socket);
	}

	bool isValid() const
	{ return read_socket != -1 && write_socket != -1; }

	static void open_udp_socket(int &socket, int port);

	void open_read_socket(int port)
	{ open_udp_socket(read_socket, port); }

	void open_write_socket(int port)
	{ open_udp_socket(write_socket, port); }

};
static inline ::std::ostream & operator << (::std::ostream &O, const params &p)
{
	O << "[" << p.read_socket << "r, " << p.write_socket << "w]";
	return O;
}

#ifndef __HC__
#  ifdef __HCC__
#    error HCC should define __HC__
#  else
#    define __HC__
#  endif
#endif

int async_process_cpu(int socket, ::std::atomic_uint &on_switch);
int async_process_gpu(int socket, ::std::atomic_uint &on_switch);
