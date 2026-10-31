#include "udp_socket.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
using socklen_t = int;
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <cstring>

namespace dis::net {

namespace {

#ifdef _WIN32
constexpr intptr_t INVALID = static_cast<intptr_t>(INVALID_SOCKET);

int last_os_error() {
	return WSAGetLastError();
}

bool would_block(int code) {
	return code == WSAEWOULDBLOCK || code == WSAECONNRESET; // ICMP "port unreachable" from an earlier send
}

// Winsock must be started once per process before any socket call.
bool ensure_winsock() {
	static const bool started = [] {
		WSADATA data;
		return WSAStartup(MAKEWORD(2, 2), &data) == 0;
	}();
	return started;
}

SOCKET native(intptr_t handle) {
	return static_cast<SOCKET>(handle);
}
#else
constexpr intptr_t INVALID = -1;

int last_os_error() {
	return errno;
}

bool would_block(int code) {
	return code == EAGAIN || code == EWOULDBLOCK;
}

bool ensure_winsock() {
	return true;
}

int native(intptr_t handle) {
	return static_cast<int>(handle);
}
#endif

// IPv4 address text -> in_addr. "" and "*" mean any.
bool parse_ipv4(const std::string &text, in_addr &out) {
	if (text.empty() || text == "*") {
		out.s_addr = htonl(INADDR_ANY);
		return true;
	}
	return inet_pton(AF_INET, text.c_str(), &out) == 1;
}

template <typename T>
bool set_option(intptr_t handle, int level, int name, const T &value) {
	return setsockopt(native(handle), level, name, reinterpret_cast<const char *>(&value), sizeof(value)) == 0;
}

} // namespace

UdpSocket::~UdpSocket() {
	close();
}

bool UdpSocket::fail(const char *what) {
	error = std::string(what) + " failed (OS error " + std::to_string(last_os_error()) + ")";
	return false;
}

bool UdpSocket::is_open() const {
	return handle != INVALID;
}

bool UdpSocket::open(uint16_t port, const std::string &bind_address, bool reuse_address) {
	close();
	if (!ensure_winsock()) {
		error = "WSAStartup failed";
		return false;
	}
	in_addr address{};
	if (!parse_ipv4(bind_address, address)) {
		error = "invalid bind address: " + bind_address;
		return false;
	}
	handle = static_cast<intptr_t>(socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP));
	if (handle == INVALID) {
		return fail("socket");
	}
	if (reuse_address) {
		const int on = 1;
		set_option(handle, SOL_SOCKET, SO_REUSEADDR, on);
#ifdef SO_REUSEPORT
		set_option(handle, SOL_SOCKET, SO_REUSEPORT, on); // Linux / macOS need it to share a unicast port
#endif
	}
	sockaddr_in local{};
	local.sin_family = AF_INET;
	local.sin_port = htons(port);
	local.sin_addr = address;
	if (bind(native(handle), reinterpret_cast<const sockaddr *>(&local), sizeof(local)) != 0) {
		fail("bind");
		close();
		return false;
	}
	// Non-blocking, so receive() returns at once when nothing is waiting.
#ifdef _WIN32
	u_long non_blocking = 1;
	ioctlsocket(native(handle), FIONBIO, &non_blocking);
#else
	fcntl(native(handle), F_SETFL, fcntl(native(handle), F_GETFL, 0) | O_NONBLOCK);
#endif
	return true;
}

void UdpSocket::close() {
	if (handle == INVALID) {
		return;
	}
#ifdef _WIN32
	closesocket(native(handle));
#else
	::close(native(handle));
#endif
	handle = INVALID;
}

bool UdpSocket::set_broadcast(bool enabled) {
	const int value = enabled ? 1 : 0;
	return set_option(handle, SOL_SOCKET, SO_BROADCAST, value) || fail("SO_BROADCAST");
}

static bool membership(intptr_t handle, int option, const std::string &group, const std::string &interface_address) {
	ip_mreq request{};
	if (!parse_ipv4(group, request.imr_multiaddr) || !parse_ipv4(interface_address, request.imr_interface)) {
		return false;
	}
	return set_option(handle, IPPROTO_IP, option, request);
}

bool UdpSocket::join_multicast(const std::string &group, const std::string &interface_address) {
	return membership(handle, IP_ADD_MEMBERSHIP, group, interface_address) || fail("IP_ADD_MEMBERSHIP");
}

bool UdpSocket::leave_multicast(const std::string &group, const std::string &interface_address) {
	return membership(handle, IP_DROP_MEMBERSHIP, group, interface_address) || fail("IP_DROP_MEMBERSHIP");
}

bool UdpSocket::set_multicast_interface(const std::string &interface_address) {
	in_addr address{};
	if (!parse_ipv4(interface_address, address)) {
		error = "invalid interface address: " + interface_address;
		return false;
	}
	return set_option(handle, IPPROTO_IP, IP_MULTICAST_IF, address) || fail("IP_MULTICAST_IF");
}

bool UdpSocket::set_multicast_loop(bool enabled) {
	// Windows takes a DWORD, BSD sockets an unsigned char.
#ifdef _WIN32
	const DWORD value = enabled ? 1 : 0;
#else
	const unsigned char value = enabled ? 1 : 0;
#endif
	return set_option(handle, IPPROTO_IP, IP_MULTICAST_LOOP, value) || fail("IP_MULTICAST_LOOP");
}

bool UdpSocket::send_to(const uint8_t *data, size_t size, const std::string &address, uint16_t port) {
	sockaddr_in remote{};
	remote.sin_family = AF_INET;
	remote.sin_port = htons(port);
	if (address.empty() || inet_pton(AF_INET, address.c_str(), &remote.sin_addr) != 1) {
		error = "invalid destination address: " + address;
		return false;
	}
	const auto sent = sendto(native(handle), reinterpret_cast<const char *>(data), static_cast<int>(size), 0,
			reinterpret_cast<const sockaddr *>(&remote), sizeof(remote));
	return (sent >= 0 && static_cast<size_t>(sent) == size) || fail("sendto");
}

bool UdpSocket::receive(std::vector<uint8_t> &out, std::string &sender_address, uint16_t &sender_port) {
	if (handle == INVALID) {
		return false;
	}
	out.resize(65536); // largest UDP payload
	sockaddr_in remote{};
	socklen_t remote_size = sizeof(remote);
	for (;;) {
		const auto received = recvfrom(native(handle), reinterpret_cast<char *>(out.data()), static_cast<int>(out.size()),
				0, reinterpret_cast<sockaddr *>(&remote), &remote_size);
		if (received >= 0) {
			out.resize(static_cast<size_t>(received));
			char text[INET_ADDRSTRLEN] = {};
			inet_ntop(AF_INET, &remote.sin_addr, text, sizeof(text));
			sender_address = text;
			sender_port = ntohs(remote.sin_port);
			return true;
		}
		const int code = last_os_error();
#ifdef _WIN32
		if (code == WSAECONNRESET) {
			continue; // stale ICMP error from an earlier send; try the next datagram
		}
#endif
		if (!would_block(code)) {
			fail("recvfrom");
		}
		out.clear();
		return false;
	}
}

} // namespace dis::net
