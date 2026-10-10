#pragma once
#include "core/settings.h"

namespace rally {
	// Per-frame driving controls after bindings, deadzones and keyboard
	// smoothing have been applied.
	struct DriveInput {
		float steer = 0.0f;     // -1 left .. +1 right
		float throttle = 0.0f;  // 0..1
		float brake = 0.0f;     // 0..1
		bool handbrake = false;
		bool shiftUp = false, shiftDown = false; // edge-triggered
	};

	namespace input {
		// Call once per frame before reading anything.
		void update(float dt);
		const DriveInput& drive();

		bool pressed(Action a);   // edge: went down this frame (key or pad)
		bool down(Action a);
		bool usingGamepad();      // last input came from a gamepad/wheel

		// Rebinding helpers for the controls menu.
		int pollAnyKey();                              // keyboard key pressed this frame, 0 if none
		int pollAnyPadButton();                        // pad button pressed this frame, 0 if none
		void beginAxisCapture();                       // snapshot current axis values
		bool pollAxisCapture(AxisBinding& out);        // true once an axis moved enough

		// Synthetic input for automated tests / AI driving.
		void setOverride(bool enabled, const DriveInput& in = {});
	}
}
