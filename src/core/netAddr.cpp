#include "core/netAddr.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <unistd.h>
#include <netdb.h>
#include <arpa/inet.h>
#endif

namespace rally::net {
	std::vector<std::string> localIPv4() {
		std::vector<std::string> out;
#ifdef _WIN32
		WSADATA wsa;
		WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
		char name[256] = {};
		if (gethostname(name, sizeof name) == 0) {
			addrinfo hints{};
			hints.ai_family = AF_INET;
			addrinfo* res = nullptr;
			if (getaddrinfo(name, nullptr, &hints, &res) == 0) {
				for (addrinfo* p = res; p; p = p->ai_next) {
					char buf[64] = {};
					auto* sin = reinterpret_cast<sockaddr_in*>(p->ai_addr);
					if (inet_ntop(AF_INET, &sin->sin_addr, buf, sizeof buf)) {
						std::string s = buf;
						if (s.rfind("127.", 0) != 0) out.push_back(s);
					}
				}
				freeaddrinfo(res);
			}
		}
#ifdef _WIN32
		WSACleanup();
#endif
		return out;
	}
}
