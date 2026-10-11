#include "game/game.h"
#include "core/input.h"
#include "core/ui.h"
#include <raymath.h>
#include <cmath>
#include <algorithm>
#include <cstdio>

namespace rally {
	namespace {
		Color fade(Color c, float a) { c.a = (unsigned char)(c.a * std::clamp(a, 0.0f, 1.0f)); return c; }

		Color severityColor(const PaceNote& n) {
			if (n.type != NoteType::Corner) return { 120, 180, 255, 255 };
			if (n.severity <= 1) return { 235, 70, 60, 255 };
			if (n.severity <= 3) return { 255, 176, 0, 255 };
			return { 90, 210, 120, 255 };
		}

		// Curved arrow: sweep angle grows as the corner gets tighter.
		void drawCornerArrow(Vector2 c, float size, int dir, int severity, Color col) {
			dir = -dir; // the arc below bends toward -dir; flip so it bends toward the corner
			float sweep = severity == 0 ? 170.0f : 25.0f + (6 - severity) * 22.0f;
			float r = size * 0.42f;
			float startDeg = 90.0f;  // pointing up (screen), start at bottom of arc
			int segs = 18;
			Vector2 prev{};
			Vector2 center = { c.x - dir * r, c.y + size * 0.1f };
			Vector2 last{}, before{};
			for (int i = 0; i <= segs; ++i) {
				float a = (startDeg - 90.0f + (float)i / segs * sweep) * DEG2RAD;
				Vector2 p = { center.x + dir * std::cos(a) * r, center.y - std::sin(a) * r };
				if (i == 0) { Vector2 tail = { p.x, p.y + size * 0.3f }; DrawLineEx(tail, p, size * 0.09f, col); }
				if (i > 0) DrawLineEx(prev, p, size * 0.09f, col);
				before = prev;
				prev = p;
				last = p;
			}
			Vector2 d = Vector2Normalize(Vector2Subtract(last, before));
			Vector2 n = { -d.y, d.x };
			float h = size * 0.18f;
			Vector2 tip = Vector2Add(last, Vector2Scale(d, h));
			Vector2 b1 = Vector2Add(last, Vector2Scale(n, h * 0.8f));
			Vector2 b2 = Vector2Subtract(last, Vector2Scale(n, h * 0.8f));
			DrawTriangle(tip, b1, b2, col);
			DrawTriangle(tip, b2, b1, col);
		}
	}

	void Game::drawNoteIcon(const PaceNote& n, Rectangle r, float alpha, bool highlight) {
		float s = r.height;
		DrawRectangleRounded(r, 0.18f, 6, fade(Color{ 20, 22, 26, 215 }, alpha));
		Color col = fade(severityColor(n), alpha);
		if (highlight) DrawRectangleRoundedLinesEx(r, 0.18f, 6, 3.0f, fade(ui::kAccent, alpha));
		std::string big;
		switch (n.type) {
		case NoteType::Corner: {
			drawCornerArrow({ r.x + r.width * 0.32f, r.y + r.height * 0.5f }, s * 0.7f, n.direction, n.severity, col);
			big = n.severity == 0 ? "HP" : (n.mods & MOD_SQUARE) ? "SQ" : std::to_string(n.severity);
			break;
		}
		case NoteType::Crest: {
			Vector2 a = { r.x + r.width * 0.12f, r.y + s * 0.65f }, b = { r.x + r.width * 0.32f, r.y + s * 0.32f }, c = { r.x + r.width * 0.52f, r.y + s * 0.65f };
			DrawLineEx(a, b, s * 0.07f, col); DrawLineEx(b, c, s * 0.07f, col);
			big = "CR";
			break;
		}
		case NoteType::Jump: {
			Vector2 a = { r.x + r.width * 0.1f, r.y + s * 0.7f }, b = { r.x + r.width * 0.35f, r.y + s * 0.3f };
			DrawLineEx(a, b, s * 0.07f, col);
			DrawCircleV({ r.x + r.width * 0.45f, r.y + s * 0.25f }, s * 0.06f, col);
			big = "JMP";
			break;
		}
		case NoteType::Bridge: {
			DrawRectangleV({ r.x + r.width * 0.08f, r.y + s * 0.45f }, { r.width * 0.45f, s * 0.07f }, col);
			for (int i = 0; i < 4; ++i) DrawRectangleV({ r.x + r.width * (0.1f + i * 0.13f), r.y + s * 0.5f }, { s * 0.04f, s * 0.2f }, col);
			big = "BR";
			break;
		}
		case NoteType::Finish: {
			for (int y = 0; y < 3; ++y) for (int x = 0; x < 4; ++x)
				if ((x + y) % 2 == 0) DrawRectangleV({ r.x + r.width * 0.1f + x * s * 0.1f, r.y + s * 0.3f + y * s * 0.1f }, { s * 0.1f, s * 0.1f }, fade(WHITE, alpha));
			big = "FIN";
			break;
		}
		}
		float bigSize = big.size() >= 3 ? s * 0.32f : s * 0.5f;
		ui::text(big, r.x + r.width * 0.78f, r.y + s * 0.08f + (s * 0.5f - bigSize) * 0.5f, bigSize, fade(ui::kText, alpha), ui::Center, true);
		std::string mods;
		for (uint32_t b = 1; b < (1u << MOD_COUNT); b <<= 1) {
			if (!(n.mods & b) || b == MOD_SQUARE) continue;
			if (!mods.empty()) mods += " ";
			const char* nm = modName(b);
			mods += nm;
		}
		if (!n.link.empty() && n.type != NoteType::Finish) mods += (mods.empty() ? "" : " | ") + n.link;
		if (!mods.empty()) ui::text(mods, r.x + r.width * 0.5f, r.y + s * 0.74f, s * 0.15f, fade(ui::kAccent, alpha), ui::Center);
	}

