#include "core/input.h"
#include "core/joystick.h"
#include <raylib.h>
#include <cmath>
#include <cstdio>
#include <algorithm>

namespace rally::input {
	namespace {
		DriveInput state;
		DriveInput overrideState;
		bool overrideOn = false;
		bool lastPad = false;
		float kbSteer = 0.0f, kbThrottle = 0.0f, kbBrake = 0.0f, kbClutch = 0.0f;
		float captureBase[8]{};
		float captureJoy[joy::kMaxDevices][joy::kAxes]{};
		bool prevJoy[joy::kMaxDevices][joy::kButtons]{};
		bool curJoy[joy::kMaxDevices][joy::kButtons]{};

		bool joyDownEncoded(int enc) {
			if (enc <= 0) return false;
			int dev = enc / 1000 - 1, btn = enc % 1000;
			if (dev < 0 || dev >= joy::kMaxDevices || btn >= joy::kButtons) return false;
			return curJoy[dev][btn];
		}
		bool joyPressedEncoded(int enc) {
			if (enc <= 0) return false;
			int dev = enc / 1000 - 1, btn = enc % 1000;
			if (dev < 0 || dev >= joy::kMaxDevices || btn >= joy::kButtons) return false;
			return curJoy[dev][btn] && !prevJoy[dev][btn];
		}

		bool keyDown(Action a) {
			const Settings& s = settings();
			int k = s.keys[(int)a], k2 = s.keysAlt[(int)a];
			return (k > 0 && IsKeyDown(k)) || (k2 > 0 && IsKeyDown(k2));
		}
		bool keyPressed(Action a) {
			const Settings& s = settings();
			int k = s.keys[(int)a], k2 = s.keysAlt[(int)a];
			return (k > 0 && IsKeyPressed(k)) || (k2 > 0 && IsKeyPressed(k2));
		}
		bool padDown(Action a) {
			const Settings& s = settings();
			int b = s.pad[(int)a];
			bool p = b > 0 && IsGamepadAvailable(s.gamepadIndex) && IsGamepadButtonDown(s.gamepadIndex, b);
			return p || joyDownEncoded(s.joy[(int)a]);
		}
		bool padPressed(Action a) {
			const Settings& s = settings();
			int b = s.pad[(int)a];
			bool p = b > 0 && IsGamepadAvailable(s.gamepadIndex) && IsGamepadButtonPressed(s.gamepadIndex, b);
			return p || joyPressedEncoded(s.joy[(int)a]);
		}

		// Reads a bound axis and maps it to the binding's range:
		// mode 0 -> [-1,1], modes 1/2 -> [0,1].
		float readAxis(const AxisBinding& b) {
			const Settings& s = settings();
			if (b.axis < 0) return 0.0f;
			float v;
			if (b.device >= 0) {
				const joy::State& js = joy::state(b.device);
				if (!js.connected || b.axis >= joy::kAxes) return 0.0f;
				v = js.axis[b.axis];
			}
			else {
				if (!IsGamepadAvailable(s.gamepadIndex) || b.axis >= GetGamepadAxisCount(s.gamepadIndex)) return 0.0f;
				v = GetGamepadAxisMovement(s.gamepadIndex, b.axis);
			}
			if (b.invert) v = -v;
			if (b.mode == 1) v = (v + 1.0f) * 0.5f;
			if (b.mode >= 1) v = std::clamp(v, 0.0f, 1.0f);
			return v;
		}

		// Is any gear bound to a joystick/pad button (i.e. a real H-shifter)?
		bool shifterBound() {
			const Settings& s = settings();
			for (int a = (int)Action::Gear1; a <= (int)Action::GearReverse; ++a)
				if (s.joy[a] > 0 || s.pad[a] > 0) return true;
			return false;
		}
		bool gearHeld(Action a) {
			const Settings& s = settings();
			int b = s.pad[(int)a];
			bool p = b > 0 && IsGamepadAvailable(s.gamepadIndex) && IsGamepadButtonDown(s.gamepadIndex, b);
			return p || joyDownEncoded(s.joy[(int)a]);
		}
	}

