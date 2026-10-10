#pragma once
#include "core/settings.h"

namespace rally {
	// Per-frame driving controls after bindings, deadzones and keyboard
	// smoothing have been applied.
	struct DriveInput {
		float steer = 0.0f;     // -1 left .. +1 right
		float throttle = 0.0f;  // 0..1
		float brake = 0.0f;     // 0..1
		float clutch = 0.0f;    // 0..1 pedal (1 = pressed = disengaged)
		bool handbrake = false;
		bool shiftUp = false, shiftDown = false; // edge-triggered (sequential)
		// H-pattern request: -2 = no request, -1 = R, 0 = N, 1..6.
		// A physical shifter (gears bound to joystick buttons) reports its
		// gate every frame (0 when between gates); keys only on press.
		int hGear = -2;
		bool ignition = false;  // edge
	};

	namespace input {
		// Call once per frame before reading anything.
		void update(float dt);
		const DriveInput& drive();

		bool pressed(Action a);   // edge: went down this frame (key, pad or joystick)
		bool down(Action a);
		bool usingGamepad();      // last input came from a gamepad/wheel
		bool shifterHeld();       // gears are bound to a physical H-shifter (reported every frame)

		// Rebinding helpers for the controls menu.
		int pollAnyKey();                              // keyboard key pressed this frame, 0 if none
		int pollAnyPadButton();                        // raylib pad button pressed this frame, 0 if none
		int pollAnyJoyButton();                        // DirectInput button: (device+1)*1000+button, 0 if none
		void beginAxisCapture();                       // snapshot current axis values
		bool pollAxisCapture(AxisBinding& out);        // true once an axis moved enough
		const char* joyButtonName(int encoded);        // "Wheel 1 btn 5"

		// Synthetic input for automated tests / AI driving.
		void setOverride(bool enabled, const DriveInput& in = {});
	}
}
