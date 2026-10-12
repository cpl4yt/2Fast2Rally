#include "core/ui.h"
#include <raylib.h>
#include <cmath>
#include <cstdio>
#include <algorithm>

namespace rally::ui {
	namespace {
		Font bodyFont{};
		Font headFont{};
		bool ownBody = false, ownHead = false;

		int widgetCount = 0;      // widgets declared this frame
		int lastWidgetCount = 0;  // from last frame (focus wraps over this)
		int focus = 0;
		bool activate = false;    // enter/A pressed this frame
		int horizontal = 0;       // -1/+1 left/right nav this frame
		bool mouseMoved = false;
		bool editing = false;     // a textField is capturing keys
		int editingIndex = -1;
		bool backPressed = false;
		std::function<void(SoundEvent)> soundHook;
		void sound(SoundEvent e) { if (soundHook) soundHook(e); }

		bool padPressed(int b) { return IsGamepadAvailable(0) && IsGamepadButtonPressed(0, b); }

		// Focus bookkeeping: returns this widget's index and whether it has focus.
		bool nextWidget(Rectangle r, int& index) {
			index = widgetCount++;
			Vector2 m = GetMousePosition();
			if (mouseMoved && CheckCollisionPointRec(m, r) && focus != index) { focus = index; sound(SoundEvent::Move); }
			return focus == index;
		}

		void drawFrame(Rectangle r, bool focused, bool enabled) {
			Color bg = focused ? Color{ 52, 46, 30, 240 } : Color{ 38, 42, 50, 230 };
			if (!enabled) bg = Color{ 30, 32, 36, 200 };
			DrawRectangleRec(r, bg);
			if (focused && enabled) {
				DrawRectangle((int)r.x, (int)r.y, (int)(5 * s()), (int)r.height, kAccent);
				DrawRectangleLinesEx(r, 1.5f, kAccent);
			}
		}

		Font loadFirst(const char* const* paths, int n, int size, bool& owned) {
			for (int i = 0; i < n; ++i) {
				if (FileExists(paths[i])) {
					Font f = LoadFontEx(paths[i], size, nullptr, 0);
					if (f.texture.id != 0) {
						SetTextureFilter(f.texture, TEXTURE_FILTER_BILINEAR);
						owned = true;
						return f;
					}
				}
			}
			owned = false;
			return GetFontDefault();
		}
	}

	void init() {
		// Bahnschrift ships with Windows 10+ and has the right motorsport look;
		// it is loaded from the system at runtime, never bundled.
		const char* body[] = {
			"C:/Windows/Fonts/bahnschrift.ttf",
			"C:/Windows/Fonts/segoeuib.ttf",
			"/usr/share/fonts/TTF/DejaVuSans-Bold.ttf",
			"/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
		};
		bodyFont = loadFirst(body, 4, 64, ownBody);
		const char* head[] = { "res/fonts/Cherry_Bomb_One/CherryBombOne-Regular.ttf" };
		headFont = loadFirst(head, 1, 128, ownHead);
		if (!ownHead) headFont = bodyFont;
	}

	void shutdown() {
		if (ownBody) UnloadFont(bodyFont);
		if (ownHead) UnloadFont(headFont);
	}

	float s() { return GetScreenHeight() / 1080.0f; }
	const Font& font() { return bodyFont; }
	const Font& titleFont() { return headFont; }