	void Game::drawHud() {
		Settings& cfg = settings();
		(void)cfg;
		if (run_.role == Role::Driver) drawDriverHud();
		else drawNavigatorHud();

		float s = ui::s();
		int W = GetScreenWidth(), H = GetScreenHeight();
		if (run_.messageTimer > 0) {
			float a = std::min(1.0f, run_.messageTimer * 2.0f);
			Vector2 sz = ui::measure(run_.message, 40 * s);
			DrawRectangle((int)(W * 0.5f - sz.x * 0.5f - 20 * s), (int)(H * 0.22f - 8 * s), (int)(sz.x + 40 * s), (int)(sz.y + 16 * s), fade(Color{ 15, 15, 18, 200 }, a));
			ui::text(run_.message, W * 0.5f, H * 0.22f, 40 * s, fade(run_.messageColor, a), ui::Center);
		}
		if (run_.phase == RunPhase::Countdown || run_.phase == RunPhase::WaitingPartner) drawCountdown();
		if (run_.online) {
			char buf[96];
			std::snprintf(buf, sizeof buf, "%s  %s  |  %u ms", run_.role == Role::Driver ? "Navigator:" : "Driver:", session_.partnerName().c_str(), session_.ping());
			ui::text(buf, W - 16 * s, H - 34 * s, 22 * s, run_.partnerLost ? ui::kBad : ui::kTextDim, ui::Right);
		}
		if (debugOverlay_) drawDebug();
		if (run_.phase == RunPhase::Finished && run_.finishedFor > 2.5f) drawResults();
		else if (run_.paused) drawPauseMenu();
	}

	void Game::drawCountdown() {
		float s = ui::s();
		int W = GetScreenWidth(), H = GetScreenHeight();
		if (run_.phase == RunPhase::WaitingPartner) {
			ui::text(run_.role == Role::Driver ? "Waiting for navigator to load..." : "Waiting for driver...", W * 0.5f, H * 0.3f, 36 * s, ui::kText, ui::Center);
			return;
		}
		int sec = (int)std::ceil(run_.countdown);
		std::string t = sec > 3 ? "READY" : std::to_string(sec);
		float frac = run_.countdown - std::floor(run_.countdown);
		float size = (sec > 3 ? 90 : 160) * s * (0.85f + 0.3f * frac);
		ui::text(t, W * 0.5f, H * 0.3f - size * 0.5f, size, sec > 3 ? ui::kText : ui::kAccent, ui::Center, true);
		ui::text(stage_.def.name, W * 0.5f, H * 0.3f + 90 * s, 34 * s, ui::kText, ui::Center);
		std::string sub = std::string(weatherName(weather_)) + "  |  " + timeOfDayName(timeOfDay_) + "  |  " + cars_[carIndex_].name;
		ui::text(sub, W * 0.5f, H * 0.3f + 132 * s, 24 * s, ui::kTextDim, ui::Center);
		if (run_.role == Role::Driver)
			ui::text("Throttle/Brake: W/S   Steer: A/D   Handbrake: Space   Reset: R   Camera: C", W * 0.5f, H * 0.9f, 22 * s, ui::kTextDim, ui::Center);
		else
			ui::text("Call the next note: SPACE / A button   Change view: TAB   Click a note to call it", W * 0.5f, H * 0.9f, 22 * s, ui::kTextDim, ui::Center);
	}

	namespace {
		// Analog gauge: 270 degrees of sweep, from bottom-left clockwise to
		// bottom-right (raylib angles: 0 = right, increasing clockwise).
		float dialAngle(float t) { return 135.0f + 270.0f * std::clamp(t, 0.0f, 1.04f); }
		Vector2 dialPoint(Vector2 c, float r, float deg) { return { c.x + std::cos(deg * DEG2RAD) * r, c.y + std::sin(deg * DEG2RAD) * r }; }

