#pragma once

#include <string>
#include <hc.hpp>

#include "hc-std-helpers.h"

class packet_stream
{
	char *data_;
	char *end_;
	char *pointer_;
	bool failed_ = false;

public:
	packet_stream(char *data, size_t size) __HC__ __CPU__
		: data_(data), end_(data + size), pointer_(data) {};

	packet_stream & append(const char *data, size_t size) __HC__ __CPU__
	{
		if (pointer_ + size > end_ || failed_) {
			failed_ = true;
		} else {
			::std::memcpy(pointer_, data, size);
			pointer_ += size;
		}
		return *this;
	}

	template<typename T>
	packet_stream & operator << (const T &c) __HC__ __CPU__
	{
		return append((char*)c.data(),
		              c.size() * sizeof(typename T::value_type));
	}

	packet_stream & operator << (uint16_t n) __HC__ __CPU__
	{
		char c[5] = {0};
		for (int i = 0; i < 5; ++i) {
			c[4-i] = (n % 10) + '0';
			n /= 10;
		}
		const char *p;
		for (p = c; (*p == '0') && (p < (c + 5)); ++p);
		return append(p, (c+5) - p);
	}

	packet_stream & operator << (const char* ptr) __HC__ __CPU__
	{ return append(ptr, ::std::strlen(ptr)); }

	size_t get_size() const __HC__ __CPU__
	{ return pointer_ - data_; }

	size_t get_free_space() const __HC__ __CPU__
	{ return end_ - pointer_; }

	bool is_ok() const __HC__ __CPU__
	{ return !failed_; }
};
