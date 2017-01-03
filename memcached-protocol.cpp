#include "memcached-protocol.h"

#include <iostream>

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

static const char *next_space(const char *data, size_t size)
{
	while (size-- && (*data++) != ' ');
	return data;
}

memcached_command memcached_command::parse_udp(const char *data, size_t size)
{
	const udp_header hdr = udp_header::parse(data);
	if (hdr.dgram_count != 1)
		return get_error();

	const char *space = next_space(data + 8, size - 8);
	if (space == (data + size))
		return get_client_error(nullptr, 0);

	data += 8;
	::std::string command(data, space);
	mc_cmd cmd = ERROR;
	if (command == "get") {
		cmd = GET;
	}
	return memcached_command(ERROR);
}

static const ::std::string error(" ERROR\r\n");

size_t memcached_command::generate_packet(char *buffer, size_t size)
{
	if ((cmd_ == ERROR) && (size > error.size()))
		return error.copy(buffer, size);

	return 0;
}
