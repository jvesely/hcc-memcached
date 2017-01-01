#pragma once
#include <atomic>
#include <ostream>

#include <unistd.h>
#include <sys/socket.h>

#ifdef __HCC__
#include <hc.hpp>
#endif


struct params {
	enum {
		BUFFER_SIZE = 4096
	};

	int read_socket = -1;
	int write_socket = -1;
	::std::atomic_uint on_switch;

	params():on_switch(0) {};
	~params()
	{ close_all(); }

	bool isValid() const
	{ return read_socket != -1 && write_socket != -1; }

	static void open_udp_socket(int &socket, int port);

	void open_read_socket(int port)
	{ open_udp_socket(read_socket, port); }

	void open_write_socket(int port)
	{ open_udp_socket(write_socket, port); }

	void close_all() {
		/* shutdown wakes up bloecked recieves */
		if (read_socket != -1) shutdown(read_socket, SHUT_RDWR);
		read_socket = -1;
		if (write_socket != -1) shutdown(write_socket, SHUT_RDWR);
		write_socket = -1;
	}

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

int async_process_cpu(int socket, ::std::atomic_uint *on_switch);
int async_process_gpu(int socket, ::std::atomic_uint *on_switch);
