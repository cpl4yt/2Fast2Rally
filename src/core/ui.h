#pragma once
#include <raylib.h>
#include <string>
#include <vector>

namespace rally::ui {
	// Palette.
	inline const Color kBg = { 18, 20, 24, 235 };
	inline const Color kPanel = { 28, 31, 38, 230 };
	inline const Color kAccent = { 255, 176, 0, 255 };
	inline const Color kAccentDim = { 160, 110, 0, 255 };
	inline const Color kText = { 236, 236, 240, 255 };
	inline const Color kTextDim = { 150, 155, 165, 255 };
	inline const Color kGood = { 90, 210, 120, 255 };
	inline const Color kBad = { 235, 80, 70, 255 };

	enum Align { Left, Center, Right };

	void init();
	void shutdown();

	// Call at the start of each frame's UI pass. Handles keyboard/gamepad
	// focus movement between the widgets drawn last frame.
	void beginFrame();
	// Reset focus to the first widget (call when a screen opens).
	void resetFocus();

	float s();                    // UI scale (screen height / 1080)
	const Font& font();
	const Font& titleFont();

	void text(const std::string& str, float x, float y, float size, Color c, Align a = Left, bool title = false);
	Vector2 measure(const std::string& str, float size, bool title = false);
	void panel(Rectangle r, Color c = kPanel);

	// Widgets return true when activated / value changed.
	bool button(Rectangle r, const std::string& label, bool enabled = true);
	bool toggle(Rectangle r, const std::string& label, bool& v);
	bool slider(Rectangle r, const std::string& label, float& v, float lo, float hi, const char* fmt = "%.0f%%", float displayScale = 100.0f);
	bool choice(Rectangle r, const std::string& label, int& idx, const std::vector<std::string>& opts);
	bool textField(Rectangle r, const std::string& label, std::string& value, int maxLen = 32);

	// True if the "back" input (Esc / gamepad B) was pressed this frame and
	// no text field is capturing it.
	bool back();
	// True while a text field has keyboard focus (gameplay keys should be ignored).
	bool typing();

	// Small helpers.
	std::string formatTime(double seconds, bool sign = false);
}
