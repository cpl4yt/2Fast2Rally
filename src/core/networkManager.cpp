#include "networkManager.h"

#include <enet/enet.h>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <algorithm>

namespace rally {

	bool NetworkManager::ensureEnetInit() {
		// enet_initialize() is global and must run once per process before any
		// host is created; enet_deinitialize() is registered for shutdown.
		static bool inited = false;
		static bool ok = false;
		if (!inited) {
			inited = true;
			ok = (enet_initialize() == 0);
			if (ok) std::atexit(enet_deinitialize);
			else std::cerr << "ENet: enet_initialize() failed\n";
		}
		return ok;
	}

	NetworkManager::~NetworkManager() { stop(); }

	bool NetworkManager::startHost(uint16_t port, size_t maxPeers) {
		if (!ensureEnetInit()) return false;
		stop();
		ENetAddress addr;
		addr.host = ENET_HOST_ANY;
		addr.port = port;
		host_ = enet_host_create(&addr, maxPeers, 2 /*channels*/, 0, 0);
		if (!host_) {
			std::cerr << "ENet: failed to create host on port " << port << "\n";
			return false;
		}
		role_ = Role::Host;
		return true;
	}

	bool NetworkManager::startClient(const std::string& host, uint16_t port) {
		if (!ensureEnetInit()) return false;
		stop();
		host_ = enet_host_create(nullptr, 1, 2, 0, 0);
		if (!host_) {
			std::cerr << "ENet: failed to create client host\n";
			return false;
		}
		ENetAddress addr;
		if (enet_address_set_host(&addr, host.c_str()) != 0) {
			std::cerr << "ENet: could not resolve host \"" << host << "\"\n";
			enet_host_destroy(host_); host_ = nullptr;
			return false;
		}
		addr.port = port;
		serverPeer_ = enet_host_connect(host_, &addr, 2, 0);
		if (!serverPeer_) {
			std::cerr << "ENet: no available peers to connect with\n";
			enet_host_destroy(host_); host_ = nullptr;
			return false;
		}
		role_ = Role::Client;
		return true;
	}

	void NetworkManager::stop() {
		if (host_) {
			// Best-effort immediate disconnect so the other side hears about it.
			for (auto& kv : peerIds_) enet_peer_disconnect_now(kv.second, 0);
			if (serverPeer_) enet_peer_disconnect_now(serverPeer_, 0);
			enet_host_flush(host_);
			enet_host_destroy(host_);
		}
		host_ = nullptr;
		serverPeer_ = nullptr;
		peerIds_.clear();
		role_ = Role::None;
		nextPeerId_ = 1;
	}

	_ENetPeer* NetworkManager::peerById(uint32_t id) const {
		auto it = peerIds_.find(id);
		return it == peerIds_.end() ? nullptr : it->second;
	}

