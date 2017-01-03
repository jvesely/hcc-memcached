#include <hc.hpp>
#include <string>

struct udp_header {
	unsigned request_id;
	unsigned sequence_number;
	unsigned dgram_count;
	static udp_header parse(const char data[8]) __HC__ __CPU__;
};

class memcached_command
{
	enum mc_cmd {
		ERROR, CLIENT_ERROR, GET, GETS, SET, ADD, REPLACE, APPEND, PREPEND, CAS, DELETE,
	} cmd_;
	const char *key_ = nullptr;
	size_t key_size_ = 0;

	const char *data_ = nullptr;
	size_t data_size_ = 0;


	memcached_command(mc_cmd cmd) : cmd_(cmd) {};
	memcached_command(mc_cmd cmd, const char *key, size_t key_size,
	                  const char *data, size_t data_size)
		: cmd_(cmd), key_(key), key_size_(key_size),
       		  data_(data), data_size_(data_size) {};
public:
	static memcached_command get_error()
	{ return memcached_command(ERROR, nullptr, 0, nullptr, 0); }

	static memcached_command get_client_error(const char* str, size_t size)
	{ return memcached_command(CLIENT_ERROR, str, size, nullptr, 0); }

	static memcached_command parse_udp(const char *data, size_t size);

	::std::string get_key();
	size_t generate_packet(char *buffer, size_t size);
};