		void drawDial(Vector2 c, float r, float value, float maxV, float major, int minorPer, float redFrom,
			float labelDiv, const char* unit, Color needle, float s) {
			DrawCircleV(c, r, Color{ 10, 11, 14, 225 });
			DrawRing(c, r - 4 * s, r, 0, 360, 72, Color{ 70, 74, 84, 255 });
			if (redFrom < maxV) DrawRing(c, r * 0.80f, r * 0.89f, dialAngle(redFrom / maxV), dialAngle(1.0f), 32, Color{ 210, 40, 40, 230 });
			float step = major / minorPer;
			int idx = 0;
			for (float t = 0; t <= maxV + step * 0.5f; t += step, ++idx) {
				float a = dialAngle(t / maxV);
				bool isMajor = idx % minorPer == 0;
				float len = isMajor ? 0.15f : 0.07f;
				Color tc = (t >= redFrom) ? Color{ 255, 90, 80, 255 } : (isMajor ? Color{ 235, 235, 240, 255 } : Color{ 140, 145, 155, 255 });
				DrawLineEx(dialPoint(c, r * (0.93f - len), a), dialPoint(c, r * 0.93f, a), isMajor ? 3.0f * s : 1.5f * s, tc);
				if (isMajor) {
					char nb[16];
					if (labelDiv < 1.0f) std::snprintf(nb, sizeof nb, "%.1f", t);
					else std::snprintf(nb, sizeof nb, "%d", (int)std::round(t / labelDiv));
					Vector2 p = dialPoint(c, r * 0.64f, a);
					float fs = r * 0.15f;
					ui::text(nb, p.x, p.y - fs * 0.5f, fs, tc, ui::Center);
				}
			}
			ui::text(unit, c.x, c.y + r * 0.62f, std::max(13.0f * s, r * 0.12f), Color{ 140, 145, 155, 255 }, ui::Center);
			float a = dialAngle(value / maxV);
			Vector2 tip = dialPoint(c, r * 0.86f, a), tail = dialPoint(c, -r * 0.14f, a);
			DrawLineEx(tail, tip, 4.0f * s, needle);
			DrawCircleV(c, r * 0.07f, Color{ 40, 42, 48, 255 });
			DrawCircleV(c, r * 0.04f, needle);
		}

		void lamp(float x, float y, float s, const char* txt, bool on, Color col) {
			Color bg = on ? col : Color{ 35, 37, 44, 230 };
			Rectangle r = { x, y, 52 * s, 24 * s };
			DrawRectangleRounded(r, 0.3f, 4, bg);
			ui::text(txt, x + r.width * 0.5f, y + 3 * s, 17 * s, on ? Color{ 15, 15, 18, 255 } : Color{ 90, 94, 104, 255 }, ui::Center);
		}
	}

	void Game::drawDashboard(const CarVisual& v, const CarSpec& spec, Rectangle area, float s) {
		Settings& cfg = settings();
		float W = area.x + area.width, H = area.y + area.height;
		float t = (float)GetTime();

		// Tachometer (right) with the gear in the middle.
		Vector2 tc = { W - 250 * s, H - 185 * s };
		float tr = 160 * s;
		float tachMax = std::ceil((spec.redline + 600.0f) / 1000.0f) * 1000.0f;
		drawDial(tc, tr, v.rpm, tachMax, 1000.0f, 4, spec.redline, 1000.0f, "x1000 rpm", Color{ 255, 120, 30, 255 }, s);
		std::string g = !car_.engineRunning() && run_.role == Role::Driver ? "-" : v.gear < 0 ? "R" : v.gear == 0 ? "N" : std::to_string(v.gear);
		// Digital gear window below the hub.
		Rectangle gw = { tc.x - 34 * s, tc.y + tr * 0.14f, 68 * s, 62 * s };
		DrawRectangleRounded(gw, 0.2f, 4, Color{ 25, 27, 32, 240 });
		DrawRectangleRoundedLinesEx(gw, 0.2f, 4, 1.5f * s, Color{ 70, 74, 84, 255 });
		ui::text(g, gw.x + gw.width * 0.5f, gw.y + 4 * s, 54 * s, ui::kAccent, ui::Center, true);

		// Shift lights over the tach.
		float rpmT = v.rpm / spec.redline;
		bool limiter = rpmT >= 0.985f;
		for (int i = 0; i < 9; ++i) {
			float on = 0.74f + i * 0.028f;
			Color c = i < 4 ? Color{ 60, 220, 90, 255 } : i < 7 ? Color{ 255, 200, 40, 255 } : Color{ 240, 50, 40, 255 };
			bool lit = rpmT >= on;
			if (limiter) { lit = std::fmod(t, 0.16f) < 0.08f; c = Color{ 80, 140, 255, 255 }; }
			Vector2 p = { tc.x - 96 * s + i * 24 * s, tc.y - tr - 20 * s };
			DrawCircleV(p, 8 * s, lit ? c : Color{ 30, 32, 38, 230 });
		}

		// Speedometer (left) with digital readout.
		bool mph = cfg.useMph;
		float spd = std::fabs(v.speed) * (mph ? 2.23694f : 3.6f);
		Vector2 sc = { tc.x - tr - 135 * s, H - 140 * s };
		float sr = 118 * s;
		drawDial(sc, sr, spd, mph ? 140.0f : 220.0f, 20.0f, 2, 1e9f, 1.0f, mph ? "mph" : "km/h", Color{ 255, 120, 30, 255 }, s);
		char buf[32];
		std::snprintf(buf, sizeof buf, "%d", (int)spd);
		Rectangle sw = { sc.x - 40 * s, sc.y + sr * 0.16f, 80 * s, 36 * s };
		DrawRectangleRounded(sw, 0.2f, 4, Color{ 25, 27, 32, 240 });
		ui::text(buf, sc.x, sw.y + 2 * s, 32 * s, ui::kText, ui::Center);

		// Turbo boost gauge.
		if (spec.turbo) {
			Vector2 bc = { sc.x + 30 * s, sc.y - sr - 82 * s };
			drawDial(bc, 74 * s, v.boost * 1.2f, 1.5f, 0.5f, 5, 1.3f, 0.5f, "BOOST bar", Color{ 120, 180, 255, 255 }, s);
		}

		// Pedals: throttle, brake, clutch.
		float px = tc.x + tr + 18 * s, ph = 150 * s, py = tc.y + tr - ph - 6 * s;
		struct P { const char* l; float v; Color c; } pedals[] = {
			{ "T", v.throttle, Color{ 60, 220, 90, 255 } },
			{ "B", v.brake, Color{ 240, 60, 50, 255 } },
			{ "C", run_.role == Role::Driver ? car_.clutchPedal() : 0.0f, Color{ 90, 160, 255, 255 } },
		};
		if (px + 3 * 18 * s > W) px = W - 3 * 18 * s - 4 * s;
		for (int i = 0; i < 3; ++i) {
			Rectangle r = { px + i * 18 * s, py, 12 * s, ph };
			DrawRectangleRec(r, Color{ 15, 16, 20, 210 });
			float val = std::clamp(pedals[i].v, 0.0f, 1.0f);
			DrawRectangleRec({ r.x, r.y + r.height * (1 - val), r.width, r.height * val }, pedals[i].c);
			ui::text(pedals[i].l, r.x + r.width * 0.5f, r.y + r.height + 3 * s, 16 * s, ui::kTextDim, ui::Center);
		}

		// Warning lamps under the speedo.
		float lx = sc.x - 2.2f * 56 * s, ly = H - 22 * s - 6 * s;
		bool engineBad = !car_.engineRunning() || v.damage > 0.3f;
		lamp(lx, ly, s, "ENG", engineBad, Color{ 240, 60, 50, 255 });
		lamp(lx + 56 * s, ly, s, "HB", v.handbrake, Color{ 240, 60, 50, 255 });
		lamp(lx + 112 * s, ly, s, "ABS", cfg.abs && v.brake > 0.5f && std::fabs(v.speed) > 3.0f, Color{ 255, 190, 40, 255 });
		lamp(lx + 168 * s, ly, s, "LIGHT", world_.env().lampIntensity > 0.0f, Color{ 80, 200, 110, 255 });

		// Messages above the cluster.
		if (!car_.engineRunning() && run_.role == Role::Driver) {
			float a = 0.5f + 0.5f * std::sin(t * 6.0f);
			ui::text("ENGINE OFF - press I", tc.x, tc.y - tr - 70 * s, 26 * s, fade(ui::kBad, 0.4f + 0.6f * a), ui::Center);
		}
		if (v.damage > 0.05f) {
			std::snprintf(buf, sizeof buf, "DAMAGE %d%%", (int)(v.damage * 100));
			ui::text(buf, sc.x, sc.y - sr - 34 * s, 22 * s, v.damage > 0.5f ? ui::kBad : ui::kAccent, ui::Center);
		}
	}

