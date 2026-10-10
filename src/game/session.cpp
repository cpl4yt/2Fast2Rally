#include "game/session.h"
#include "core/log.h"
#include "core/netAddr.h"
#include <cstring>

namespace rally {
	using Msg = NetworkManager::MsgType;

	nlohmann::json LobbyState::toJson() const {
		return { {"hostName", hostName}, {"clientName", clientName}, {"clientConnected", clientConnected},
			{"driverIsHost", driverIsHost}, {"stage", stage}, {"car", car}, {"weather", weather},
			{"timeOfDay", timeOfDay}, {"hostReady", hostReady}, {"clientReady", clientReady} };
	}

	void LobbyState::fromJson(const nlohmann::json& j) {
		hostName = j.value("hostName", hostName);
		clientName = j.value("clientName", clientName);
		clientConnected = j.value("clientConnected", clientConnected);
		driverIsHost = j.value("driverIsHost", driverIsHost);
		stage = j.value("stage", stage);
		car = j.value("car", car);
		weather = j.value("weather", weather);
		timeOfDay = j.value("timeOfDay", timeOfDay);
		hostReady = j.value("hostReady", hostReady);
		clientReady = j.value("clientReady", clientReady);
	}

	void packCar(const CarVisual& v, CarStatePacket& p) {
		p.pos[0] = v.position.x; p.pos[1] = v.position.y; p.pos[2] = v.position.z;
		p.rot[0] = v.rotation.x; p.rot[1] = v.rotation.y; p.rot[2] = v.rotation.z; p.rot[3] = v.rotation.w;
		p.vel[0] = v.velocity.x; p.vel[1] = v.velocity.y; p.vel[2] = v.velocity.z;
		p.steerAngle = v.steerAngle; p.steerInput = v.steerInput;
		for (int i = 0; i < 4; ++i) {
			p.wheelOffset[i] = v.wheelOffset[i]; p.spinRate[i] = v.wheelSpinRate[i];
			p.slip[i] = v.wheelSlip[i]; p.surface[i] = v.wheelSurface[i];
		}
		p.contact = v.wheelContact; p.handbrake = v.handbrake ? 1 : 0;
		p.gear = (int8_t)v.gear; p.rpm = v.rpm; p.throttle = v.throttle; p.brake = v.brake;
		p.boost = v.boost; p.speed = v.speed; p.dirt = v.dirt; p.damage = v.damage; p.lightsBroken = v.lightsBroken;
	}

	void unpackCar(const CarStatePacket& p, CarVisual& v) {
		v.position = { p.pos[0], p.pos[1], p.pos[2] };
		v.rotation = { p.rot[0], p.rot[1], p.rot[2], p.rot[3] };
		v.velocity = { p.vel[0], p.vel[1], p.vel[2] };
		v.steerAngle = p.steerAngle; v.steerInput = p.steerInput;
		for (int i = 0; i < 4; ++i) {
			v.wheelOffset[i] = p.wheelOffset[i]; v.wheelSpinRate[i] = p.spinRate[i];
			v.wheelSlip[i] = p.slip[i]; v.wheelSurface[i] = p.surface[i];
		}
		v.wheelContact = p.contact; v.handbrake = p.handbrake != 0;
		v.gear = p.gear; v.rpm = p.rpm; v.throttle = p.throttle; v.brake = p.brake;
		v.boost = p.boost; v.speed = p.speed; v.dirt = p.dirt; v.damage = p.damage; v.lightsBroken = p.lightsBroken;
	}

	bool Session::host(uint16_t port, const std::string& name) {
		stop();
		myName_ = name;
		port_ = port;
		if (!net_.startHost(port, 1)) return false;
		log::info("session: hosting on UDP %u", port);
		return true;
	}

	bool Session::join(const std::string& address, uint16_t port, const std::string& name) {
		stop();
		myName_ = name;
		address_ = address;
		port_ = port;
		if (!net_.startClient(address, port)) return false;
		log::info("session: connecting to %s:%u", address.c_str(), port);
		return true;
	}

	void Session::stop() {
		net_.stop();
		partner_ = false;
		partnerName_.clear();
	}

	void Session::sendReliable(Msg t, const std::string& payload) {
		if (!net_.isActive()) return;
		if (isHost()) net_.broadcast(t, payload);
		else net_.sendToServer(t, payload);
	}

