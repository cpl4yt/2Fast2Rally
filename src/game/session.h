#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "core/networkManager.h"
#include "game/vehicle.h"

namespace rally {
	// Lobby, owned by the host and mirrored to the client.
	struct LobbyState {
		std::string hostName = "Host", clientName;
		bool clientConnected = false;
		bool driverIsHost = true;
		int stage = 0, car = 0, weather = 0, timeOfDay = 0;
		bool hostReady = false, clientReady = false;
		bool rally = false;          // run every stage back to back
		nlohmann::json toJson() const;
		void fromJson(const nlohmann::json& j);
	};

#pragma pack(push, 1)
	// Driver -> navigator snapshot, ~30 Hz, unreliable. Both ends run the
	// same build (protocol version is checked on join), so a packed POD is fine.
	struct CarStatePacket {
		uint32_t seq;
		double simTime;          // driver's monotonic sim clock
		float stageTime;
		float progress;
		uint8_t phase;           // RunPhase
		uint8_t paused;
		int8_t gear;
		uint8_t lightsBroken;
		float pos[3], rot[4], vel[3];
		float steerAngle, steerInput;
		float wheelOffset[4], spinRate[4], slip[4];
		uint8_t surface[4];
		uint8_t contact, handbrake, resets, splitIndex;
		float rpm, throttle, brake, boost, speed, dirt, damage;
		float penalties;
		float splitTimes[2];
		uint16_t backfireSeq, shiftSeq, impactSeq, cuts;
		float impactStrength;
	};
#pragma pack(pop)

	void packCar(const CarVisual& v, CarStatePacket& p);
	void unpackCar(const CarStatePacket& p, CarVisual& v);

	struct NetEvent {
		enum Kind { Connected, Disconnected, Rejected, Lobby, LobbyRequest, Start, CarState, NoteCall, StageEvent, Pause, BackToLobby, Chat } kind;
		std::string text;
		nlohmann::json json;
		CarStatePacket car{};
		int value = 0;
	};

	class Session {
	public:
		bool host(uint16_t port, const std::string& name);
		bool join(const std::string& address, uint16_t port, const std::string& name);
		void stop();

		bool active() const { return net_.isActive(); }
		bool isHost() const { return net_.role() == NetworkManager::Role::Host; }
		bool partnerConnected() const { return partner_; }
		const std::string& partnerName() const { return partnerName_; }
		uint32_t ping() const { return net_.roundTripMs(); }
		const std::string& lastJoinAddress() const { return address_; }
		uint16_t port() const { return port_; }

		void poll(std::vector<NetEvent>& out);

		void sendLobby(const LobbyState& l);
		void sendLobbyRequest(const nlohmann::json& j);
		void sendStart(const nlohmann::json& j);
		void sendCarState(const CarStatePacket& p);
		void sendNoteCall(int index);
		void sendStageEvent(const nlohmann::json& j);
		void sendPause(bool paused);
		void sendBackToLobby();
		void sendChat(const std::string& text);

		// Local IPv4 addresses to show the host (so they can tell their partner).
		static std::vector<std::string> localAddresses();

	private:
		void sendReliable(NetworkManager::MsgType t, const std::string& payload);
		NetworkManager net_;
		std::string myName_, partnerName_, address_;
		uint16_t port_ = 0;
		bool partner_ = false;
		uint32_t partnerPeer_ = 0;
	};
}
