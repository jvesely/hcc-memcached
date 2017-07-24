#include "process.h"

#include "memcached-protocol.h"
#include "packet-stream.h"
#include "rwlock.h"
#include "hash_table.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <iostream>
#include <thread>
#include <vector>

#include <sys/socket.h>
#include <netinet/in.h>

struct request_t {
	request_t(size_t buffer_size): buffer(buffer_size) {};
	volatile bool done = false;
	struct sockaddr address = {};
	socklen_t address_len = sizeof(address);
	::std::vector<char> buffer;
	size_t response_size = 0;
	size_t request_size = 0;
};

struct hybrid_params_t {
	const params *p;
	::std::deque<request_t> requests;
	::std::condition_variable cond_not_empty;
	::std::mutex que_lock;
};

static void hybrid_listen(hybrid_params_t *hp, hash_table *storage, ::std::atomic_uint *count)
{
	const params *p = hp->p;
	int socket = p->hybrid_socket;
	const ::std::atomic_uint *on = &p->on_switch;
	unsigned packet_count = 0;

	while (*on) {
		request_t request(p->buffer_size);
		struct sockaddr *addr = (struct sockaddr *)&request.address;
		socklen_t *address_len = &request.address_len;
		size_t response_size = 0;

		size_t data_len = recvfrom(socket, request.buffer.data(),
		                           request.buffer.size(),
		                           MSG_TRUNC, addr, address_len);

		if (data_len == 0) //spurious return
			continue;
		++packet_count;
		mc_binary_packet cmd = mc_binary_packet::parse_udp(
			request.buffer.data(),
			::std::min(data_len, request.buffer.size()));
		if (p->verbose)
			::std::cerr << cmd << "\n";
		if (data_len > request.buffer.size()) { // truncated
			response_size = cmd.set_response(
				mc_binary_header::RE_VALUE_TOO_LARGE);
			sendto(socket, request.buffer.data(), response_size, 0, addr, *address_len);
			continue;
		}

		switch(cmd.get_cmd())
		{
		case mc_binary_header::OP_SET:
			storage->insert(cmd.get_key(), cmd.get_value());
			if (p->verbose)
				::std::cerr << "STORED " << cmd.get_key()
				            << " (" << cmd.get_key().size()
					    << ") : " << cmd.get_value()
				            << " (" << cmd.get_value().size()
				            << ")\n";
			response_size = cmd.set_response(mc_binary_header::RE_OK);
			break;
		case mc_binary_header::OP_GET:
			request.request_size = data_len;
			hp->que_lock.lock();
			hp->requests.push_back(request);
			hp->cond_not_empty.notify_one();
			hp->que_lock.unlock();
			if (p->verbose)
				::std::cerr << "Enqueued GET request: " << cmd << "\n";
			continue;
		default:
			::std::cerr << "Unknown command: " << cmd << ::std::endl;
			response_size = cmd.set_response(
				mc_binary_header::RE_NOT_SUPPORTED);
		}
		if (p->verbose)
			::std::cerr << "Responding: " << cmd << "\n";
		sendto(socket, request.buffer.data(), response_size, 0, addr, *address_len);
	}
	(*count) += packet_count;
	if (p->verbose)
		::std::cerr << "Exiting listener\n";
	// Notify processor to avoid hang
	hp->cond_not_empty.notify_all();
}

