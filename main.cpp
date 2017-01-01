#include "process.h"

#include <getopt.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include <future>
#include <iostream>

static const struct option options[] = {
	{"cpu-port", required_argument, NULL, 'c'},
	{"gpu-port", required_argument, NULL, 'g'},
	{"buffer-size", required_argument, NULL, 'b'},
	{"cpu-threads", required_argument, NULL, 't'},
	{"verbose", no_argument, NULL, 'v'},
	{"help", no_argument, NULL, 'h'},
	{NULL, }
};

void params::open_udp_socket(int &fd, int port)
{
	if (fd > 0) {
		::std::cerr << "Reopening socket for port: " << port << "\n";
		close(fd);
		fd = -1;
	}
	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd == -1) {
		::std::cerr << "Failed to create socket for port: " << port << "\n";
		return;
	}

	struct sockaddr_in address = {
		.sin_family = AF_INET,
		.sin_port = htons(port),
		.sin_addr = {INADDR_ANY},
	};
	if (bind(fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
		close(fd);
		fd = -1;
		perror("Failed to bin socket");
		::std::cerr << "Failed to bind socket for port: " << port
			    << "(" << errno << ")\n";
	}
}

int main(int argc, char *argv[])
{
	params p;
	char c;
	opterr = 0;
	while ((c = getopt_long(argc, argv, "c:g:b:t:vh", options, NULL)) != -1) {
		switch (c) {
		case 'c':
			p.open_cpu_socket(::std::stoi(optarg));
			break;
		case 'g':
			p.open_gpu_socket(::std::stoi(optarg));
			break;
		case 'b':
			p.buffer_size = ::std::stoi(optarg);
			break;
		case 't':
			p.thread_count = ::std::stoi(optarg);
			break;
		case 'v':
			p.verbose = true;
			break;
		default:
			::std::cerr << "Unknown option: " << argv[optind -1]
			            << ::std::endl;
		case 'h':
			::std::cerr << "Available options:\n";
			for (const option &o : options)
				if (o.name)
					::std::cerr << "\t--" << o.name << ", -"
					            << (char)o.val << ::std::endl;
			return 0;
		}
	}
	if (!p.isValid()) {
		::std::cerr << "Invalid configuration: " << p << ::std::endl;
		return 1;
	}
	p.on_switch = 1;
	::std::cout << "Running " << argv[0] << " " << p << ::std::endl;
	auto cpu = ::std::async(::std::launch::async, async_process_cpu, &p);
	auto gpu = ::std::async(::std::launch::async, async_process_gpu, &p);
	::std::cout << "Press any key to exit: ";
	getchar();
	p.on_switch = 0;
	p.close_all();
	cpu.wait();
	gpu.wait();
	return 0;
}