	void update(float dt) {
		joy::poll();
		for (int d = 0; d < joy::kMaxDevices; ++d) {
			const joy::State& js = joy::state(d);
			for (int b = 0; b < joy::kButtons; ++b) { prevJoy[d][b] = curJoy[d][b]; curJoy[d][b] = js.connected && js.button[b]; }
		}
		if (overrideOn) { state = overrideState; return; }
		const Settings& s = settings();

		// Keyboard: steering ramps toward the target instead of snapping,
		// and returns to center faster than it leaves it.
		float target = 0.0f;
		if (keyDown(Action::SteerLeft)) target -= 1.0f;
		if (keyDown(Action::SteerRight)) target += 1.0f;
		float rate = s.keyboardSteerSpeed * ((target == 0.0f || target * kbSteer < 0.0f) ? 1.6f : 1.0f);
		if (kbSteer < target) kbSteer = std::min(target, kbSteer + rate * dt);
		else if (kbSteer > target) kbSteer = std::max(target, kbSteer - rate * dt);

		// Keys ramp the pedals in (and drop them instantly) so a tap isn't
		// a full stab. The clutch key comes up slowly so launches are possible.
		kbThrottle = keyDown(Action::Throttle) ? std::min(1.0f, kbThrottle + dt * 4.0f) : 0.0f;
		kbBrake = keyDown(Action::Brake) ? std::min(1.0f, kbBrake + dt * 6.0f) : 0.0f;
		kbClutch = keyDown(Action::Clutch) ? std::min(1.0f, kbClutch + dt * 10.0f) : std::max(0.0f, kbClutch - dt * 2.5f);

		float padSteer = readAxis(s.steerAxis);
		// A DirectInput steering axis is a real wheel: keep it linear and raw.
		bool wheel = s.steerAxis.device >= 0 && s.steerAxis.axis >= 0 && joy::state(s.steerAxis.device).connected;
		if (wheel) { /* 1:1 */ }
		else if (std::fabs(padSteer) < s.steerDeadzone) padSteer = 0.0f;
		else {
			float sign = padSteer < 0 ? -1.0f : 1.0f;
			float mag = (std::fabs(padSteer) - s.steerDeadzone) / (1.0f - s.steerDeadzone);
			padSteer = sign * std::pow(std::clamp(mag, 0.0f, 1.0f), s.steerLinearity);
		}
		float padThrottle = readAxis(s.throttleAxis);
		float padBrake = readAxis(s.brakeAxis);
		float padClutch = readAxis(s.clutchAxis);
		if (padThrottle < 0.03f) padThrottle = 0.0f;
		if (padBrake < 0.03f) padBrake = 0.0f;
		if (padClutch < 0.03f) padClutch = 0.0f;

		bool anyPad = std::fabs(padSteer) > 0.0f || padThrottle > 0.0f || padBrake > 0.0f;
		bool anyKb = target != 0.0f || kbThrottle > 0.0f || kbBrake > 0.0f;
		if (anyPad) lastPad = true;
		if (anyKb) lastPad = false;

		state.steer = std::clamp(kbSteer + padSteer, -1.0f, 1.0f);
		state.steerIsWheel = wheel && target == 0.0f;
		state.throttle = std::max(kbThrottle, padThrottle);
		state.brake = std::max(kbBrake, padBrake);
		state.clutch = std::max(kbClutch, padClutch);
		state.handbrake = keyDown(Action::Handbrake) || padDown(Action::Handbrake);
		state.shiftUp = keyPressed(Action::ShiftUp) || padPressed(Action::ShiftUp);
		state.shiftDown = keyPressed(Action::ShiftDown) || padPressed(Action::ShiftDown);
		state.ignition = keyPressed(Action::Ignition) || padPressed(Action::Ignition);

		// H-pattern.
		state.hGear = -2;
		static const Action gears[] = { Action::Gear1, Action::Gear2, Action::Gear3, Action::Gear4, Action::Gear5, Action::Gear6 };
		if (shifterBound()) {
			// Physical shifter: the held gate is the gear, nothing held = neutral.
			int g = 0;
			for (int i = 0; i < 6; ++i) if (gearHeld(gears[i])) g = i + 1;
			if (gearHeld(Action::GearReverse)) g = -1;
			state.hGear = g;
		}
		for (int i = 0; i < 6; ++i) if (keyPressed(gears[i])) state.hGear = i + 1;
		if (keyPressed(Action::GearReverse)) state.hGear = -1;
		if (keyPressed(Action::GearNeutral) || padPressed(Action::GearNeutral)) state.hGear = 0;
	}