	// Stable peer id <-> ENetPeer mapping is carried in peer->data (ENet gives
	// us that void* slot per peer). Host assigns ids on connect; a client only
	// ever talks to "the server" (id 0).
	static uint32_t idOf(ENetPeer* peer) {
		return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(peer->data));
	}

	void NetworkManager::poll(std::vector<Event>& out, uint32_t timeoutMs) {
		if (!host_) return;
		ENetEvent ev;
		while (enet_host_service(host_, &ev, timeoutMs) > 0) {
			timeoutMs = 0; // only the first service call may block
			switch (ev.type) {
			case ENET_EVENT_TYPE_CONNECT: {
				uint32_t id = 0;
				if (role_ == Role::Host) {
					// Reject a banned address immediately - no id, no event.
					if (bannedHosts_.count(ev.peer->address.host)) {
						enet_peer_disconnect_now(ev.peer, 0);
						break;
					}
					id = nextPeerId_++;
					ev.peer->data = reinterpret_cast<void*>(static_cast<uintptr_t>(id));
					peerIds_[id] = ev.peer;
				}
				out.push_back({ Event::PeerConnect, id, MsgType::Chat, {} });
				break;
			}
			case ENET_EVENT_TYPE_DISCONNECT: {
				uint32_t id = (role_ == Role::Host) ? idOf(ev.peer) : 0;
				if (role_ == Role::Host) {
					peerIds_.erase(id);
					ev.peer->data = nullptr;
				}
				out.push_back({ Event::PeerDisconnect, id, MsgType::Chat, {} });
				break;
			}
			case ENET_EVENT_TYPE_RECEIVE: {
				uint32_t id = (role_ == Role::Host) ? idOf(ev.peer) : 0;
				if (ev.packet->dataLength >= 1) {
					MsgType type = static_cast<MsgType>(ev.packet->data[0]);
					std::string payload(reinterpret_cast<const char*>(ev.packet->data + 1),
						ev.packet->dataLength - 1);
					out.push_back({ Event::Message, id, type, std::move(payload) });
				}
				enet_packet_destroy(ev.packet);
				break;
			}
			default:
				break;
			}
		}
	}

	// One reliable packet: [MsgType byte][payload...].
	static ENetPacket* makePacket(NetworkManager::MsgType type, const std::string& payload) {
		ENetPacket* pkt = enet_packet_create(nullptr, payload.size() + 1, ENET_PACKET_FLAG_RELIABLE);
		pkt->data[0] = static_cast<uint8_t>(type);
		if (!payload.empty()) std::memcpy(pkt->data + 1, payload.data(), payload.size());
		return pkt;
	}

	void NetworkManager::broadcast(MsgType type, const std::string& payload) {
		if (!host_) return;
		enet_host_broadcast(host_, 0, makePacket(type, payload));
		enet_host_flush(host_);
	}

	void NetworkManager::sendTo(uint32_t peer, MsgType type, const std::string& payload) {
		ENetPeer* p = peerById(peer);
		if (!p || !host_) return;
		enet_peer_send(p, 0, makePacket(type, payload));
		enet_host_flush(host_);
	}

	void NetworkManager::sendToServer(MsgType type, const std::string& payload) {
		if (!serverPeer_ || !host_) return;
		enet_peer_send(serverPeer_, 0, makePacket(type, payload));
		enet_host_flush(host_);
	}

	// Unreliable, unsequenced packet: [MsgType byte][payload...] on channel 1.
	static ENetPacket* makeUnreliablePacket(NetworkManager::MsgType type, const std::string& payload) {
		ENetPacket* pkt = enet_packet_create(nullptr, payload.size() + 1, ENET_PACKET_FLAG_UNSEQUENCED);
		pkt->data[0] = static_cast<uint8_t>(type);
		if (!payload.empty()) std::memcpy(pkt->data + 1, payload.data(), payload.size());
		return pkt;
	}

	void NetworkManager::broadcastUnreliable(MsgType type, const std::string& payload) {
		if (!host_) return;
		enet_host_broadcast(host_, 1, makeUnreliablePacket(type, payload));
	}

	void NetworkManager::sendToServerUnreliable(MsgType type, const std::string& payload) {
		if (!serverPeer_ || !host_) return;
		enet_peer_send(serverPeer_, 1, makeUnreliablePacket(type, payload));
	}

	uint32_t NetworkManager::roundTripMs() const {
		if (serverPeer_) return serverPeer_->roundTripTime;
		if (!peerIds_.empty()) return peerIds_.begin()->second->roundTripTime;
		return 0;
	}

	std::vector<uint32_t> NetworkManager::peerIds() const {
		std::vector<uint32_t> ids;
		ids.reserve(peerIds_.size());
		for (const auto& kv : peerIds_) ids.push_back(kv.first);
		std::sort(ids.begin(), ids.end());
		return ids;
	}

	void NetworkManager::kick(uint32_t peerId) {
		if (ENetPeer* p = peerById(peerId)) enet_peer_disconnect(p, 0);
	}

	void NetworkManager::ban(uint32_t peerId, const std::string& label) {
		ENetPeer* p = peerById(peerId);
		if (!p) return;
		bannedHosts_[p->address.host] = label;
		enet_peer_disconnect(p, 0);
	}

	bool NetworkManager::unban(const std::string& labelOrIp) {
		for (auto it = bannedHosts_.begin(); it != bannedHosts_.end(); ++it) {
			char ipBuf[64] = {};
			ENetAddress addr{}; addr.host = it->first; addr.port = 0;
			enet_address_get_host_ip(&addr, ipBuf, sizeof ipBuf);
			if (it->second == labelOrIp || std::string(ipBuf) == labelOrIp) {
				bannedHosts_.erase(it);
				return true;
			}
		}
		return false;
	}

	std::vector<std::pair<std::string, std::string>> NetworkManager::bannedList() const {
		std::vector<std::pair<std::string, std::string>> out;
		out.reserve(bannedHosts_.size());
		for (const auto& kv : bannedHosts_) {
			char ipBuf[64] = {};
			ENetAddress addr{}; addr.host = kv.first; addr.port = 0;
			enet_address_get_host_ip(&addr, ipBuf, sizeof ipBuf);
			out.emplace_back(std::string(ipBuf), kv.second);
		}
		return out;
	}
}
