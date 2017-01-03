#include "process.h"

#include "memcached-protocol.h"

#include <chrono>
#include <deque>
#include <iostream>
#include <thread>
#include <vector>

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

		if (data_len == 0) //spurious return
			continue;
		size_t response_size = 8;
		if (data_len > buffer.size()) { // truncated
			response_size +=
				memcached_command::get_error().generate_packet(
					buffer.data() + 8, buffer.size() - 8);
		} else { // error for now
			memcached_command cmd =
				memcached_command::parse_udp(buffer.data(),
				                             data_len);
			if (cmd.get_cmd() == memcached_command::SET) {
				response_size +=
					memcached_command::get_reply("STORED\r\n").generate_packet(
						buffer.data() + 8, buffer.size() - 8);
			} else {
				::std::cout << cmd << ::std::endl;
				response_size +=
					memcached_command::get_error().generate_packet(
						buffer.data() + 8, buffer.size() - 8);
			}
		}
		sendto(socket, buffer.data(), response_size, 0, addr, address_len);
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
