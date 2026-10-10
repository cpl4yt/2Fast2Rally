#include "core/settings.h"
#include "core/log.h"
#include <raylib.h>
#include <nlohmann/json.hpp>
#include <fstream>

namespace rally {
	const char* kSettingsPath = "settings.json";

	const char* actionName(Action a) {
		switch (a) {
		case Action::Throttle: return "Throttle";
		case Action::Brake: return "Brake / Reverse";
		case Action::SteerLeft: return "Steer Left";
		case Action::SteerRight: return "Steer Right";
		case Action::Handbrake: return "Handbrake";
		case Action::ShiftUp: return "Shift Up";
		case Action::ShiftDown: return "Shift Down";
		case Action::Reset: return "Reset to Road";
		case Action::Camera: return "Change Camera";
		case Action::Pause: return "Pause";
		case Action::CallNote: return "Call Next Note (Nav)";
		case Action::NavView: return "Navigator View (Nav)";
		case Action::LookBack: return "Look Back";
		case Action::Debug: return "Debug Overlay";
		case Action::Clutch: return "Clutch (key)";
		case Action::Gear1: return "Gear 1 (H-shifter)";
		case Action::Gear2: return "Gear 2";
		case Action::Gear3: return "Gear 3";
		case Action::Gear4: return "Gear 4";
		case Action::Gear5: return "Gear 5";
		case Action::Gear6: return "Gear 6";
		case Action::GearReverse: return "Reverse gear";
		case Action::GearNeutral: return "Neutral";
		case Action::Ignition: return "Ignition / start engine";
		default: return "?";
		}
	}

	Settings::Settings() { resetControls(); }

	void Settings::resetControls() {
		auto set = [&](Action a, int k, int alt, int p) {
			keys[(int)a] = k; keysAlt[(int)a] = alt; pad[(int)a] = p; joy[(int)a] = 0;
		};
		set(Action::Throttle, KEY_W, KEY_UP, 0);
		set(Action::Brake, KEY_S, KEY_DOWN, 0);
		set(Action::SteerLeft, KEY_A, KEY_LEFT, 0);
		set(Action::SteerRight, KEY_D, KEY_RIGHT, 0);
		set(Action::Handbrake, KEY_SPACE, 0, GAMEPAD_BUTTON_RIGHT_FACE_LEFT);
		set(Action::ShiftUp, KEY_E, 0, GAMEPAD_BUTTON_RIGHT_TRIGGER_1);
		set(Action::ShiftDown, KEY_Q, 0, GAMEPAD_BUTTON_LEFT_TRIGGER_1);
		set(Action::Reset, KEY_R, 0, GAMEPAD_BUTTON_RIGHT_FACE_UP);
		set(Action::Camera, KEY_C, 0, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT);
		set(Action::Pause, KEY_ESCAPE, KEY_P, GAMEPAD_BUTTON_MIDDLE_RIGHT);
		set(Action::CallNote, KEY_SPACE, KEY_ENTER, GAMEPAD_BUTTON_RIGHT_FACE_DOWN);
		set(Action::NavView, KEY_TAB, 0, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT);
		set(Action::LookBack, KEY_B, 0, GAMEPAD_BUTTON_RIGHT_THUMB);
		set(Action::Debug, KEY_F3, 0, 0);
		set(Action::Clutch, KEY_LEFT_SHIFT, 0, 0);
		set(Action::Gear1, KEY_ONE, 0, 0); set(Action::Gear2, KEY_TWO, 0, 0); set(Action::Gear3, KEY_THREE, 0, 0);
		set(Action::Gear4, KEY_FOUR, 0, 0); set(Action::Gear5, KEY_FIVE, 0, 0); set(Action::Gear6, KEY_SIX, 0, 0);
		set(Action::GearReverse, KEY_ZERO, 0, 0);
		set(Action::GearNeutral, KEY_N, 0, 0);
		set(Action::Ignition, KEY_I, 0, 0);
		steerAxis = { GAMEPAD_AXIS_LEFT_X, false, 0, -1 };
		throttleAxis = { GAMEPAD_AXIS_RIGHT_TRIGGER, false, 1, -1 };
		brakeAxis = { GAMEPAD_AXIS_LEFT_TRIGGER, false, 1, -1 };
		clutchAxis = { -1, false, 1, -1 };
	}

	static nlohmann::json axisToJson(const AxisBinding& a) {
		return { {"axis", a.axis}, {"invert", a.invert}, {"mode", a.mode}, {"device", a.device} };
	}
	static void axisFromJson(const nlohmann::json& j, AxisBinding& a) {
		if (!j.is_object()) return;
		a.axis = j.value("axis", a.axis);
		a.invert = j.value("invert", a.invert);
		a.mode = j.value("mode", a.mode);
	}

