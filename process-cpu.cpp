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

static void cpu_process(const params *p, hash_table *storage, ::std::atomic_uint *count)
{
	int socket = p->cpu_socket;
	const ::std::atomic_uint *on = &p->on_switch;
	struct sockaddr_in address = {0};
	struct sockaddr *addr = (struct sockaddr *)&address;
	socklen_t address_len = sizeof(address);
	::std::vector<char> buffer(p->buffer_size);
	unsigned packet_count = 0;
	while (*on) {
		address_len = sizeof(address);
		size_t data_len = recvfrom(socket, buffer.data(), buffer.size(),
		                           MSG_TRUNC, addr, &address_len);

		if (data_len == 0) //spurious return
			continue;
		++packet_count;

		size_t response_size = 0;

		mc_binary_packet cmd = mc_binary_packet::parse_udp(
			buffer.data(), ::std::min(data_len, buffer.size()));
		if (p->verbose)
			::std::cerr << cmd << "\n";
		if (data_len > buffer.size()) { // truncated
			response_size = cmd.set_response(
				mc_binary_header::RE_VALUE_TOO_LARGE);
			sendto(socket, buffer.data(), response_size, 0, addr, address_len);
			continue;
		}

		if (cmd.get_cmd() == mc_binary_header::OP_SET) {
			storage->insert(cmd.get_key(), cmd.get_value());
			if (p->verbose)
				::std::cerr << "STORED " << cmd.get_key()
				            << " (" << cmd.get_key().size()
					    << ") : " << cmd.get_value()
				            << " (" << cmd.get_value().size()
				            << ")\n";
			response_size = cmd.set_response(mc_binary_header::RE_OK);
		} else
		if (cmd.get_cmd() == mc_binary_header::OP_GET) {
			::std::string key = cmd.get_key();
			if (p->verbose)
				::std::cerr << "Looking for: " << key << " ("
				            << key.size() << ")\n";
			auto &bucket = storage->get_bucket(key);
			bucket.read_lock();
			bool found = false;
			for (const auto &e : bucket.get_element_array())
				if (e.key == key) {
					found = true;
					response_size = cmd.set_response(
						mc_binary_header::RE_OK, e.key,
						e.data);
					break;
				}
			bucket.read_unlock();
			if (!found)
				response_size = cmd.set_response(
					mc_binary_header::RE_NOT_FOUND);
		} else {
			::std::cerr << "Unknown command: " << cmd << ::std::endl;
			response_size = cmd.set_response(
				mc_binary_header::RE_NOT_SUPPORTED);
		}
		if (p->verbose)
			::std::cerr <<cmd << "\n";
		sendto(socket, buffer.data(), response_size, 0, addr, address_len);
	}
	(*count) += packet_count;
}

int async_process_cpu(const params *p, hash_table *storage)
{
	::std::atomic_uint count(0);
	::std::deque<::std::thread> threads;
	while (threads.size() < p->thread_count)
		threads.push_back(::std::thread(cpu_process, p, storage, &count));
	::std::cout << "Launched " << threads.size() << " CPU threads\n";
	for (auto &t: threads)
		t.join();
	::std::cout << "CPU threads processed " << count << " packets\n";
	return 0;
}
