#pragma once
#include "packet-stream.h"
#include "string_ref.h"

#ifdef __HCC__
#  include <hc.hpp>
#else
#  define __HC__
#  define __CPU__
#endif
#include <string>
#include <ostream>

struct mc_binary_header {
	enum { MAGIC_REQUEST = 0x80, MAGIC_RESPONSE = 0x81 };
	enum opcode_t:uint8_t {
		OP_GET = 0x00,
		OP_SET = 0x01,
		OP_ERROR = 0xff
	};
	enum response_t:uint16_t {
		RE_OK = 0x0000,
		RE_NOT_FOUND = 0x0001,
		RE_VALUE_TOO_LARGE = 0x0003,
		RE_NOT_SUPPORTED = 0x0083,
		RE_INTERNAL_ERROR = 0x0084
	};
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

static_assert(sizeof(mc_binary_header) == 24, "Incorrect header struct size");

struct udp_header {
	uint16_t request_id;
	uint16_t sequence_number;
	uint16_t dgram_count;
	uint16_t res_;
};
static_assert(sizeof(udp_header) == 8, "Incorrect udp header struct size");

class mc_binary_packet
{
	struct udp_header *udp_header_;
	mc_binary_header *header_;
	char *data_;
	mc_binary_packet(udp_header *udp, mc_binary_header *header, char *data):
		udp_header_(udp), header_(header), data_(data) {};

public:

	static mc_binary_packet parse_udp(char *data, size_t size)
	{

		static const size_t headers_size_ =
			sizeof(udp_header) + sizeof(mc_binary_header);
		if ((data[8] == (char)mc_binary_header::MAGIC_REQUEST ||
		     data[8] == (char)mc_binary_header::MAGIC_REQUEST) &&
		    size >= headers_size_)
			return mc_binary_packet((udp_header*)data,
			                        (mc_binary_header*)(data + 8),
						data + headers_size_);
		return mc_binary_packet(nullptr, nullptr, nullptr);
	}

	bool isValid() const
	{ return header_; }

	mc_binary_header::opcode_t get_cmd() const __CPU__ __HC__
	{ return header_ ? (mc_binary_header::opcode_t)header_->opcode
	                 : mc_binary_header::OP_ERROR; }

	string_ref get_key() const __CPU__ __HC__
	{ return string_ref(data_ + ::std::ntoh(header_->extras_size),
	                    ::std::ntoh(header_->key_size)); }

	string_ref get_value() const
	{ return string_ref(data_ + ::std::ntoh(header_->extras_size)
	                          + ::std::ntoh(header_->key_size),
	                    ::std::ntoh(header_->total_size) -
	                    ::std::ntoh(header_->extras_size) -
	                    ::std::ntoh(header_->key_size)); }
	size_t get_size() const
	{ return header_ ? ::std::ntoh(header_->total_size) : 0 ; }

	size_t set_response(mc_binary_header::response_t response,
	                    size_t key_size = 0, size_t value_size = 0) __HC__ __CPU__
	{
		header_->magic = mc_binary_header::MAGIC_RESPONSE;
		header_->key_size = ::std::hton<uint16_t>(key_size);
		header_->extras_size = 0;
		header_->status = ::std::hton<uint16_t>(response);
		header_->total_size = ::std::hton<uint32_t>(key_size + value_size);
		// Keep this cast. it works around HCC bugs
		volatile unsigned long f = (unsigned long)udp_header_;
		return (f ? sizeof(*udp_header_) : 0) +
		        sizeof(*header_) + key_size + value_size;
	}

	size_t set_response(mc_binary_header::response_t response,
	                    const ::std::string &key,
	                    const ::std::vector<char> &value)  __HC__ __CPU__
	{
		::std::memcpy(data_ , key.data(), key.size());
		::std::memcpy(data_ + key.size(), value.data(), value.size());
		return set_response(response, key.size(), value.size());
	}

	friend ::std::ostream & operator << (::std::ostream &O,
	                                     const mc_binary_packet &cmd);

};

class memcached_command
{
public:
	enum mc_cmd:char {
		ERROR, CLIENT_ERROR, GET, GETS, SET, ADD, REPLACE, APPEND, PREPEND, CAS, DELETE
	};
private:
	uint8_t cmd_;
	bool no_reply_ = false;
	unsigned flags_ = 0;

	const char *key_ = nullptr;
	size_t key_size_ = 0;

	const char *data_ = nullptr;
	size_t data_size_ = 0;


	memcached_command(mc_cmd cmd) __HC__ __CPU__ : cmd_(cmd) {};
	memcached_command(mc_cmd cmd, const char *key, size_t key_size,
	                  unsigned flags = 0, bool no_reply = false,
	                  const char *data = nullptr, size_t data_size = 0)
		__HC__ __CPU__
		: cmd_(cmd), no_reply_(no_reply), flags_(flags),
	          key_(key), key_size_(key_size),
       		  data_(data), data_size_(data_size) {};
public:
	~memcached_command() __HC__ __CPU__ {};
	static memcached_command get_error()
	{ return memcached_command(ERROR); }

	static memcached_command get_client_error(const char* str, size_t size)
	{ return memcached_command(CLIENT_ERROR, str, size); }

	static memcached_command parse_udp(const char *data, size_t size);
	/* This is a simplified version to get around HCC crashes */
	static const char *
		parse_get_key_end(const char *data, size_t size) __HC__;

	const char * get_cmd_string() const;
	unsigned get_flags() const
	{ return flags_; }
	mc_cmd get_cmd() const __HC__ __CPU__
	{ return (mc_cmd)cmd_; };
	::std::string get_key() const
	{ return ::std::string(key_, key_size_); }// This creates a copy
	::std::vector<char> get_data() const
	{ return ::std::vector<char>(data_, data_ + data_size_); }
	bool generate_packet(packet_stream &pg);

	friend ::std::ostream & operator << (::std::ostream &O,
	                                     const memcached_command &cmd);
};
