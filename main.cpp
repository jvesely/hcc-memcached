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
	{"hybrid-port", required_argument, NULL, 'y'},
	{"cpu-threads", required_argument, NULL, 't'},
	{"gpu-work-groups", required_argument, NULL, 'w'},
	{"buffer-size", required_argument, NULL, 'b'},
	{"bucket-size", required_argument, NULL, 's'},
	{"bucket-count", required_argument, NULL, 'n'},
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
	while ((c = getopt_long(argc, argv, "c:g:y:b:t:w:s:n:vh", options, NULL)) != -1) {
		switch (c) {
		case 'c':
			p.open_cpu_socket(::std::stoi(optarg));
			break;
		case 'g':
			p.open_gpu_socket(::std::stoi(optarg));
			break;
		case 'y':
			p.open_hybrid_socket(::std::stoi(optarg));
			break;
		case 'b':
			p.buffer_size = ::std::stoi(optarg);
			break;
		case 't':
			p.thread_count = ::std::stoi(optarg);
			break;
		case 'w':
			p.gpu_work_groups = ::std::stoi(optarg);
			break;
		case 's':
			p.bucket_size = ::std::stoi(optarg);
			break;
		case 'n':
			p.bucket_count = ::std::stoi(optarg);
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
	hash_table h(p.bucket_count, p.bucket_size);
	p.on_switch = 1;
	::std::cout << "Running " << argv[0] << " " << p << ::std::endl;
	auto cpu = ::std::async(::std::launch::async, async_process_cpu, &p, &h);
	auto gpu = ::std::async(::std::launch::async, async_process_gpu, &p, &h);
	auto hybrid = ::std::async(::std::launch::async, async_process_hybrid, &p, &h);
	::std::cout << "Press any key to exit...\n";
	getchar();
	p.on_switch = 0;
	p.close_all();
	cpu.wait();
	gpu.wait();
	hybrid.wait();
	return 0;
}