	void Game::drawDriverHud() {
		Settings& cfg = settings();
		float s = ui::s();
		int W = GetScreenWidth(), H = GetScreenHeight();
		const CarVisual& v = displayCar();
		const CarSpec& spec = cars_[carIndex_];

		// --- Instrument cluster (bottom right) ---
		char buf[64];
		// In the cockpit the gauges are on the dashboard behind the wheel instead.
		if (!cockpitCluster()) drawDashboard(v, spec, { 0, 0, (float)W, (float)H }, ui::s());

		// --- Stage time (top centre) ---
		std::string tt = ui::formatTime(run_.stageTime + run_.penalties);
		DrawRectangleRounded({ W * 0.5f - 130 * s, 10 * s, 260 * s, 58 * s }, 0.2f, 6, Color{ 15, 16, 20, 180 });
		ui::text(tt, W * 0.5f, 16 * s, 44 * s, ui::kText, ui::Center);
		if (run_.penalties > 0) {
			std::snprintf(buf, sizeof buf, "incl. +%.0fs penalties", run_.penalties);
			ui::text(buf, W * 0.5f, 70 * s, 20 * s, ui::kBad, ui::Center);
		}

		// --- Progress bar (left) ---
		float L = stage_.finishS;
		Rectangle pb = { 24 * s, H * 0.25f, 10 * s, H * 0.5f };
		DrawRectangleRec(pb, Color{ 30, 32, 38, 200 });
		float t = std::clamp(run_.progress / L, 0.0f, 1.0f);
		DrawRectangleRec({ pb.x, pb.y + pb.height * (1 - t), pb.width, pb.height * t }, ui::kAccent);
		for (float sp : stage_.splitS) DrawRectangle((int)(pb.x - 4 * s), (int)(pb.y + pb.height * (1 - sp / L)), (int)(pb.width + 8 * s), (int)(3 * s), WHITE);
		ui::text("FIN", pb.x - 2 * s, pb.y - 26 * s, 18 * s, ui::kTextDim);

		// --- Called pace notes (top centre, below the clock) ---
		int shown = 0;
		float iconW = 190 * s, iconH = 110 * s;
		float x0 = W * 0.5f - (iconW * 1.5f + 12 * s);
		for (int idx : run_.hudNotes) {
			if (shown >= 3) break;
			const PaceNote& n = notes_[idx];
			float dist = n.s - run_.progress;
			float a = dist < -5 ? 0.4f : 1.0f;
			Rectangle r = { x0 + shown * (iconW + 12 * s), 96 * s, iconW, iconH };
			drawNoteIcon(n, r, a, shown == 0);
			++shown;
		}
		if (!run_.online && run_.aiCodriver && cfg.showFps) ui::text("AI co-driver", 24 * s, 20 * s, 20 * s, ui::kTextDim);
	}

