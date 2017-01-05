#include "memcached-protocol.h"

#include "hc-std-helpers.h"
#include <iostream>

::std::ostream & operator << (::std::ostream &O, const mc_header &h)
{
	O << ::std::hex << "<" << (unsigned)h.magic
	  << ", " << (unsigned)h.opcode
	  << ", " << (unsigned)h.extras_size << ", " << (unsigned)h.data_type
	  << ", " << h.status << ", " << h.total_size
	  << ", " << h.cas << ">";
	return O;
}

udp_header udp_header::parse(const char data[8])
{
// assume little endina for now
	udp_header ret {
		.request_id = (unsigned)(data[1] << 8 | data[0]),
		.sequence_number = (unsigned)(data[3] << 8 | data[2]),
		.dgram_count = (unsigned)(data[4] << 8u | data[5]),
	};
	return ret;
}

::std::ostream & operator << (::std::ostream &O, const udp_header &h)
{
	O << "<" << h.request_id << "," << h.sequence_number
	  << "," << h.dgram_count << ">";
	return O;
}

static const char *next_char(const char *data, size_t size, char c) __HC__ __CPU__
{
	while (size-- && (data[0]) != c)
		++data;
	return data;
}

static const char *next_space(const char *data, size_t size) __HC__ __CPU__
{
	return next_char(data, size, ' ');
}

const char *
memcached_command::parse_get_key_end(const char *data, size_t size) __HC__
{
	const udp_header hdr = udp_header::parse(data);
	if (hdr.dgram_count != 1)
		return nullptr;
	data += 8;
	size -= 8;
	if (data[0] != 'g' || data[1] != 'e' || data[2] != 't' || data[3] != ' ')
		return nullptr;
	data += 4;
	const char *space = next_space(data, size);
	const char *end = next_char(data, size, '\r');
	return ::std::min(space, end);
}

#define STR(str) str, (sizeof(str))

memcached_command memcached_command::parse_udp(const char *data, size_t size)
{
	const udp_header hdr = udp_header::parse(data);
	if (hdr.dgram_count != 1)
		return get_client_error(STR("too many datagrams"));

	data += 8;
	size -= 8;
	const char *space = next_space(data, size);
	if (space[0] != ' ')
		return get_client_error(STR("malformed request"));

	if (::std::strncmp("set", data, space - data) == 0) {
		size -= (space - data - 1);
		data = space + 1;
		space = next_space(data, size);
		if (space[0] != ' ')
			return get_client_error(STR("malformed request key"));

		const char *key = data;
		size_t key_size = space - data;

		size -= (space - data - 1);
		data = space + 1;
		space = next_space(data, size);
		if (space[0] != ' ')
			return get_client_error(STR("malformed request flags"));
		unsigned flags = ::std::atoi(data);

		size -= (space - data - 1);
		data = space + 1;
		space = next_space(data, size);
		if (space[0] != ' ')
			return get_client_error(STR("malformed request time"));
		// Ignore expire time

		size -= (space - data - 1);
		data = space + 1;
		space = next_space(data, size);
		const char *end = next_char(data, size, '\r');
		space = ::std::min(space, end);
		if (space[0] != ' ' && space[0] != '\r')
			return get_client_error(STR("malformed request bytes"));
		unsigned data_size = ::std::atoi(data);

		bool noreply = (space[2] == 'n');
		if (space != end) {
			size -= (space - data - 1);
			data = space + 1;
			end = next_char(data, size, '\r');
			if (end[0] != '\r' || end[1] != '\n')
				return get_client_error(STR("malformed request reply"));
		}

		size -= (end - data - 2);
		data = end + 2;
		if (size < data_size)
			return get_client_error(STR("malformed request size"));

		return  memcached_command(SET, key, key_size, flags,
		                          noreply, data, data_size);
	}
	if (::std::strncmp("get", data, space - data) == 0) {
		size -= (space - data - 1);
		data = space + 1;
		space = next_space(data, size);
		const char *end = next_char(data, size, '\r');
		space = ::std::min(space, end);
		if (space[0] != ' ' && space[0] != '\r')
			return get_client_error(STR("malformed request key"));

		const char *key = data;
		size_t key_size = space - data;

		return  memcached_command(GET, key, key_size);
	}
	::std::cerr << "UNKNOWN COMMAND FOUND: " << ::std::string(data, size)
	            << ::std::endl;
	return get_error();
}

bool memcached_command::generate_packet(packet_stream &pg)
{
	if (cmd_ == ERROR)
		return (pg << "ERROR\r\n").is_ok();
	if (cmd_ == CLIENT_ERROR)
		return (pg << "CLIENT ERROR").append(key_ , key_size_).is_ok();

	return false;
}

const char *memcached_command::get_cmd_string() const
{
	switch (cmd_)
	{
	case ERROR: return "ERROR";
	case CLIENT_ERROR: return "CLIENT ERROR";
	case GET: return "GET";
	case SET: return "SET";
	}
	return "UNKNOWN";
}

::std::ostream & operator << (::std::ostream &O, const memcached_command &cmd)
{
	O << "[" << cmd.get_cmd_string() << ", " << cmd.flags_
	  << ", " << (cmd.key_ ? ::std::string(cmd.key_, cmd.key_size_)
	               : ::std::string("nokey"))
	  << ", " << (cmd.no_reply_ ? "no" : "do ") << "reply"
	  << ", " << (cmd.data_ ? ::std::string(cmd.data_, cmd.data_size_)
	               : ::std::string("nodata"))
	   << "]";
	return O;
}