	bool Settings::load(const std::string& path) {
		std::ifstream in(path);
		if (!in) return false;
		try {
			nlohmann::json j = nlohmann::json::parse(in);
			fullscreen = j.value("fullscreen", fullscreen);
			vsync = j.value("vsync", vsync);
			windowW = j.value("windowW", windowW);
			windowH = j.value("windowH", windowH);
			fov = j.value("fov", fov);
			drawDistance = j.value("drawDistance", drawDistance);
			particleQuality = j.value("particleQuality", particleQuality);
			showFps = j.value("showFps", showFps);
			if (j.contains("msaa")) msaa = j["msaa"].is_boolean() ? j["msaa"].get<bool>() : (j["msaa"].is_number() && j["msaa"].get<int>() != 0);
			masterVolume = j.value("masterVolume", masterVolume);
			engineVolume = j.value("engineVolume", engineVolume);
			effectsVolume = j.value("effectsVolume", effectsVolume);
			voiceVolume = j.value("voiceVolume", voiceVolume);
			musicVolume = j.value("musicVolume", musicVolume);
			useMph = j.value("useMph", useMph);
			manualGearbox = j.value("manualGearbox", manualGearbox);
			transmission = j.value("transmission", manualGearbox ? 1 : 0);
			abs = j.value("abs", abs);
			tcs = j.value("tcs", tcs);
			antiFlip = j.value("antiFlip", antiFlip);
			defaultCamera = j.value("defaultCamera", defaultCamera);
			playerName = j.value("playerName", playerName);
			lastJoinAddress = j.value("lastJoinAddress", lastJoinAddress);
			port = j.value("port", port);
			aiCodriverVoice = j.value("aiCodriverVoice", aiCodriverVoice);
			gamepadIndex = j.value("gamepadIndex", gamepadIndex);
			steerDeadzone = j.value("steerDeadzone", steerDeadzone);
			steerLinearity = j.value("steerLinearity", steerLinearity);
			keyboardSteerSpeed = j.value("keyboardSteerSpeed", keyboardSteerSpeed);
			ffbStrength = j.value("ffbStrength", ffbStrength); wheelRotation = j.value("wheelRotation", wheelRotation); ffbInvert = j.value("ffbInvert", ffbInvert); rumble = j.value("rumble", rumble);
			if (j.contains("keys")) for (int i = 0; i < (int)Action::Count && i < (int)j["keys"].size(); ++i) keys[i] = j["keys"][i];
			if (j.contains("keysAlt")) for (int i = 0; i < (int)Action::Count && i < (int)j["keysAlt"].size(); ++i) keysAlt[i] = j["keysAlt"][i];
			if (j.contains("pad")) for (int i = 0; i < (int)Action::Count && i < (int)j["pad"].size(); ++i) pad[i] = j["pad"][i];
			if (j.contains("joy")) for (int i = 0; i < (int)Action::Count && i < (int)j["joy"].size(); ++i) joy[i] = j["joy"][i];
			if (j.contains("steerAxis")) axisFromJson(j["steerAxis"], steerAxis);
			if (j.contains("throttleAxis")) axisFromJson(j["throttleAxis"], throttleAxis);
			if (j.contains("brakeAxis")) axisFromJson(j["brakeAxis"], brakeAxis);
			if (j.contains("clutchAxis")) axisFromJson(j["clutchAxis"], clutchAxis);
		}
		catch (const std::exception& e) {
			log::warn("settings: failed to parse %s: %s", path.c_str(), e.what());
			return false;
		}
		return true;
	}

	bool Settings::save(const std::string& path) const {
		nlohmann::json j;
		j["fullscreen"] = fullscreen; j["vsync"] = vsync;
		j["windowW"] = windowW; j["windowH"] = windowH;
		j["fov"] = fov; j["drawDistance"] = drawDistance;
		j["particleQuality"] = particleQuality; j["showFps"] = showFps; j["msaa"] = msaa;
		j["masterVolume"] = masterVolume; j["engineVolume"] = engineVolume;
		j["effectsVolume"] = effectsVolume; j["voiceVolume"] = voiceVolume; j["musicVolume"] = musicVolume;
		j["useMph"] = useMph; j["manualGearbox"] = manualGearbox; j["abs"] = abs; j["tcs"] = tcs; j["antiFlip"] = antiFlip;
		j["defaultCamera"] = defaultCamera; j["playerName"] = playerName;
		j["lastJoinAddress"] = lastJoinAddress; j["port"] = port; j["aiCodriverVoice"] = aiCodriverVoice;
		j["gamepadIndex"] = gamepadIndex; j["steerDeadzone"] = steerDeadzone;
		j["steerLinearity"] = steerLinearity; j["keyboardSteerSpeed"] = keyboardSteerSpeed;
		j["ffbStrength"] = ffbStrength; j["wheelRotation"] = wheelRotation; j["ffbInvert"] = ffbInvert; j["rumble"] = rumble;
		j["keys"] = keys; j["keysAlt"] = keysAlt; j["pad"] = pad; j["joy"] = joy;
		j["clutchAxis"] = axisToJson(clutchAxis); j["transmission"] = transmission;
		j["steerAxis"] = axisToJson(steerAxis);
		j["throttleAxis"] = axisToJson(throttleAxis);
		j["brakeAxis"] = axisToJson(brakeAxis);
		std::ofstream out(path);
		if (!out) return false;
		out << j.dump(2);
		return true;
	}

	Settings& settings() {
		static Settings s;
		return s;
	}
}
