#include "process.h"

#include "memcached-protocol.h"
#include "packet-stream.h"

#include <chrono>
#include <deque>
#include <iostream>
#include <thread>
#include <vector>
#include <unordered_map>
#include <shared_mutex>

#include <sys/socket.h>
#include <netinet/in.h>

// temporary
static ::std::unordered_map<::std::string, ::std::vector<char>> storage_;
static ::std::mutex storage_lock_;

static void cpu_process(const params *p)
{
	int socket = p->cpu_socket;
	const ::std::atomic_uint *on = &p->on_switch;
	struct sockaddr_in address = {0};
	struct sockaddr *addr = (struct sockaddr *)&address;
	socklen_t address_len = sizeof(address);
	::std::vector<char> buffer(p->buffer_size);
	while (*on) {
		address_len = sizeof(address);
		size_t data_len = recvfrom(socket, buffer.data(), buffer.size(),
		                           MSG_TRUNC, addr, &address_len);

		if (data_len == 0) //spurious return
			continue;

		size_t response_size = 8;
		packet_stream packet(buffer.data() + 8, buffer.size() - 8);

		if (data_len > buffer.size()) { // truncated
			packet << "CLIENT ERROR 'too big'\r\n";
			response_size += packet.get_size();
			sendto(socket, buffer.data(), response_size, 0, addr, address_len);
			continue;
		}

		memcached_command cmd =
			memcached_command::parse_udp(buffer.data(), data_len);
		if (cmd.get_cmd() == memcached_command::SET) {
			::std::lock_guard<::std::mutex> l(storage_lock_);
			storage_[cmd.get_key()] = cmd.get_data();
			packet << "STORED\r\n";
		} else
		if (cmd.get_cmd() == memcached_command::GET) {
			::std::lock_guard<::std::mutex> l(storage_lock_);
			auto it = storage_.find(cmd.get_key());
			if (it == storage_.end()) {
				packet << "NOT_FOUND\r\n";
			} else {
				packet << "VALUE " << it->first;
				packet << " 0"; //ignore flags
				packet << " ";
				packet << (uint16_t)it->second.size();
				packet << "\r\n" << it->second;
				packet << "\r\nEND\r\n";
			}
		} else {
			::std::cout << cmd << ::std::endl;
			packet << "ERROR'\r\n";
		}
		response_size += packet.get_size();
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
