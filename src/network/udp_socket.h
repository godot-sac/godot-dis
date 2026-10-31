#pragma once

// A small non-blocking IPv4 UDP socket over the operating system's socket API (Winsock on Windows,
// BSD sockets elsewhere). DisNetwork uses it instead of Godot's PacketPeerUDP because Godot never sets
// SO_REUSEADDR on Windows, so two DIS applications on one computer could not both listen on port 3000.
// Plain C++ (no Godot types).

#include <cstdint>
#include <string>
#include <vector>

namespace dis::net {

class UdpSocket {
public:
	UdpSocket() = default;
	~UdpSocket();
	UdpSocket(const UdpSocket &) = delete; // owns an OS handle: not copyable
	UdpSocket &operator=(const UdpSocket &) = delete;

	// Binds to `port` on `bind_address` ("" or "*" = every interface). With `reuse_address`, other
	// sockets (in this or another process) may bind the same port: each of them receives every broadcast and
	// multicast datagram; a unicast datagram reaches only one of them.
	bool open(uint16_t port, const std::string &bind_address, bool reuse_address);
	void close();
	bool is_open() const;

	bool set_broadcast(bool enabled);
	// Joins `group` on the interface that has IPv4 address `interface_address` ("" = the system's default).
	bool join_multicast(const std::string &group, const std::string &interface_address);
	bool leave_multicast(const std::string &group, const std::string &interface_address);
	// Interface that sends multicast datagrams ("" = default route) and whether they loop back to this host.
	bool set_multicast_interface(const std::string &interface_address);
	bool set_multicast_loop(bool enabled);

	// Returns false if the datagram could not be sent.
	bool send_to(const uint8_t *data, size_t size, const std::string &address, uint16_t port);
	// Reads one waiting datagram into `out`. Returns false when nothing is waiting (never blocks).
	bool receive(std::vector<uint8_t> &out, std::string &sender_address, uint16_t &sender_port);

	// Description of the last failure, e.g. "bind: address in use (10048)".
	const std::string &last_error() const { return error; }

private:
	intptr_t handle = -1; // SOCKET on Windows, int elsewhere
	std::string error;

	bool fail(const char *what);
};

} // namespace dis::net
