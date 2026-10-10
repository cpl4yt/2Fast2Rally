#pragma once

namespace rally::ffb {
	// Force feedback for steering wheels (DirectInput) and rumble for
	// XInput gamepads. Own translation unit: the Windows/DirectInput headers
	// clash with raylib. No-ops on other platforms or without a device.
	bool init(void* nativeWindowHandle);
	void shutdown();
	bool wheelAvailable();
	const char* wheelName();

	// constant: -1..1 (+ = push the wheel to the right)
	// spring:   0..1 centering stiffness, damper: 0..1
	void setWheel(float constant, float spring, float damper);
	// Gamepad rumble, 0..1 per motor (low = heavy, high = buzzy).
	void setRumble(float low, float high);
	// Release all forces (menus, pause).
	void stopAll();
}