	void Session::poll(std::vector<NetEvent>& out) {
		if (!net_.isActive()) return;
		std::vector<NetworkManager::Event> evs;
		net_.poll(evs, 0);
		for (auto& e : evs) {
			if (e.kind == NetworkManager::Event::PeerConnect) {
				if (!isHost()) {
					nlohmann::json hello = { {"name", myName_}, {"protocolVersion", NetworkManager::kProtocolVersion} };
					net_.sendToServer(Msg::Hello, hello.dump());
				}
				continue;
			}
			if (e.kind == NetworkManager::Event::PeerDisconnect) {
				if (isHost() && e.peer != partnerPeer_) continue;
				bool had = partner_;
				partner_ = false;
				NetEvent ne{ NetEvent::Disconnected };
				ne.text = had ? partnerName_ : "";
				out.push_back(ne);
				if (!isHost()) { log::info("session: disconnected from host"); net_.stop(); }
				continue;
			}
			// Message.
			const std::string& d = e.data;
			auto parse = [&](nlohmann::json& j) {
				try { j = nlohmann::json::parse(d); return true; }
				catch (...) { log::warn("session: bad json in msg %d", (int)e.type); return false; }
			};
			switch (e.type) {
			case Msg::Hello: {
				if (!isHost()) break;
				nlohmann::json j;
				if (!parse(j)) break;
				uint32_t ver = j.value("protocolVersion", 0u);
				if (ver != NetworkManager::kProtocolVersion) {
					net_.sendTo(e.peer, Msg::Rejected, "Version mismatch - both players need the same build of 2Fast2Rally.");
					net_.kick(e.peer);
					break;
				}
				if (partner_ && e.peer != partnerPeer_) {
					net_.sendTo(e.peer, Msg::Rejected, "This rally already has a co-driver.");
					net_.kick(e.peer);
					break;
				}
				partner_ = true;
				partnerPeer_ = e.peer;
				partnerName_ = j.value("name", std::string("Partner"));
				NetEvent ne{ NetEvent::Connected };
				ne.text = partnerName_;
				out.push_back(ne);
				log::info("session: %s joined", partnerName_.c_str());
				break;
			}
			case Msg::Rejected: {
				NetEvent ne{ NetEvent::Rejected };
				ne.text = d;
				out.push_back(ne);
				break;
			}
			case Msg::Lobby: {
				NetEvent ne{ NetEvent::Lobby };
				if (!parse(ne.json)) break;
				if (!partner_) {
					partner_ = true;
					partnerName_ = ne.json.value("hostName", std::string("Host"));
					NetEvent c{ NetEvent::Connected };
					c.text = partnerName_;
					out.push_back(c);
				}
				out.push_back(ne);
				break;
			}
			case Msg::LobbyRequest: { NetEvent ne{ NetEvent::LobbyRequest }; if (parse(ne.json)) out.push_back(ne); break; }
			case Msg::StartStage: { NetEvent ne{ NetEvent::Start }; if (parse(ne.json)) out.push_back(ne); break; }
			case Msg::StageEvent: { NetEvent ne{ NetEvent::StageEvent }; if (parse(ne.json)) out.push_back(ne); break; }
			case Msg::CarState: {
				if (d.size() != sizeof(CarStatePacket)) break;
				NetEvent ne{ NetEvent::CarState };
				std::memcpy(&ne.car, d.data(), sizeof(CarStatePacket));
				out.push_back(ne);
				break;
			}
			case Msg::NoteCall: {
				if (d.size() != 4) break;
				NetEvent ne{ NetEvent::NoteCall };
				uint32_t v; std::memcpy(&v, d.data(), 4);
				ne.value = (int)v;
				out.push_back(ne);
				break;
			}
			case Msg::Pause: { NetEvent ne{ NetEvent::Pause }; ne.value = (!d.empty() && d[0]) ? 1 : 0; out.push_back(ne); break; }
			case Msg::BackToLobby: out.push_back(NetEvent{ NetEvent::BackToLobby }); break;
			case Msg::Chat: { NetEvent ne{ NetEvent::Chat }; ne.text = d; out.push_back(ne); break; }
			default: break;
			}
		}
	}

	void Session::sendLobby(const LobbyState& l) { sendReliable(Msg::Lobby, l.toJson().dump()); }
	void Session::sendLobbyRequest(const nlohmann::json& j) { sendReliable(Msg::LobbyRequest, j.dump()); }
	void Session::sendStart(const nlohmann::json& j) { sendReliable(Msg::StartStage, j.dump()); }
	void Session::sendStageEvent(const nlohmann::json& j) { sendReliable(Msg::StageEvent, j.dump()); }
	void Session::sendBackToLobby() { sendReliable(Msg::BackToLobby, ""); }
	void Session::sendChat(const std::string& text) { sendReliable(Msg::Chat, text); }
	void Session::sendPause(bool p) { sendReliable(Msg::Pause, std::string(1, p ? 1 : 0)); }

	void Session::sendNoteCall(int index) {
		uint32_t v = (uint32_t)index;
		sendReliable(Msg::NoteCall, std::string(reinterpret_cast<const char*>(&v), 4));
	}

	void Session::sendCarState(const CarStatePacket& p) {
		if (!net_.isActive()) return;
		std::string payload(reinterpret_cast<const char*>(&p), sizeof p);
		if (isHost()) net_.broadcastUnreliable(Msg::CarState, payload);
		else net_.sendToServerUnreliable(Msg::CarState, payload);
	}

	std::vector<std::string> Session::localAddresses() { return net::localIPv4(); }
}