	void Game::drawNoteTimeline(Rectangle r) {
		float s = ui::s();
		DrawRectangleRec(r, Color{ 14, 15, 19, 215 });
		DrawRectangle((int)r.x, (int)r.y, (int)r.width, (int)(3 * s), ui::kAccent);
		ui::text("PACE NOTES", r.x + 14 * s, r.y + 10 * s, 24 * s, ui::kAccent, ui::Left, true);
		float y = r.y + 50 * s;
		float rowH = 64 * s;
		int first = 0;
		for (int i = 0; i < (int)notes_.size(); ++i) if (notes_[i].sEnd >= run_.progress - 5) { first = i; break; }
		int nextToCall = -1;
		for (int i = first; i < (int)notes_.size(); ++i) if (!run_.calls[i].called) { nextToCall = i; break; }
		for (int i = first; i < (int)notes_.size() && y + rowH < r.y + r.height; ++i) {
			const PaceNote& n = notes_[i];
			float dist = n.s - run_.progress;
			bool called = run_.calls[i].called;
			Rectangle row = { r.x + 8 * s, y, r.width - 16 * s, rowH - 6 * s };
			bool hover = CheckCollisionPointRec(GetMousePosition(), row);
			Color bg = i == nextToCall ? Color{ 60, 50, 20, 230 } : hover ? Color{ 40, 42, 50, 230 } : Color{ 28, 30, 36, 220 };
			DrawRectangleRec(row, bg);
			Rectangle icon = { row.x + 4 * s, row.y + 4 * s, 90 * s, row.height - 8 * s };
			drawNoteIcon(n, icon, called ? 0.45f : 1.0f, false);
			ui::text(noteText(n, false), row.x + 104 * s, row.y + 6 * s, 24 * s, called ? ui::kTextDim : ui::kText);
			char buf[48];
			std::snprintf(buf, sizeof buf, "%s%.0f m%s", dist < 0 ? "" : "in ", std::max(0.0f, dist), n.link.empty() ? "" : ("   then " + n.link).c_str());
			ui::text(buf, row.x + 104 * s, row.y + 32 * s, 19 * s, ui::kTextDim);
			if (called) ui::text("CALLED", row.x + row.width - 10 * s, row.y + 8 * s, 18 * s, ui::kGood, ui::Right);
			else if (i == nextToCall) {
				// Timing hint: when this note should be called.
				float speed = std::max(5.0f, std::fabs(displayCar().speed));
				float tt = dist / speed;
				Color c = tt > 7 ? ui::kTextDim : tt > 2.5f ? ui::kGood : ui::kBad;
				ui::text(tt > 7 ? "wait" : tt > 2.5f ? "CALL NOW" : "LATE!", row.x + row.width - 10 * s, row.y + 8 * s, 20 * s, c, ui::Right);
			}
			if (hover && !called && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && run_.phase == RunPhase::Running && !run_.paused) callNote(i, false);
			y += rowH;
		}
	}

	void Game::drawPaceBook(Rectangle r) {
		float s = ui::s();
		DrawRectangleRounded(r, 0.03f, 6, Color{ 246, 242, 228, 245 });
		for (float ly = r.y + 70 * s; ly < r.y + r.height - 10; ly += 46 * s) DrawLine((int)(r.x + 20 * s), (int)ly, (int)(r.x + r.width - 20 * s), (int)ly, Color{ 180, 200, 230, 255 });
		DrawLine((int)(r.x + 70 * s), (int)r.y, (int)(r.x + 70 * s), (int)(r.y + r.height), Color{ 230, 120, 120, 255 });
		ui::text(stage_.def.name, r.x + 90 * s, r.y + 18 * s, 32 * s, Color{ 40, 40, 60, 255 }, ui::Left, true);
		int first = 0;
		for (int i = 0; i < (int)notes_.size(); ++i) if (notes_[i].sEnd >= run_.progress - 5) { first = i; break; }
		float y = r.y + 76 * s;
		for (int i = first; i < (int)notes_.size() && y < r.y + r.height - 40 * s; ++i) {
			const PaceNote& n = notes_[i];
			Color ink = run_.calls[i].called ? Color{ 150, 150, 160, 255 } : Color{ 20, 30, 90, 255 };
			char num[16];
			std::snprintf(num, sizeof num, "%d", i + 1);
			ui::text(num, r.x + 60 * s, y, 26 * s, Color{ 200, 90, 90, 255 }, ui::Right);
			ui::text(noteText(n, true), r.x + 90 * s, y, 32 * s, ink);
			y += 46 * s;
		}
	}

