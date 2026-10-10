#include "core/input.h"
#include <raylib.h>
#include <cmath>
#include <algorithm>

namespace rally::input {
	namespace {
		DriveInput state;
		DriveInput overrideState;
		bool overrideOn = false;
		bool lastPad = false;
		float kbSteer = 0.0f;
		float captureBase[8]{};

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
			return b > 0 && IsGamepadAvailable(s.gamepadIndex) && IsGamepadButtonDown(s.gamepadIndex, b);
		}
		bool padPressed(Action a) {
			const Settings& s = settings();
			int b = s.pad[(int)a];
			return b > 0 && IsGamepadAvailable(s.gamepadIndex) && IsGamepadButtonPressed(s.gamepadIndex, b);
		}

		// Reads a bound axis and maps it to the binding's range:
		// mode 0 -> [-1,1], modes 1/2 -> [0,1].
		float readAxis(const AxisBinding& b) {
			const Settings& s = settings();
			if (b.axis < 0 || !IsGamepadAvailable(s.gamepadIndex)) return 0.0f;
			if (b.axis >= GetGamepadAxisCount(s.gamepadIndex)) return 0.0f;
			float v = GetGamepadAxisMovement(s.gamepadIndex, b.axis);
			if (b.invert) v = -v;
			if (b.mode == 1) v = (v + 1.0f) * 0.5f;
			if (b.mode >= 1) v = std::clamp(v, 0.0f, 1.0f);
			return v;
		}
	}

	void update(float dt) {
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
		// a full-throttle stab.
		static float kbThrottle = 0.0f, kbBrake = 0.0f;
		kbThrottle = keyDown(Action::Throttle) ? std::min(1.0f, kbThrottle + dt * 4.0f) : 0.0f;
		kbBrake = keyDown(Action::Brake) ? std::min(1.0f, kbBrake + dt * 6.0f) : 0.0f;

		float padSteer = readAxis(s.steerAxis);
		if (std::fabs(padSteer) < s.steerDeadzone) padSteer = 0.0f;
		else {
			float sign = padSteer < 0 ? -1.0f : 1.0f;
			float mag = (std::fabs(padSteer) - s.steerDeadzone) / (1.0f - s.steerDeadzone);
			padSteer = sign * std::pow(std::clamp(mag, 0.0f, 1.0f), s.steerLinearity);
		}
		float padThrottle = readAxis(s.throttleAxis);
		float padBrake = readAxis(s.brakeAxis);
		if (padThrottle < 0.03f) padThrottle = 0.0f;
		if (padBrake < 0.03f) padBrake = 0.0f;

		bool anyPad = std::fabs(padSteer) > 0.0f || padThrottle > 0.0f || padBrake > 0.0f;
		bool anyKb = target != 0.0f || kbThrottle > 0.0f || kbBrake > 0.0f;
		if (anyPad) lastPad = true;
		if (anyKb) lastPad = false;

		state.steer = std::clamp(kbSteer + padSteer, -1.0f, 1.0f);
		state.throttle = std::max(kbThrottle, padThrottle);
		state.brake = std::max(kbBrake, padBrake);
		state.handbrake = keyDown(Action::Handbrake) || padDown(Action::Handbrake);
		state.shiftUp = keyPressed(Action::ShiftUp) || padPressed(Action::ShiftUp);
		state.shiftDown = keyPressed(Action::ShiftDown) || padPressed(Action::ShiftDown);
	}

	const DriveInput& drive() { return state; }

	bool pressed(Action a) { return keyPressed(a) || padPressed(a); }
	bool down(Action a) { return keyDown(a) || padDown(a); }
	bool usingGamepad() { return lastPad; }

	int pollAnyKey() { return GetKeyPressed(); }

	int pollAnyPadButton() {
		int idx = settings().gamepadIndex;
		if (!IsGamepadAvailable(idx)) return 0;
		for (int b = 1; b <= GAMEPAD_BUTTON_RIGHT_THUMB; ++b)
			if (IsGamepadButtonPressed(idx, b)) return b;
		return 0;
	}

	void beginAxisCapture() {
		int idx = settings().gamepadIndex;
		for (int a = 0; a < 8; ++a)
			captureBase[a] = (IsGamepadAvailable(idx) && a < GetGamepadAxisCount(idx)) ? GetGamepadAxisMovement(idx, a) : 0.0f;
	}

	bool pollAxisCapture(AxisBinding& out) {
		int idx = settings().gamepadIndex;
		if (!IsGamepadAvailable(idx)) return false;
		int count = std::min(8, GetGamepadAxisCount(idx));
		int best = -1; float bestDelta = 0.5f;
		for (int a = 0; a < count; ++a) {
			float d = std::fabs(GetGamepadAxisMovement(idx, a) - captureBase[a]);
			if (d > bestDelta) { bestDelta = d; best = a; }
		}
		if (best < 0) return false;
		float base = captureBase[best];
		float now = GetGamepadAxisMovement(idx, best);
		out.axis = best;
		if (base < -0.8f) { out.mode = 1; out.invert = false; }      // trigger resting at -1
		else if (base > 0.8f) { out.mode = 1; out.invert = true; }   // pedal resting at +1
		else {
			// Centered: steering if it was centered, or a 0..1 pedal.
			out.mode = 0;
			out.invert = false;
			(void)now;
		}
		return true;
	}

	void setOverride(bool enabled, const DriveInput& in) {
		overrideOn = enabled;
		overrideState = in;
	}
}
