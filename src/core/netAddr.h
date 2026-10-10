#pragma once
#include <string>
#include <vector>

namespace rally::net {
	// Non-loopback IPv4 addresses of this machine. Own translation unit
	// because the socket headers collide with raylib's names on Windows.
	std::vector<std::string> localIPv4();
}
