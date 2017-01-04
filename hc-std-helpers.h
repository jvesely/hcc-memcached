#pragma once

#include <hc.hpp>

// Provide GPU implementations of basic routines
namespace std {
	inline size_t strlen(const char *ch) __HC__
	{
		size_t s = 0;
		while (*ch++ != '\0')
			++s;
		return s;
	}

	inline int strncmp(const char *a, const char *b, size_t n) __HC__
	{
		while (n && *a == *b) {
			n--;
			a++;
			b++;
		}
		return (n == 0) ? 0 : (*a < *b ? -1 : 1);
	}

	inline int atoi(const char *a) __HC__
	{
		while (a[0] == ' ')
			++a;
		int sign = 1;
		if (a[0] == '-') {
			sign = -1;
			++a;
		}
		int val = 0;
		while (a[0] >= '0' && a[0] <= '9')
			val = (val * 10) + (a[0] - '0');
		return val * sign;
	}

	template<typename T>
	inline T * memcpy(T *dst, const T *src, size_t count) __HC__
	{
		char *dst_ch = (char *)dst;
		char *src_ch = (char *)src;
		while (count--)
			*dst_ch++ = *src_ch++;
		return dst;
	}
};

