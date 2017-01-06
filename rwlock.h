#pragma once

#include <hc.hpp>
#include <atomic>

class rwlock {
	enum {
		WRITE_LOCK = 0x80000000
	};
	::std::atomic_uint lock_;
public:
	rwlock() __HC__ __CPU__: lock_(0) {};
	~rwlock() __HC__ __CPU__ {};

	rwlock(rwlock &) = delete;

	void write_lock() __HC__ __CPU__
	{
		// Wait for other writers that might hold the lock
		while ((lock_.fetch_or(WRITE_LOCK) & WRITE_LOCK) == WRITE_LOCK);
		// Wait for active readers
		while (lock_.load() > WRITE_LOCK);
	}

	void write_unlock() __HC__ __CPU__
	{
		lock_ &= ~WRITE_LOCK;
	}

	void read_lock()
	{
		unsigned val = 0;
		do {
			// Wait for any writer to leave
			while (lock_.load() > WRITE_LOCK);
			// Try to add reader reference
			val = lock_.fetch_add(1);
			// We raced with a writer, give up claim
			if (val > WRITE_LOCK)
				lock_.fetch_add(-1);
		} while (val > WRITE_LOCK);
	}

	void read_unlock()
	{
		lock_--;
	}
};