	void Game::drawNavigatorHud() {
		float s = ui::s();
		int W = GetScreenWidth(), H = GetScreenHeight();
		const CarVisual& v = displayCar();

		// Stage clock + splits (navigator sees the deltas).
		DrawRectangleRounded({ 16 * s, 12 * s, 330 * s, 128 * s }, 0.12f, 6, Color{ 15, 16, 20, 190 });
		ui::text(ui::formatTime(run_.stageTime + run_.penalties), 32 * s, 18 * s, 44 * s, ui::kText);
		auto best = leaderboard_.bestSplits(stage_.def.id);
		for (int i = 0; i < 2; ++i) {
			std::string line = "Split " + std::to_string(i + 1) + ": ";
			Color c = ui::kTextDim;
			if (run_.splitIndex > i) {
				line += ui::formatTime(run_.splitTimes[i]);
				if ((int)best.size() > i) {
					double d = run_.splitTimes[i] - best[i];
					line += "  " + ui::formatTime(d, true);
					c = d <= 0 ? ui::kGood : ui::kBad;
				}
				else c = ui::kText;
			}
			else line += "--";
			ui::text(line, 32 * s, 70 * s + i * 30 * s, 24 * s, c);
		}
		char buf[64];
		std::snprintf(buf, sizeof buf, "%d km/h   gear %d   %.0f m to go", (int)(std::fabs(v.speed) * 3.6f), v.gear, std::max(0.0f, stage_.finishS - run_.progress));
		ui::text(buf, 16 * s, 150 * s, 22 * s, ui::kTextDim);

		// Right panel: note timeline (or full map / pace book views).
		Rectangle side = { W - 470 * s, 12 * s, 458 * s, H - 24 * s };
		if (navView_ == 2) {
			Rectangle mr = { W * 0.08f, H * 0.08f, W * 0.84f - 470 * s, H * 0.84f };
			DrawRectangleRec(mr, Color{ 12, 14, 18, 225 });
			Matrix rot = QuaternionToMatrix(v.rotation);
			Vector3 f = Vector3Transform({ 0, 0, 1 }, rot);
			drawStageMap(stage_, mr, run_.progress, v.position, std::atan2(f.x, f.z), true);
			ui::text("STAGE MAP", mr.x + 14 * s, mr.y + 10 * s, 26 * s, ui::kAccent, ui::Left, true);
		}
		if (navView_ == 3) {
			drawPaceBook({ W * 0.06f, H * 0.06f, W * 0.88f - 470 * s, H * 0.88f });
		}
		else if (navView_ != 2) {
			// Mini map bottom-left.
			Rectangle mm = { 16 * s, H - 276 * s, 260 * s, 260 * s };
			DrawRectangleRec(mm, Color{ 12, 14, 18, 200 });
			Matrix rot = QuaternionToMatrix(v.rotation);
			Vector3 f = Vector3Transform({ 0, 0, 1 }, rot);
			drawStageMap(stage_, mm, run_.progress, v.position, std::atan2(f.x, f.z), true);
		}
		drawNoteTimeline(side);

		// Call stats + hint.
		std::snprintf(buf, sizeof buf, "Calls  good %d   early %d   late %d   missed %d", run_.goodCalls, run_.earlyCalls, run_.lateCalls, run_.missedCalls);
		ui::text(buf, W * 0.5f - 235 * s, H - 64 * s, 22 * s, ui::kTextDim, ui::Center);
		const char* views[] = { "Co-driver seat", "Chase cam", "Stage map", "Pace-note book" };
		ui::text(std::string("[TAB] View: ") + views[navView_] + "     [SPACE] Call next note", W * 0.5f - 235 * s, H - 34 * s, 22 * s, ui::kTextDim, ui::Center);
	}

	void Game::drawPauseMenu() {
		float s = ui::s();
		int W = GetScreenWidth(), H = GetScreenHeight();
		DrawRectangle(0, 0, W, H, Color{ 0, 0, 0, 140 });
		Rectangle p = { W * 0.5f - 260 * s, H * 0.5f - 280 * s, 520 * s, 560 * s };
		ui::panel(p, ui::kBg);
		if (run_.partnerLost) {
			ui::text("PARTNER DISCONNECTED", W * 0.5f, p.y + 26 * s, 34 * s, ui::kBad, ui::Center, true);
			float y = p.y + 110 * s;
			if (run_.role == Role::Driver) {
				ui::text("Waiting for them to rejoin...", W * 0.5f, p.y + 70 * s, 22 * s, ui::kTextDim, ui::Center);
				if (ui::button({ p.x + 40 * s, y, p.width - 80 * s, 60 * s }, "Continue with AI co-driver")) {
					run_.online = false; run_.partnerLost = false; run_.paused = false; run_.aiCodriver = true;
				}
				y += 76 * s;
			}
			else {
				if (ui::button({ p.x + 40 * s, y, p.width - 80 * s, 60 * s }, "Reconnect")) {
					session_.join(session_.lastJoinAddress(), session_.port(), settings().playerName);
					joinStatus_ = "Reconnecting...";
				}
				y += 76 * s;
				ui::text(joinStatus_, W * 0.5f, y, 20 * s, ui::kTextDim, ui::Center);
				y += 40 * s;
			}
			if (ui::button({ p.x + 40 * s, y, p.width - 80 * s, 60 * s }, "Quit to Menu")) {
				endStage(); session_.stop(); openScreen(Screen::MainMenu);
			}
			return;
		}
		ui::text("PAUSED", W * 0.5f, p.y + 26 * s, 50 * s, ui::kAccent, ui::Center, true);
		float y = p.y + 120 * s;
		if (ui::button({ p.x + 40 * s, y, p.width - 80 * s, 60 * s }, "Resume")) {
			run_.paused = false;
			if (run_.online) session_.sendPause(false);
		}
		y += 76 * s;
		if (!run_.online && !rally_.active) {
			if (ui::button({ p.x + 40 * s, y, p.width - 80 * s, 60 * s }, "Restart Stage")) {
				startStage(stageIndex_, carIndex_, weather_, timeOfDay_, run_.role, false);
				return;
			}
			y += 76 * s;
		}
		// Volume can be changed without leaving the stage.
		Settings& cfg = settings();
		if (ui::slider({ p.x + 40 * s, y, p.width - 80 * s, 44 * s }, "Master", cfg.masterVolume, 0, 1)) SetMasterVolume(cfg.masterVolume);
		y += 52 * s;
		ui::slider({ p.x + 40 * s, y, p.width - 80 * s, 44 * s }, "Engine", cfg.engineVolume, 0, 1); y += 52 * s;
		ui::slider({ p.x + 40 * s, y, p.width - 80 * s, 44 * s }, "Co-driver", cfg.voiceVolume, 0, 1); y += 64 * s;
		if (ui::button({ p.x + 40 * s, y, p.width - 80 * s, 60 * s }, run_.online ? (session_.isHost() ? "Back to Lobby" : "Leave Rally") : "Quit to Menu")) {
			bool online = run_.online;
			cfg.save(kSettingsPath);
			rally_ = RallyState{};
			endStage();
			if (online && session_.isHost()) { session_.sendBackToLobby(); openScreen(Screen::Lobby); }
			else if (online) { session_.stop(); openScreen(Screen::MainMenu); }
			else openScreen(Screen::MainMenu);
		}
	}

