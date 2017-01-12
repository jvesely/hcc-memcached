#pragma once

#include <hc.hpp>
#include <string>
#include <vector>
#include <ostream>

class string_ref
{
	const char *ptr_;
	size_t size_;
public:
	string_ref(const char *ptr, size_t size) : ptr_(ptr), size_(size) {};

	// Copies data
	operator ::std::string() const
	{ return ::std::string(ptr_, size_); }

	// Copies data
	operator ::std::vector<char>() const
	{ return ::std::vector<char>(ptr_, ptr_ + size_); }
};

inline ::std::ostream & operator << (::std::ostream &O, const string_ref &s)
{
	return O << ::std::string(s);
}
