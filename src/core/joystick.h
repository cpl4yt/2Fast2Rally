#pragma once

namespace rally::joy {
	// Raw DirectInput game controllers (wheels, pedal sets, H-shifters,
	// button boxes) - raylib/GLFW only sees devices with a gamepad mapping,
	// which most wheels and shifters don't have. Implemented in
	// forceFeedback.cpp so both share one DirectInput instance.
	constexpr int kMaxDevices = 4;
	constexpr int kAxes = 8;     // X, Y, Z, Rx, Ry, Rz, Slider0, Slider1
	constexpr int kButtons = 64;

	struct State {
		bool connected = false;
		float axis[kAxes]{};        // -1..1
		bool button[kButtons]{};
	};

	int deviceCount();
	const char* deviceName(int device);
	const State& state(int device);
	void poll();                    // once per frame
}