	void beginFrame() {
		lastWidgetCount = widgetCount;
		widgetCount = 0;
		Vector2 d = GetMouseDelta();
		mouseMoved = (d.x != 0 || d.y != 0) || IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
		activate = false;
		horizontal = 0;
		backPressed = false;

		if (editing) {
			if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_TAB)) editing = false;
			return;
		}

		int n = std::max(1, lastWidgetCount);
		if (IsKeyPressed(KEY_DOWN) || padPressed(GAMEPAD_BUTTON_LEFT_FACE_DOWN)) { focus = (focus + 1) % n; sound(SoundEvent::Move); }
		if (IsKeyPressed(KEY_UP) || padPressed(GAMEPAD_BUTTON_LEFT_FACE_UP)) { focus = (focus - 1 + n) % n; sound(SoundEvent::Move); }
		if (IsKeyPressed(KEY_LEFT) || padPressed(GAMEPAD_BUTTON_LEFT_FACE_LEFT)) horizontal = -1;
		if (IsKeyPressed(KEY_RIGHT) || padPressed(GAMEPAD_BUTTON_LEFT_FACE_RIGHT)) horizontal = 1;
		if (IsKeyPressed(KEY_ENTER) || padPressed(GAMEPAD_BUTTON_RIGHT_FACE_DOWN)) activate = true;
		if (IsKeyPressed(KEY_ESCAPE) || padPressed(GAMEPAD_BUTTON_RIGHT_FACE_RIGHT)) { backPressed = true; sound(SoundEvent::Back); }
		if (focus >= n) focus = 0;
	}

	void resetFocus() { focus = 0; editing = false; }
	void setSoundHook(std::function<void(SoundEvent)> fn) { soundHook = std::move(fn); }

	bool back() { return backPressed && !editing; }
	bool typing() { return editing; }

	Vector2 measure(const std::string& str, float size, bool title) {
		const Font& f = title ? headFont : bodyFont;
		return MeasureTextEx(f, str.c_str(), size, size * 0.02f);
	}

	void text(const std::string& str, float x, float y, float size, Color c, Align a, bool title) {
		const Font& f = title ? headFont : bodyFont;
		Vector2 sz = MeasureTextEx(f, str.c_str(), size, size * 0.02f);
		if (a == Center) x -= sz.x * 0.5f;
		else if (a == Right) x -= sz.x;
		DrawTextEx(f, str.c_str(), { std::round(x), std::round(y) }, size, size * 0.02f, c);
	}

	void panel(Rectangle r, Color c) {
		DrawRectangleRec(r, c);
		DrawRectangle((int)r.x, (int)r.y, (int)r.width, (int)(3 * s()), kAccent);
	}

	bool button(Rectangle r, const std::string& label, bool enabled) {
		int idx;
		bool f = nextWidget(r, idx);
		drawFrame(r, f, enabled);
		float fs = r.height * 0.5f;
		text(label, r.x + r.width * 0.5f, r.y + (r.height - fs) * 0.5f, fs, enabled ? kText : kTextDim, Center);
		if (!enabled) return false;
		bool clicked = CheckCollisionPointRec(GetMousePosition(), r) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
		if (clicked || (f && activate)) { sound(SoundEvent::Select); return true; }
		return false;
	}

	bool toggle(Rectangle r, const std::string& label, bool& v) {
		int idx;
		bool f = nextWidget(r, idx);
		drawFrame(r, f, true);
		float fs = r.height * 0.45f;
		text(label, r.x + 16 * s(), r.y + (r.height - fs) * 0.5f, fs, kText);
		text(v ? "ON" : "OFF", r.x + r.width - 16 * s(), r.y + (r.height - fs) * 0.5f, fs, v ? kAccent : kTextDim, Right);
		bool clicked = CheckCollisionPointRec(GetMousePosition(), r) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
		if (clicked || (f && (activate || horizontal != 0))) { v = !v; sound(SoundEvent::Select); return true; }
		return false;
	}

	bool slider(Rectangle r, const std::string& label, float& v, float lo, float hi, const char* fmt, float displayScale) {
		int idx;
		bool f = nextWidget(r, idx);
		drawFrame(r, f, true);
		float fs = r.height * 0.45f;
		text(label, r.x + 16 * s(), r.y + (r.height - fs) * 0.5f, fs, kText);
		Rectangle bar = { r.x + r.width * 0.5f, r.y + r.height * 0.42f, r.width * 0.32f, r.height * 0.16f };
		DrawRectangleRec(bar, Color{ 60, 64, 72, 255 });
		float t = (v - lo) / (hi - lo);
		DrawRectangleRec({ bar.x, bar.y, bar.width * t, bar.height }, kAccent);
		char buf[32];
		std::snprintf(buf, sizeof buf, fmt, v * displayScale);
		text(buf, r.x + r.width - 16 * s(), r.y + (r.height - fs) * 0.5f, fs, kText, Right);

		bool changed = false;
		Vector2 m = GetMousePosition();
		Rectangle hit = { bar.x - 6, r.y, bar.width + 12, r.height };
		if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(m, hit)) {
			float nt = std::clamp((m.x - bar.x) / bar.width, 0.0f, 1.0f);
			v = lo + nt * (hi - lo);
			changed = true;
		}
		if (f && horizontal != 0) {
			v = std::clamp(v + horizontal * (hi - lo) * 0.05f, lo, hi);
			sound(SoundEvent::Move);
			changed = true;
		}
		return changed;
	}

	bool choice(Rectangle r, const std::string& label, int& idx, const std::vector<std::string>& opts) {
		int wi;
		bool f = nextWidget(r, wi);
		drawFrame(r, f, true);
		float fs = r.height * 0.45f;
		text(label, r.x + 16 * s(), r.y + (r.height - fs) * 0.5f, fs, kText);
		if (opts.empty()) return false;
		idx = std::clamp(idx, 0, (int)opts.size() - 1);
		std::string shown = "< " + opts[idx] + " >";
		text(shown, r.x + r.width - 16 * s(), r.y + (r.height - fs) * 0.5f, fs, kAccent, Right);
		bool clicked = CheckCollisionPointRec(GetMousePosition(), r) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
		int dir = 0;
		if (clicked) dir = (GetMousePosition().x < r.x + r.width * 0.75f) ? -1 : 1;
		if (f && horizontal != 0) dir = horizontal;
		if (f && activate) dir = 1;
		if (dir != 0) {
			idx = (idx + dir + (int)opts.size()) % (int)opts.size();
			sound(SoundEvent::Select);
			return true;
		}
		return false;
	}

	bool textField(Rectangle r, const std::string& label, std::string& value, int maxLen) {
		int idx;
		bool f = nextWidget(r, idx);
		drawFrame(r, f, true);
		float fs = r.height * 0.45f;
		text(label, r.x + 16 * s(), r.y + (r.height - fs) * 0.5f, fs, kTextDim);
		bool active = editing && editingIndex == idx;
		bool clicked = CheckCollisionPointRec(GetMousePosition(), r) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
		if (!active && (clicked || (f && activate))) {
			editing = true; editingIndex = idx; active = true;
		}
		bool changed = false;
		if (active) {
			if (!f) { editing = false; active = false; }
			int c;
			while ((c = GetCharPressed()) > 0) {
				if (c >= 32 && c < 127 && (int)value.size() < maxLen) { value.push_back((char)c); changed = true; }
			}
			if ((IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) && !value.empty()) { value.pop_back(); changed = true; }
		}
		std::string shown = value;
		if (active && std::fmod(GetTime(), 1.0) < 0.5) shown += "_";
		text(shown, r.x + r.width - 16 * s(), r.y + (r.height - fs) * 0.5f, fs, active ? kAccent : kText, Right);
		return changed;
	}

	std::string formatTime(double t, bool sign) {
		char buf[32];
		const char* pre = "";
		if (sign) pre = t < 0 ? "-" : "+";
		double a = std::fabs(t);
		int m = (int)(a / 60.0);
		double sec = a - m * 60.0;
		if (m > 0) std::snprintf(buf, sizeof buf, "%s%d:%06.3f", pre, m, sec);
		else std::snprintf(buf, sizeof buf, "%s%.3f", pre, sec);
		return buf;
	}
}
