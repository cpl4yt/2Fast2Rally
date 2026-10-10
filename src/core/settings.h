#pragma once
#include <string>
#include <array>

namespace rally {
	// Bindable digital actions.
	enum class Action : int {
		Throttle, Brake, SteerLeft, SteerRight, Handbrake,
		ShiftUp, ShiftDown, Reset, Camera, Pause,
		CallNote, NavView, LookBack, Debug,
		Clutch, Gear1, Gear2, Gear3, Gear4, Gear5, Gear6, GearReverse, GearNeutral, Ignition,
		Count
	};
	const char* actionName(Action a);

	// One analog axis binding (gamepad / wheel).
	struct AxisBinding {
		int axis = -1;        // axis index, -1 = unbound
		int device = -1;      // -1 = raylib gamepad, 0..3 = DirectInput device (wheel, pedals...)
		bool invert = false;
		// 0 = centered stick (-1..1), 1 = pedal/trigger that rests at -1,
		// 2 = pedal that rests at 0 (0..1)
		int mode = 0;
	};

	struct Settings {
		// --- Graphics ---
		bool fullscreen = false;
		bool vsync = true;
		int windowW = 1600, windowH = 900;
		float fov = 70.0f;
		float drawDistance = 450.0f;
		int particleQuality = 2;  // 0 off, 1 low, 2 high
		bool showFps = false;
		bool msaa = true;         // 4x MSAA

		// --- Audio (0..1) ---
		float masterVolume = 0.8f;
		float engineVolume = 0.8f;
		float effectsVolume = 0.8f;
		float voiceVolume = 1.0f;
		float musicVolume = 0.4f;

		// --- Gameplay ---
		bool useMph = false;
		bool manualGearbox = false;   // legacy, mirrors transmission != 0
		int transmission = 0;         // 0 automatic, 1 sequential, 2 H-pattern + clutch
		bool abs = true;
		bool tcs = true;
		bool antiFlip = true;
		int defaultCamera = 3;        // cockpit: it's a first-person rally game
		std::string playerName = "Player";
		std::string lastJoinAddress = "127.0.0.1";
		int port = 27420;
		bool aiCodriverVoice = true;  // navigator: voice plays when notes are called

		// --- Controls ---
		std::array<int, (int)Action::Count> keys{};     // raylib KeyboardKey
		std::array<int, (int)Action::Count> keysAlt{};  // secondary key
		std::array<int, (int)Action::Count> pad{};      // raylib GamepadButton, 0 = unbound
		std::array<int, (int)Action::Count> joy{};      // DirectInput button: (device+1)*1000 + button, 0 = unbound
		AxisBinding steerAxis{ 0, false, 0 };
		AxisBinding throttleAxis{ 5, false, 1 };
		AxisBinding brakeAxis{ 4, false, 1 };
		AxisBinding clutchAxis{ -1, false, 1 };
		int gamepadIndex = 0;
		float steerDeadzone = 0.06f;
		float steerLinearity = 1.3f;   // >1 = softer around center
		float keyboardSteerSpeed = 3.2f;
		float wheelRotation = 900.0f;  // degrees lock-to-lock of the physical wheel (in-game wheel matches 1:1)
		float ffbStrength = 0.7f;      // steering wheel force feedback
		bool ffbInvert = false;
		bool rumble = true;            // gamepad vibration

		Settings();
		void resetControls();
		bool load(const std::string& path);
		bool save(const std::string& path) const;
	};

	Settings& settings();
	extern const char* kSettingsPath;
}
