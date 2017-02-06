#include "process.h"

#include <getopt.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <future>
#include <iostream>

static const struct option options[] = {
	{"address", required_argument, NULL, 'a'},
	{"port", required_argument, NULL, 'p'},
	{"buffer-size", required_argument, NULL, 'b'},
	{"cpu-threads", required_argument, NULL, 't'},
	{"verbose", no_argument, NULL, 'v'},
	{"help", no_argument, NULL, 'h'},
	{NULL, }
};

int main(int argc, char *argv[])
{
	params p;
	char c;
	opterr = 0;
	while ((c = getopt_long(argc, argv, "a:p:b:t:s:vh", options, NULL)) != -1) {
		switch (c) {
		case 'a':
			inet_aton(optarg, &p.address.sin_addr);
			break;
		case 'p':
			p.address.sin_port = htons(::std::stoi(optarg));
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
	::std::cout << "Press any key to exit...\n";
	getchar();
	p.on_switch = 0;
	cpu.wait();
	return 0;
}
