#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

// ENet types, forward-declared so this header stays free of <enet/enet.h>
// (and the winsock headers it drags in) - the include lives only in the .cpp.
struct _ENetHost;
struct _ENetPeer;

namespace rally {

	// Thin wrapper over ENet (carried over from Prairie). The same class backs
	// the hosting player (Role::Host) and the joining player (Role::Client).
	class NetworkManager {
	public:
		enum class Role { None, Host, Client };

		// Bump whenever a payload shape changes so mismatched builds refuse
		// each other cleanly instead of misreading packets.
		static constexpr uint32_t kProtocolVersion = 1;

		// First byte of every packet identifies the payload.
		enum class MsgType : uint8_t {
			Hello = 1,        // client -> host (reliable): JSON {name, protocolVersion}
			Rejected = 2,     // host -> client (reliable): reason text
			Lobby = 3,        // host -> client (reliable): JSON lobby state (roles, stage, car, ready flags)
			Ready = 4,        // client -> host (reliable): 1 byte ready flag
			LobbyRequest = 5, // client -> host (reliable): JSON {stage?, car?, swap?} - only host applies
			StartStage = 6,   // host -> client (reliable): JSON {stage, car, weather, timeOfDay, seed, resume?}
			CarState = 7,     // driver -> navigator (unreliable): binary snapshot, see session.cpp
			NoteCall = 8,     // navigator -> driver (reliable): u32 note index
			StageEvent = 9,   // driver -> navigator (reliable): JSON {kind: start|split|finish|reset|...}
			Pause = 10,       // either (reliable): 1 byte paused flag
			Chat = 11,        // either (reliable): UTF-8 text (quick messages)
			BackToLobby = 12, // host -> client (reliable): empty
			Ping = 13,        // either (unreliable): f64 send time, echoed back as Pong
			Pong = 14,
		};

		struct Event {
			enum Kind { PeerConnect, PeerDisconnect, Message } kind;
			uint32_t peer = 0;              // stable id of the peer involved (0 = the server, from a client's view)
			MsgType type = MsgType::Chat;   // valid when kind == Message
			std::string data;               // payload bytes (Message only)
		};

		NetworkManager() = default;
		~NetworkManager();
		NetworkManager(const NetworkManager&) = delete;
		NetworkManager& operator=(const NetworkManager&) = delete;

		// Start a server on `port` accepting up to `maxPeers` clients.
		bool startHost(uint16_t port, size_t maxPeers);
		// Connect to a server. Returns false only on immediate setup failure;
		// the connection itself completes asynchronously - watch poll() for a
		// PeerConnect event (or PeerDisconnect on timeout/refusal).
		bool startClient(const std::string& host, uint16_t port);
		void stop();

		Role role() const { return role_; }
		bool isActive() const { return host_ != nullptr; }
		size_t peerCount() const { return peerIds_.size(); }

		// Host: connected peer ids, ascending.
		std::vector<uint32_t> peerIds() const;
		// Host: disconnect a peer (graceful; a PeerDisconnect event follows).
		void kick(uint32_t peerId);
		void ban(uint32_t peerId, const std::string& label = std::string());
		bool unban(const std::string& labelOrIp);
		std::vector<std::pair<std::string, std::string>> bannedList() const;

		// Pump the network; appends any events to `out`. timeoutMs = 0 is fully
		// non-blocking (call once per frame); a small value blocks briefly.
		void poll(std::vector<Event>& out, uint32_t timeoutMs = 0);

		// Reliable, ordered sends on channel 0.
		void broadcast(MsgType type, const std::string& payload);            // host -> all peers
		void sendTo(uint32_t peer, MsgType type, const std::string& payload); // host -> one peer
		void sendToServer(MsgType type, const std::string& payload);          // client -> host

		// Unreliable, unordered sends on channel 1 - for high-rate snapshots
		// where a dropped packet is fine (the next one supersedes it).
		void broadcastUnreliable(MsgType type, const std::string& payload);     // host -> all
		void sendToServerUnreliable(MsgType type, const std::string& payload);  // client -> host

		// Round-trip time to the other side in ms (client: to host; host: first peer).
		uint32_t roundTripMs() const;

	private:
		static bool ensureEnetInit();
		_ENetPeer* peerById(uint32_t id) const;

		Role role_ = Role::None;
		_ENetHost* host_ = nullptr;                          // ENet host (server or client side)
		_ENetPeer* serverPeer_ = nullptr;                    // client's link to the server
		std::unordered_map<uint32_t, _ENetPeer*> peerIds_;   // host: stable id -> peer
		std::unordered_map<uint32_t, std::string> bannedHosts_; // host: banned IP (address.host) -> label
		uint32_t nextPeerId_ = 1;
	};
}