	void Game::drawResults() {
		float s = ui::s();
		int W = GetScreenWidth(), H = GetScreenHeight();
		DrawRectangle(0, 0, W, H, Color{ 0, 0, 0, 150 });
		Rectangle p = { W * 0.5f - 420 * s, H * 0.5f - 330 * s, 840 * s, 660 * s };
		ui::panel(p, ui::kBg);
		ui::text("STAGE COMPLETE", W * 0.5f, p.y + 20 * s, 46 * s, ui::kAccent, ui::Center, true);
		ui::text(stage_.def.name + "  -  " + cars_[carIndex_].name, W * 0.5f, p.y + 78 * s, 24 * s, ui::kTextDim, ui::Center);
		ui::text(ui::formatTime(run_.finishTime), W * 0.5f, p.y + 112 * s, 72 * s, ui::kText, ui::Center);
		if (run_.rank > 0) ui::text(run_.rank == 1 ? "NEW STAGE RECORD!" : "Leaderboard position #" + std::to_string(run_.rank), W * 0.5f, p.y + 190 * s, 26 * s, run_.rank == 1 ? ui::kGood : ui::kAccent, ui::Center);

		float y = p.y + 240 * s;
		float lx = p.x + 60 * s, rx = p.x + p.width * 0.5f + 20 * s;
		char buf[96];
		auto row = [&](float x, const std::string& a, const std::string& b, Color c = ui::kText) {
			ui::text(a, x, y, 24 * s, ui::kTextDim);
			ui::text(b, x + 330 * s, y, 24 * s, c, ui::Right);
		};
		row(lx, "Stage time", ui::formatTime(run_.stageTime));
		row(rx, "Split 1", ui::formatTime(run_.splitTimes[0])); y += 36 * s;
		std::snprintf(buf, sizeof buf, "+%.0fs", run_.penalties);
		row(lx, "Penalties", buf, run_.penalties > 0 ? ui::kBad : ui::kGood);
		row(rx, "Split 2", ui::formatTime(run_.splitTimes[1])); y += 36 * s;
		row(lx, "Resets", std::to_string(run_.resets));
		row(rx, "Corner cuts", std::to_string(run_.cuts)); y += 50 * s;

		ui::text("Pace-note calls", lx, y, 26 * s, ui::kAccent); y += 38 * s;
		bool humanNav = run_.online || run_.role == Role::Navigator;
		if (humanNav) {
			row(lx, "On time", std::to_string(run_.goodCalls), ui::kGood);
			row(rx, "Early", std::to_string(run_.earlyCalls), run_.earlyCalls ? ui::kAccent : ui::kText); y += 36 * s;
			row(lx, "Late", std::to_string(run_.lateCalls), run_.lateCalls ? ui::kBad : ui::kText);
			row(rx, "Missed", std::to_string(run_.missedCalls), run_.missedCalls ? ui::kBad : ui::kText); y += 36 * s;
			int total = run_.goodCalls + run_.earlyCalls + run_.lateCalls + run_.missedCalls;
			std::snprintf(buf, sizeof buf, "Timing score: %d%%", total ? (int)(100.0f * run_.goodCalls / total) : 0);
			ui::text(buf, lx, y, 24 * s, ui::kText);
		}
		else ui::text("AI co-driver called " + std::to_string((int)notes_.size()) + " notes.", lx, y, 24 * s, ui::kTextDim);

		float by = p.y + p.height - 90 * s;
		float bw = (p.width - 100 * s) / 3.0f;

		// --- Rally mode: running total and the next stage ---
		if (rally_.active) {
			bool last = rally_.index + 1 >= rally_.count;
			char rb[128];
			std::string evName = rally_.event < (int)eventNames_.size() ? eventNames_[rally_.event] : std::string("RALLY");
			std::snprintf(rb, sizeof rb, "%s  stage %d of %d   total %s", evName.c_str(), rally_.index + 1, rally_.count, ui::formatTime(rally_.total).c_str());
			ui::text(last ? "RALLY COMPLETE  -  total " + ui::formatTime(rally_.total) : std::string(rb), W * 0.5f, by - 44 * s, 28 * s, last ? ui::kGood : ui::kAccent, ui::Center);
			bool canDrive = !run_.online || session_.isHost();
			if (!last && canDrive) {
				if (ui::button({ p.x + 30 * s, by, bw * 1.6f, 60 * s }, "Next Stage (" + std::to_string(rally_.index + 2) + "/" + std::to_string(rally_.count) + ")")) {
					rally_.index++;
					if (run_.online) { endStage(); hostStartStage(); }
					else startStage(rally_.index, carIndex_, weather_, timeOfDay_, run_.role, false);
					return;
				}
			}
			else if (!canDrive) ui::text("Waiting for the host...", p.x + 30 * s, by + 16 * s, 22 * s, ui::kTextDim);
			if (ui::button({ p.x + p.width - 30 * s - bw * 1.2f, by, bw * 1.2f, 60 * s }, last ? (run_.online && session_.isHost() ? "Back to Lobby" : "Finish") : "Abandon Rally")) {
				bool online = run_.online;
				rally_ = RallyState{};
				endStage();
				if (online && session_.isHost()) { session_.sendBackToLobby(); lobby_.clientReady = false; lobbyChanged(); openScreen(Screen::Lobby); }
				else if (online) { session_.stop(); openScreen(Screen::MainMenu); }
				else openScreen(Screen::MainMenu);
			}
			return;
		}

		if (!run_.online) {
			if (ui::button({ p.x + 30 * s, by, bw, 60 * s }, "Retry")) { startStage(stageIndex_, carIndex_, weather_, timeOfDay_, run_.role, false); return; }
			if (ui::button({ p.x + 50 * s + bw, by, bw, 60 * s }, "Next Stage")) {
				int next = (stageIndex_ + 1) % std::max(1, (int)stageDefs_.size());
				startStage(next, carIndex_, weather_, timeOfDay_, run_.role, false);
				return;
			}
			if (ui::button({ p.x + 70 * s + 2 * bw, by, bw, 60 * s }, "Main Menu")) { endStage(); openScreen(Screen::MainMenu); }
		}
		else if (session_.isHost()) {
			if (ui::button({ p.x + 30 * s, by, bw * 1.5f, 60 * s }, "Back to Lobby")) {
				endStage(); session_.sendBackToLobby(); lobby_.clientReady = false; lobbyChanged(); openScreen(Screen::Lobby);
			}
			if (ui::button({ p.x + 70 * s + bw * 1.5f, by, bw * 1.5f - 40 * s, 60 * s }, "Swap Roles & Lobby")) {
				endStage(); lobby_.driverIsHost = !lobby_.driverIsHost; lobby_.clientReady = false;
				session_.sendBackToLobby(); lobbyChanged(); openScreen(Screen::Lobby);
			}
		}
		else {
			ui::text("Waiting for the host to continue...", W * 0.5f, by - 30 * s, 22 * s, ui::kTextDim, ui::Center);
			if (ui::button({ p.x + p.width * 0.5f - bw * 0.5f, by, bw, 60 * s }, "Leave")) { endStage(); session_.stop(); openScreen(Screen::MainMenu); }
		}
	}

