#pragma once

#include "rwlock.h"

#include <vector>

class bucket {
public:
	struct element {
		::std::string key;
		::std::vector<char> data;
	};

private:
	rwlock lock_;
	size_t next_insert_ = 0;
	::std::vector<element> elements_;
public:
	bucket() = delete;
	bucket(const bucket&) = delete;
	bucket(bucket&& other_) : lock_()
	{
		elements_.swap(other_.elements_);
		next_insert_ = other_.next_insert_;
	}
	bucket(size_t element_count) : elements_(element_count) {};

	void insert(const ::std::string &&key,
	            const ::std::vector<char> &&data)
	{
		lock_.write_lock();
		assert(next_insert_ < elements_.size());
		auto &ref = elements_[next_insert_++];
		ref.key = ::std::move(key);
		ref.data = ::std::move(data);
		next_insert_ %= elements_.size();
		lock_.write_unlock();
	}

	const ::std::vector<element> &get_element_array() const __HC__ __CPU__
	{ return elements_; }
	void read_lock() __HC__ __CPU__
	{ lock_.read_lock(); }
	void read_unlock() __HC__ __CPU__
	{ lock_.read_unlock(); }
};

class hash_table {
	::std::vector<bucket> buckets_;
	unsigned hash(const char *ptr, size_t size) __HC__ __CPU__
	{
		uint16_t hash = 0;
		// This might skip the last byte for odd sized keys,
		// but we don't really need to care about that.
		for (size_t i = 0; i < size; i += 2)
		{
			hash ^= ((uint16_t)(ptr[i]) | (uint16_t)(ptr[i+1] << 8));
		}
		return hash;
	}
public:
	hash_table(size_t buckets, size_t elements)
	{
		buckets_.reserve(buckets);
		while (buckets--)
			buckets_.emplace_back(bucket(elements));
	}

	bucket &get_bucket(const char *key_begin, size_t key_size) __HC__ __CPU__
	{ return buckets_[hash(key_begin, key_size) % buckets_.size()];	}

	bucket &get_bucket(const ::std::string &key)
	{ return get_bucket(key.data(), key.size()); }

	void insert(const ::std::string &&key, const ::std::vector<char> &&data)
	{
		auto &b = get_bucket(key.c_str(), key.size());
		b.insert(::std::move(key), ::std::move(data));
	}
};