	const DriveInput& drive() { return state; }

	bool pressed(Action a) { return keyPressed(a) || padPressed(a); }
	bool down(Action a) { return keyDown(a) || padDown(a); }
	bool usingGamepad() { return lastPad; }
	bool shifterHeld() { return shifterBound(); }

	int pollAnyKey() { return GetKeyPressed(); }

	int pollAnyPadButton() {
		int idx = settings().gamepadIndex;
		if (!IsGamepadAvailable(idx)) return 0;
		for (int b = 1; b <= GAMEPAD_BUTTON_RIGHT_THUMB; ++b)
			if (IsGamepadButtonPressed(idx, b)) return b;
		return 0;
	}

	int pollAnyJoyButton() {
		for (int d = 0; d < joy::kMaxDevices; ++d)
			for (int b = 0; b < joy::kButtons; ++b)
				if (curJoy[d][b] && !prevJoy[d][b]) return (d + 1) * 1000 + b;
		return 0;
	}

	const char* joyButtonName(int enc) {
		static char buf[32];
		if (enc <= 0) return "-";
		std::snprintf(buf, sizeof buf, "Dev%d btn %d", enc / 1000, enc % 1000 + 1);
		return buf;
	}

	void beginAxisCapture() {
		int idx = settings().gamepadIndex;
		for (int a = 0; a < 8; ++a)
			captureBase[a] = (IsGamepadAvailable(idx) && a < GetGamepadAxisCount(idx)) ? GetGamepadAxisMovement(idx, a) : 0.0f;
		for (int d = 0; d < joy::kMaxDevices; ++d)
			for (int a = 0; a < joy::kAxes; ++a) captureJoy[d][a] = joy::state(d).axis[a];
	}

	bool pollAxisCapture(AxisBinding& out) {
		int idx = settings().gamepadIndex;
		int bestDev = -2, best = -1;
		float bestDelta = 0.5f, base = 0.0f;
		if (IsGamepadAvailable(idx)) {
			int count = std::min(8, GetGamepadAxisCount(idx));
			for (int a = 0; a < count; ++a) {
				float d = std::fabs(GetGamepadAxisMovement(idx, a) - captureBase[a]);
				if (d > bestDelta) { bestDelta = d; best = a; bestDev = -1; base = captureBase[a]; }
			}
		}
		for (int dv = 0; dv < joy::deviceCount() && dv < joy::kMaxDevices; ++dv) {
			const joy::State& js = joy::state(dv);
			if (!js.connected) continue;
			for (int a = 0; a < joy::kAxes; ++a) {
				float d = std::fabs(js.axis[a] - captureJoy[dv][a]);
				if (d > bestDelta) { bestDelta = d; best = a; bestDev = dv; base = captureJoy[dv][a]; }
			}
		}
		if (best < 0) return false;
		out.axis = best;
		out.device = bestDev;
		if (base < -0.8f) { out.mode = 1; out.invert = false; }      // pedal/trigger resting at -1
		else if (base > 0.8f) { out.mode = 1; out.invert = true; }   // pedal resting at +1
		else { out.mode = 0; out.invert = false; }                   // centred (steering)
		return true;
	}

	void setOverride(bool enabled, const DriveInput& in) {
		overrideOn = enabled;
		overrideState = in;
	}
}