static void hybrid_process(hybrid_params_t *hp, hash_table *storage, ::std::atomic_uint *count)
{
	const params *p = hp->p;
	const ::std::atomic_uint *on = &p->on_switch;

	for (::std::unique_lock<::std::mutex> lock(hp->que_lock);
	     *on || !hp->requests.empty();) {

		::std::deque<request_t> local_q;

		if (hp->requests.size() == 0) {
			hp->cond_not_empty.wait(lock);
			continue;
		}
		local_q.swap(hp->requests);

		// unlock for GPU execution
		lock.unlock();

		// Read this once
		size_t requests = local_q.size();
		auto it = local_q.begin();

		if (p->verbose)
			::std::cerr << "Processing " << requests << " requests\n";
		auto textent =
			hc::extent<1>(requests * p->bucket_size).tile(p->bucket_size);
		parallel_for_each(textent, [&](hc::tiled_index<1> idx) [[hc]] {
			size_t request_id = idx.tile[0];
			int id = idx.local[0];
			bool is_lead = (idx.local[0] == 0);
			request_t &request = *(it + request_id);

			// Found element pointer
			volatile tile_static uint64_t element;
			tile_static unsigned hash;
			if (is_lead) {
				hc::atomic_exchange(&hash, 0);
				element = 0;
			}
			idx.barrier.wait_with_tile_static_memory_fence();

			mc_binary_packet cmd =
				mc_binary_packet::parse_udp(
					request.buffer.data(),
					::std::min(request.request_size,
						   request.buffer.size()));
			char * buffer_end =
				request.buffer.data() + request.buffer.size();
			string_ref key = cmd.get_key();
			if (key.data() > buffer_end ||
			    (key.data() + key.size()) > buffer_end) {
				if (is_lead) {
					request.response_size = cmd.set_response(
						mc_binary_header::RE_INTERNAL_ERROR);
					request.done = true;
				}
				return;
			}

			// Hash the key
			// Keep this in sync with the HT implementation
			for (unsigned offset = 0; (offset + id) < key.size();
			     offset += idx.tile_dim[0]) {
				unsigned i = id + offset;
				uint32_t lval = key.data()[i] << ((i % 2) * 8);
				hc::atomic_fetch_xor(&hash, lval);
			}
			idx.barrier.wait_with_tile_static_memory_fence();

			auto &bucket = storage->get_bucket(hash);
			if (is_lead)
				bucket.read_lock();
			idx.barrier.wait();


			// Get elment in array
			const auto &e =
				bucket.get_element_array()[idx.local[0]];
			// Check element
			if (e.key.size() == key.size() &&
			    0 == ::std::strncmp(e.key.data(), key.data(), key.size())) {
				element = (uint64_t)&e;
				request.response_size = cmd.set_response(
					mc_binary_header::RE_OK,
					e.key.size(), e.data.size());
			}
			idx.barrier.wait_with_tile_static_memory_fence();

			bucket::element *el = (bucket::element*)element;

			// Copy the key if element found
			for (unsigned offset = 0;
			     el && (offset + id) < el->key.size();
			     offset += idx.tile_dim[0]) {
				unsigned i = id + offset;
				request.buffer.data()[32 + i] = el->key.data()[i];
			}

			// Copy the data if element found
			for (unsigned offset = 0;
			     el && (offset + id) < el->data.size();
			     offset += idx.tile_dim[0]) {
				unsigned i = id + offset;
				request.buffer.data()[32 + el->key.size() + i]
					= el->data.data()[i];
			}

			idx.barrier.wait();
			if (is_lead)
				bucket.read_unlock();

			idx.barrier.wait_with_tile_static_memory_fence();

			if (element == 0) {
				// This barrier should prevent hcc from
				// miscompiling and corrupting the condition
				// vector
				idx.barrier.wait();
				if (is_lead) {
					request.response_size = cmd.set_response(
						mc_binary_header::RE_NOT_FOUND);
				}
			}
			idx.barrier.wait_with_global_memory_fence();
			request.done = true;
		}).wait();
		// lock again for request update
		if (p->verbose)
			::std::cerr << "GPU processed " << requests << " requests\n";
		::std::async(::std::launch::async, [=](){
			for (const request_t &r: local_q) {
				sendto(p->hybrid_socket, r.buffer.data(), r.response_size, 0,
				        &r.address, r.address_len);
			}
		});
		// Lock for condition evaluation and next iteration
		lock.lock();
	}
	if (p->verbose)
		::std::cerr << "Exiting processor\n";
}

int async_process_hybrid(const params *p, hash_table *storage)
{
	if (p->hybrid_socket < 0)
		return -1;
	hybrid_params_t hp;
	hp.p = p;

	::std::atomic_uint count(0);
	::std::deque<::std::thread> threads;
	while (threads.size() < p->thread_count)
		threads.push_back(::std::thread([&](){hybrid_process(&hp, storage, &count);}));
	while (threads.size() < p->thread_count * 2)
		threads.push_back(::std::thread([&](){hybrid_listen(&hp, storage, &count);}));
	::std::cout << "Launched " << threads.size() << " hybrid CPU threads\n";
	for (auto &t: threads)
		t.join();
	::std::cout << "Hybrid CPU threads processed " << count << " packets\n";
	return 0;
}
