#include "memcached-protocol.h"

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

static const char *next_char(const char *data, size_t size, char c)
{
	while (size-- && (data[0]) != c)
		++data;
	return data;
}

static const char *next_space(const char *data, size_t size)
{
	return next_char(data, size, ' ');
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

	if (strncmp("set", data, space - data) == 0) {
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
	return get_error();
}

static const ::std::string error(" ERROR\r\n");

size_t memcached_command::generate_packet(char *buffer, size_t size)
{
	if ((cmd_ == ERROR) && (size > error.size()))
		return error.copy(buffer, size);
	if ((cmd_ == REPLY) && (size >= data_size_)) {
		::std::memcpy(buffer, data_, data_size_);
		return data_size_;
	}

	return 0;
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
