#include "process.h"

#include <chrono>
#include <climits>
#include <deque>
#include <iostream>
#include <thread>
#include <vector>

#include <sys/socket.h>
#include <netinet/in.h>

struct stats {
	::std::atomic_uint out_packets;
	::std::atomic_uint in_packets;

	stats(): out_packets(0), in_packets(0) {};
};

static void cpu_process(const params *p, struct stats *s)
{
	int sock = socket(AF_INET, SOCK_DGRAM, 0);

	const ::std::atomic_uint *on = &p->on_switch;
	::std::thread::id my_id = ::std::this_thread::get_id();

	struct sockaddr_in address = {0};
	struct sockaddr *addr = (struct sockaddr *)&address;
	const struct sockaddr *addr_out = (const struct sockaddr *)&p->address;
	socklen_t address_len = sizeof(address);

	::std::vector<char> buffer(p->buffer_size);
	size_t in_packets = 0, out_packets = 0;
	while (*on) {
		size_t ret = sendto(sock, buffer.data(), buffer.size(), 0,
		                 addr_out, sizeof(p->address));
		if (ret != buffer.size()) {
			::std::cerr << "Thread " << my_id
		                    << " failed to send a packet. exiting\n";
			break;
		}
		++out_packets;

		address_len = sizeof(address);
		size_t data_len = recvfrom(sock, buffer.data(), ret,
		                           MSG_TRUNC, addr, &address_len);
		if (data_len != buffer.size())
			::std::cerr << "Data for " << my_id << ":"
			            << data_len << "\n";
		else
			++in_packets;
	}
	s->in_packets += in_packets;
	s->out_packets += out_packets;
}

int async_process_cpu(const params *p)
{
	stats s;
	::std::deque<::std::thread> threads;
	auto start = ::std::chrono::high_resolution_clock::now();
	while (threads.size() < p->thread_count)
		threads.push_back(::std::thread(cpu_process, p, &s));
	::std::cout << "Launched " << threads.size() << " CPU threads\n";
	for (auto &t: threads)
		t.join();
	auto end = ::std::chrono::high_resolution_clock::now();
	auto us = ::std::chrono::duration_cast<::std::chrono::microseconds>(end - start);
	size_t in_bits = s.in_packets * p->buffer_size * CHAR_BIT;
	size_t out_bits = s.out_packets * p->buffer_size * CHAR_BIT;
	::std::cout << "Sent " << out_bits << " bits in "
                    << us.count() << " microseconds\n";
	::std::cout << "Out rate: " << (double)out_bits / (double)us.count()
                    << " bits/us (mbit/s)\n";
	::std::cout << "Received " << in_bits << " bits in "
                    << us.count() << " microseconds\n";
	::std::cout << "In rate: " << (double)in_bits / (double)us.count()
                    << " bits/us (mbit/s)\n";
	return 0;
}