	void Game::drawDebug() {
		float s = ui::s();
		const VehicleTelemetry& t = car_.telemetry();
		const CarVisual& v = displayCar();
		Rectangle r = { 300 * s, 12 * s, 560 * s, 300 * s };
		DrawRectangleRec(r, Color{ 0, 0, 0, 170 });
		char buf[160];
		float y = r.y + 8 * s;
		auto line = [&](const char* txt) { ui::text(txt, r.x + 10 * s, y, 19 * s, ui::kText); y += 22 * s; };
		std::snprintf(buf, sizeof buf, "FPS %d   speed %.1f km/h   rpm %.0f   gear %d   boost %.2f", GetFPS(), v.speed * 3.6f, v.rpm, v.gear, v.boost); line(buf);
		std::snprintf(buf, sizeof buf, "latG %.2f  longG %.2f  drive %.0f N  wheels %d  slip F %.2f R %.2f", t.lateralG, t.longG, t.driveForce, t.wheelsOnGround, t.frontSlip, t.rearSlip); line(buf);
		std::snprintf(buf, sizeof buf, "progress %.1f / %.0f   lateral %.2f   hint %d", run_.progress, stage_.finishS, run_.lateral, run_.hint); line(buf);
		std::snprintf(buf, sizeof buf, "damage eng %.2f steer %.2f susp %.2f  dirt %.2f  lights %d", car_.engineDamage, car_.steeringDamage, car_.suspensionDamage, car_.dirt, car_.lightsBroken); line(buf);
		const char* names[] = { "FL", "FR", "RL", "RR" };
		for (int i = 0; i < 4; ++i) {
			const Wheel& w = car_.wheel(i);
			std::snprintf(buf, sizeof buf, "%s  load %5.0f  comp %.3f  slipA %+.3f  slip %.2f  %s%s%s  %s", names[i], w.load, w.compression, w.slipAngle, w.slipRatio,
				w.contact ? "" : "AIR ", w.spinning ? "SPIN " : "", w.locked ? "LOCK " : "", surfaceProps(w.surface).name);
			line(buf);
		}
		line("F5: reload res/data/cars.json   F3: hide");
	}
}
