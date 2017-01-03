#pragma once
#include "packet-stream.h"

#include <hc.hpp>
#include <string>
#include <ostream>

struct mc_header {
	enum { MC_MAGIC_REQUEST = 0x80, MC_MAGIC_RESPONSE = 0x81 };
	uint8_t magic;
	uint8_t opcode;
	uint16_t key_size;
	uint8_t extras_size;
	uint8_t data_type;
	union {
		uint16_t vbucket_id;
		uint16_t status;
	};
	uint32_t total_size;
	uint32_t reserved;
	uint64_t cas;
};

struct udp_header {
	unsigned request_id;
	unsigned sequence_number;
	unsigned dgram_count;
	static udp_header parse(const char data[8]) __HC__ __CPU__;
};

class memcached_command
{
public:
	enum mc_cmd:char {
		ERROR, CLIENT_ERROR, GET, GETS, SET, ADD, REPLACE, APPEND, PREPEND, CAS, DELETE
	};
private:
        mc_cmd cmd_;
	bool no_reply_ = false;
	unsigned flags_ = 0;

	const char *key_ = nullptr;
	size_t key_size_ = 0;

	const char *data_ = nullptr;
	size_t data_size_ = 0;


	memcached_command(mc_cmd cmd) : cmd_(cmd) {};
	memcached_command(mc_cmd cmd, const char *key, size_t key_size,
	                  unsigned flags = 0, bool no_reply = false,
	                  const char *data = nullptr, size_t data_size = 0)
		: cmd_(cmd), no_reply_(no_reply), flags_(flags),
	          key_(key), key_size_(key_size),
       		  data_(data), data_size_(data_size) {};
public:
	static memcached_command get_error()
	{ return memcached_command(ERROR); }

	static memcached_command get_client_error(const char* str, size_t size)
	{ return memcached_command(CLIENT_ERROR, str, size); }

	static memcached_command parse_udp(const char *data, size_t size);

	const char * get_cmd_string() const;
	unsigned get_flags() const
	{ return flags_; }
	mc_cmd get_cmd() const
	{ return cmd_; };
	::std::string get_key() const
	{ return ::std::string(key_, key_size_); }// This creates a copy
	::std::vector<char> get_data() const
	{ return ::std::vector<char>(data_, data_ + data_size_); }
	bool generate_packet(packet_stream &pg);

	friend ::std::ostream & operator << (::std::ostream &O,
	                                     const memcached_command &cmd);
};
