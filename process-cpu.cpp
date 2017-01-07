#include "process.h"

#include "memcached-protocol.h"
#include "packet-stream.h"
#include "rwlock.h"
#include "hash_table.h"

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

static void cpu_process(const params *p, hash_table *storage)
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
			storage->insert(cmd.get_key(), cmd.get_data());
			packet << "STORED\r\n";
		} else
		if (cmd.get_cmd() == memcached_command::GET) {
			::std::string key = cmd.get_key();
			auto &bucket = storage->get_bucket(key);
			bucket.read_lock();
			bool found = false;
			for (const auto &e : bucket.get_element_array())
				if (e.key == key) {
					found = true;
					packet << "VALUE " << key;
					packet << " 0"; //ignore flags
					packet << " ";
					packet << (uint16_t)e.data.size();
					packet << "\r\n" << e.data;
					packet << "\r\nEND\r\n";
					break;
				}
			bucket.read_unlock();
			if (!found)
				packet << "NOT_FOUND\r\n";
		} else {
			::std::cout << cmd << ::std::endl;
			packet << "ERROR\r\n";
		}
		response_size += packet.get_size();
		sendto(socket, buffer.data(), response_size, 0, addr, address_len);
	}
}

int async_process_cpu(const params *p, hash_table *storage)
{
	::std::deque<::std::thread> threads;
	while (threads.size() < p->thread_count)
		threads.push_back(::std::thread(cpu_process, p, storage));
	::std::cout << "Launched " << threads.size() << " CPU threads\n";
	for (auto &t: threads)
		t.join();
	return 0;
}
