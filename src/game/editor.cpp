#include "game/game.h"
#include "game/stageGen.h"
#include "core/ui.h"
#include "core/log.h"
#include <raymath.h>
#include <filesystem>
#include <cmath>
#include <ctime>
#include <algorithm>

namespace rally {
	namespace {
		// Top-down editor space: mirrored x so the map reads like the in-game map.
		Vector2 toEd(Vector2 world) { return { -world.x, -world.y }; }
		Vector2 fromEd(Vector2 ed) { return { -ed.x, -ed.y }; }

		Color surfaceColor(Surface s) {
			switch (s) {
			case Surface::Tarmac: return { 90, 90, 100, 255 };
			case Surface::Gravel: return { 190, 160, 120, 255 };
			case Surface::Snow: return { 235, 240, 250, 255 };
			case Surface::Mud: return { 110, 80, 50, 255 };
			case Surface::Grass: return { 90, 150, 70, 255 };
			default: return { 150, 120, 90, 255 };
			}
		}
	}

	void Game::editorOpen(int stageIndex) {
		if (stageDefs_.empty()) return;
		ed_ = EditorState{};
		ed_.stage = std::clamp(stageIndex, 0, (int)stageDefs_.size() - 1);
		ed_.def = stageDefs_[ed_.stage];
		ed_.path = stagePath(ed_.stage);
		editorRebuildPreview();
		// Fit the view.
		float minX = 1e9f, minZ = 1e9f, maxX = -1e9f, maxZ = -1e9f;
		for (auto& p : ed_.def.points) {
			minX = std::min(minX, p.x); maxX = std::max(maxX, p.x);
			minZ = std::min(minZ, p.y); maxZ = std::max(maxZ, p.y);
		}
		Vector2 c = toEd({ (minX + maxX) * 0.5f, (minZ + maxZ) * 0.5f });
		ed_.cam2d.target = c;
		ed_.cam2d.offset = { GetScreenWidth() * 0.38f, GetScreenHeight() * 0.5f };
		float span = std::max(maxX - minX, maxZ - minZ) + 100.0f;
		ed_.cam2d.zoom = std::min(GetScreenWidth() * 0.7f, (float)GetScreenHeight()) / span;
		openScreen(Screen::Editor);
	}

	void Game::editorRebuildPreview() {
		ed_.preview.build(ed_.def, Weather::Clear, TimeOfDay::Day, false);
		ed_.previewNotes = generatePaceNotes(ed_.preview);
		ed_.closest = trackClosestApproach(ed_.def.points, 160.0f);
		if (ed_.selectedNote >= (int)ed_.previewNotes.size()) ed_.selectedNote = -1;
	}

	void Game::editorFrame(float dt) {
		float s = ui::s();
		int W = GetScreenWidth(), H = GetScreenHeight();
		ClearBackground(Color{ 24, 28, 26, 255 });
		EditorState& e = ed_;
		if (e.statusTimer > 0) e.statusTimer -= dt;
		auto status = [&](const std::string& m) { e.status = m; e.statusTimer = 4.0f; };

		Rectangle panel = { W - 520 * s, 0, 520 * s, (float)H };
		Vector2 mouse = GetMousePosition();
		bool overPanel = CheckCollisionPointRec(mouse, panel);
		e.cam2d.offset = { (W - panel.width) * 0.5f, H * 0.5f };

		// --- View controls ---
		if (!overPanel) {
			float wheel = GetMouseWheelMove();
			if (wheel != 0) {
				Vector2 before = GetScreenToWorld2D(mouse, e.cam2d);
				e.cam2d.zoom = std::clamp(e.cam2d.zoom * (wheel > 0 ? 1.15f : 1 / 1.15f), 0.05f, 20.0f);
				Vector2 after = GetScreenToWorld2D(mouse, e.cam2d);
				e.cam2d.target = Vector2Add(e.cam2d.target, Vector2Subtract(before, after));
			}
			if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT) || IsMouseButtonDown(MOUSE_BUTTON_MIDDLE)) {
				Vector2 d = GetMouseDelta();
				e.cam2d.target = Vector2Subtract(e.cam2d.target, Vector2Scale(d, 1.0f / e.cam2d.zoom));
			}
		}
		float pan = 500.0f * dt / e.cam2d.zoom;
		if (!ui::typing()) {
			if (IsKeyDown(KEY_W)) e.cam2d.target.y -= pan;
			if (IsKeyDown(KEY_S) && !IsKeyDown(KEY_LEFT_CONTROL)) e.cam2d.target.y += pan;
			if (IsKeyDown(KEY_A)) e.cam2d.target.x -= pan;
			if (IsKeyDown(KEY_D)) e.cam2d.target.x += pan;
		}

		// --- Point editing ---
		Vector2 mouseEd = GetScreenToWorld2D(mouse, e.cam2d);
		Vector2 mouseWorld = fromEd(mouseEd);
		float pickR = 10.0f / e.cam2d.zoom;
		int hover = -1;
		for (int i = 0; i < (int)e.def.points.size(); ++i)
			if (Vector2Distance(toEd(e.def.points[i]), mouseEd) < pickR) hover = i;
		bool changed = false;
		if (!overPanel && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
			if (hover >= 0) { e.selected = hover; e.dragging = true; }
			else if (IsKeyDown(KEY_LEFT_SHIFT)) {
				int at = e.selected >= 0 ? e.selected + 1 : (int)e.def.points.size();
				e.def.points.insert(e.def.points.begin() + at, mouseWorld);
				e.selected = at;
				changed = true;
			}
			else e.selected = -1;
		}
		if (e.dragging) {
			if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && e.selected >= 0) e.def.points[e.selected] = mouseWorld;
			if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) { e.dragging = false; changed = true; }
		}
		if (e.selected >= 0 && (IsKeyPressed(KEY_DELETE) || IsKeyPressed(KEY_BACKSPACE)) && e.def.points.size() > 4 && !ui::typing()) {
			e.def.points.erase(e.def.points.begin() + e.selected);
			e.selected = std::min(e.selected, (int)e.def.points.size() - 1);
			changed = true;
		}

		// s range of the segment after the selected control point.
		auto segmentRange = [&](float& a, float& b) {
			if (e.selected < 0 || e.selected + 1 >= (int)e.def.points.size()) return false;
			int hint = -1;
			Vector2 p0 = e.def.points[e.selected], p1 = e.def.points[e.selected + 1];
			a = e.preview.project({ p0.x, 0, p0.y }, hint);
			hint = -1;
			b = e.preview.project({ p1.x, 0, p1.y }, hint);
			if (b < a) std::swap(a, b);
			return true;
		};
		if (!ui::typing()) {
			if (IsKeyPressed(KEY_LEFT_BRACKET)) { e.def.width = std::max(4.5f, e.def.width - 0.5f); changed = true; }
			if (IsKeyPressed(KEY_RIGHT_BRACKET)) { e.def.width = std::min(10.0f, e.def.width + 0.5f); changed = true; }
			for (int k = 0; k < (int)Surface::Count; ++k) {
				if (IsKeyPressed(KEY_ONE + k)) {
					float a, b;
					if (segmentRange(a, b)) { e.def.surfaceSegments.push_back({ a, b, (Surface)k }); changed = true; status(std::string("Segment surface: ") + surfaceProps((Surface)k).name); }
				}
			}
			if (IsKeyPressed(KEY_B)) {
				float a, b;
				if (segmentRange(a, b)) {
					auto it = std::find_if(e.def.bridges.begin(), e.def.bridges.end(), [&](const Range& r) { return r.from < b && r.to > a; });
					if (it != e.def.bridges.end()) e.def.bridges.erase(it); else e.def.bridges.push_back({ a, std::min(b, a + 60.0f) });
					changed = true;
				}
			}
			if (IsKeyPressed(KEY_J) && e.selected >= 0) {
				int hint = -1;
				Vector2 p = e.def.points[e.selected];
				float at = e.preview.project({ p.x, 0, p.y }, hint);
				auto it = std::find_if(e.def.jumps.begin(), e.def.jumps.end(), [&](const JumpDef& j) { return std::fabs(j.at - at) < 20; });
				if (it != e.def.jumps.end()) e.def.jumps.erase(it); else e.def.jumps.push_back({ at, 1.3f });
				changed = true;
			}
			if (IsKeyPressed(KEY_S) && IsKeyDown(KEY_LEFT_CONTROL)) {
				bool ok = saveStageDef(e.path, e.def);
				std::string src = "../" + e.path;
				if (std::filesystem::exists(std::filesystem::path(src).parent_path())) saveStageDef(src, e.def);
				stageDefs_[e.stage] = e.def;
				e.dirty = false;
				status(ok ? "Saved " + e.path : "Save failed!");
			}
		}
		if (changed) { e.dirty = true; editorRebuildPreview(); }

		// --- Draw map ---
		BeginMode2D(e.cam2d);
		float lw = std::max(1.5f / e.cam2d.zoom, e.def.width);
		// Grid.
		float grid = 100.0f;
		Vector2 tl = GetScreenToWorld2D({ 0, 0 }, e.cam2d), br = GetScreenToWorld2D({ (float)W, (float)H }, e.cam2d);
		for (float gx = std::floor(tl.x / grid) * grid; gx < br.x; gx += grid) DrawLineV({ gx, tl.y }, { gx, br.y }, Color{ 40, 46, 42, 255 });
		for (float gy = std::floor(tl.y / grid) * grid; gy < br.y; gy += grid) DrawLineV({ tl.x, gy }, { br.x, gy }, Color{ 40, 46, 42, 255 });
		const auto& S = e.preview.samples;
		for (size_t i = 2; i < S.size(); i += 2) {
			Color c = surfaceColor(S[i].surface);
			if (S[i].bridge) c = { 90, 150, 230, 255 };
			DrawLineEx(toEd({ S[i - 2].pos.x, S[i - 2].pos.z }), toEd({ S[i].pos.x, S[i].pos.z }), lw, c);
		}
		// Control polygon & points.
		for (size_t i = 1; i < e.def.points.size(); ++i)
			DrawLineEx(toEd(e.def.points[i - 1]), toEd(e.def.points[i]), 1.0f / e.cam2d.zoom, Color{ 255, 255, 255, 60 });
		for (int i = 0; i < (int)e.def.points.size(); ++i) {
			Color c = i == e.selected ? ui::kAccent : i == hover ? WHITE : Color{ 220, 220, 220, 200 };
			DrawCircleV(toEd(e.def.points[i]), 6.0f / e.cam2d.zoom, c);
		}
		for (auto& j : e.def.jumps) {
			RoadSample r = e.preview.sampleAt(j.at);
			DrawCircleV(toEd({ r.pos.x, r.pos.z }), 9.0f / e.cam2d.zoom, Color{ 230, 90, 200, 220 });
		}
		auto mark = [&](float sPos, Color c) { RoadSample r = e.preview.sampleAt(sPos); DrawCircleV(toEd({ r.pos.x, r.pos.z }), 11.0f / e.cam2d.zoom, c); };
		mark(e.preview.startS, { 40, 200, 80, 255 });
		for (float sp : e.preview.splitS) mark(sp, { 255, 190, 30, 255 });
		mark(e.preview.finishS, { 230, 50, 50, 255 });
		for (int i = 0; i < (int)e.previewNotes.size(); ++i) {
			const PaceNote& n = e.previewNotes[i];
			RoadSample r = e.preview.sampleAt(n.s);
			Vector2 p = toEd({ r.pos.x, r.pos.z });
			bool sel = i == e.selectedNote;
			DrawCircleV(p, (sel ? 8.0f : 4.0f) / e.cam2d.zoom, sel ? ui::kAccent : Color{ 120, 200, 255, 255 });
			if (e.cam2d.zoom > 0.6f || sel) {
				Vector2 sp = GetWorldToScreen2D(p, e.cam2d);
				EndMode2D();
				ui::text(noteText(n, false), sp.x + 8, sp.y - 10, 16, sel ? ui::kAccent : Color{ 200, 230, 255, 255 });
				BeginMode2D(e.cam2d);
			}
		}
		EndMode2D();

		// --- Side panel ---
		ui::panel(panel, ui::kBg);
		float x = panel.x + 20 * s, y = 16 * s, w = panel.width - 40 * s, h = 40 * s;
		ui::text("STAGE EDITOR", x, y, 36 * s, ui::kAccent, ui::Left, true); y += 50 * s;
		if (ui::textField({ x, y, w, h }, "Name", e.def.name, 32)) e.dirty = true;
		y += h + 6 * s;
		std::vector<std::string> stages;
		for (auto& d : stageDefs_) stages.push_back(d.name);
		int st = e.stage;
		if (ui::choice({ x, y, w, h }, "Edit stage", st, stages) && st != e.stage) { editorOpen(st); return; }
		y += h + 6 * s;
		int envI = e.def.environment == "snow" ? 2 : e.def.environment == "mountain" ? 1 : 0;
		if (ui::choice({ x, y, w, h }, "Environment", envI, { "forest", "mountain", "snow" })) {
			e.def.environment = envI == 2 ? "snow" : envI == 1 ? "mountain" : "forest";
			changed = true;
		}
		y += h + 6 * s;
		int surfI = (int)e.def.surface;
		if (ui::choice({ x, y, w, h }, "Main surface", surfI, { "Tarmac", "Gravel", "Snow", "Mud", "Grass", "Dirt" })) { e.def.surface = (Surface)surfI; changed = true; }
		y += h + 10 * s;
		char buf[160];
		std::snprintf(buf, sizeof buf, "Length %.0f m   width %.1f m   %d points   %d notes", e.preview.length(), e.def.width, (int)e.def.points.size(), (int)e.previewNotes.size());
		ui::text(buf, x, y, 18 * s, ui::kTextDim); y += 26 * s;
		const char* help[] = {
			"LMB drag point   Shift+LMB add after selected",
			"Del remove point   RMB/WASD pan   wheel zoom",
			"1-6 surface for segment after point",
			"B bridge on segment   J jump at point",
			"[ ] road width   Ctrl+S save",
		};
		for (const char* hl : help) { ui::text(hl, x, y, 17 * s, ui::kTextDim); y += 22 * s; }
		y += 8 * s;

		// Note editing.
		ui::text("PACE NOTE OVERRIDES", x, y, 22 * s, ui::kAccent); y += 30 * s;
		if (ui::button({ x, y, w * 0.48f, h }, "< Prev note")) e.selectedNote = std::max(0, e.selectedNote - 1);
		if (ui::button({ x + w * 0.52f, y, w * 0.48f, h }, "Next note >")) e.selectedNote = std::min((int)e.previewNotes.size() - 1, e.selectedNote + 1);
		y += h + 6 * s;
		if (e.selectedNote >= 0 && e.selectedNote < (int)e.previewNotes.size()) {
			PaceNote n = e.previewNotes[e.selectedNote];
			std::snprintf(buf, sizeof buf, "#%d at %.0f m:  %s", e.selectedNote + 1, n.s, noteText(n).c_str());
			ui::text(buf, x, y, 20 * s, ui::kText); y += 30 * s;
			auto ov = [&]() -> NoteOverride& {
				for (auto& o : e.def.noteOverrides) if (std::fabs(o.at - n.s) < 3.0f) return o;
				NoteOverride o; o.at = n.s;
				e.def.noteOverrides.push_back(o);
				return e.def.noteOverrides.back();
			};
			auto setMods = [&](uint32_t mods) { NoteOverride& o = ov(); o.modsSet = mods; o.modsClear = ((1u << MOD_COUNT) - 1) & ~mods; };
			if (n.type == NoteType::Corner) {
				if (ui::button({ x, y, w * 0.31f, h }, "Sharper")) { ov().severity = std::max(0, n.severity - 1); changed = true; }
				if (ui::button({ x + w * 0.345f, y, w * 0.31f, h }, "Faster")) { ov().severity = std::min(6, n.severity + 1); changed = true; }
				if (ui::button({ x + w * 0.69f, y, w * 0.31f, h }, "Flip L/R")) { ov().direction = -n.direction; changed = true; }
				y += h + 6 * s;
				uint32_t bits[] = { MOD_LONG, MOD_TIGHTENS, MOD_OPENS, MOD_DONTCUT, MOD_CAUTION, MOD_OVERCREST };
				for (int i = 0; i < 6; ++i) {
					bool on = (n.mods & bits[i]) != 0;
					Rectangle r = { x + (i % 2) * w * 0.52f, y + (i / 2) * (h + 6 * s), w * 0.48f, h };
					if (ui::toggle(r, modName(bits[i]), on)) { setMods(on ? (n.mods | bits[i]) : (n.mods & ~bits[i])); changed = true; }
				}
				y += 3 * (h + 6 * s);
			}
			if (ui::button({ x, y, w * 0.48f, h }, "Remove note")) { ov().remove = true; changed = true; }
			if (ui::button({ x + w * 0.52f, y, w * 0.48f, h }, "Clear override")) {
				e.def.noteOverrides.erase(std::remove_if(e.def.noteOverrides.begin(), e.def.noteOverrides.end(), [&](const NoteOverride& o) { return std::fabs(o.at - n.s) < 3.0f; }), e.def.noteOverrides.end());
				changed = true;
			}
			y += h + 10 * s;
		}
		else { ui::text("Select a note with Prev/Next.", x, y, 18 * s, ui::kTextDim); y += 30 * s; }
		if (changed) { e.dirty = true; editorRebuildPreview(); }

		float by = H - 170 * s;
		if (ui::button({ x, by, w * 0.48f, 50 * s }, "Save")) {
			bool ok = saveStageDef(e.path, e.def);
			std::string src = "../" + e.path;
			if (std::filesystem::exists(std::filesystem::path(src).parent_path())) saveStageDef(src, e.def);
			stageDefs_[e.stage] = e.def;
			e.dirty = false;
			status(ok ? "Saved" : "Save failed!");
		}
		if (ui::button({ x + w * 0.52f, by, w * 0.48f, 50 * s }, "Test Drive")) {
			stageDefs_[e.stage] = e.def;
			startStage(e.stage, carIndex_, Weather::Clear, TimeOfDay::Day, Role::Driver, false);
			return;
		}
		by += 58 * s;
		if (ui::button({ x, by, w * 0.48f, 50 * s }, "New Random Stage")) {
			std::string id = "custom_" + std::to_string((long long)std::time(nullptr));
			uint32_t seed = (uint32_t)std::time(nullptr);
			StageDef d = generateStage(id, "Custom Stage", e.def.environment, e.def.surface, seed, 2500);
			d.event = "Custom Stages";
			std::string path = "res/data/stages/" + id + ".json";
			saveStageDef(path, d);
			if (std::filesystem::exists("../res/data/stages")) saveStageDef("../" + path, d);
			loadContent();
			for (int i = 0; i < (int)stageDefs_.size(); ++i) if (stageDefs_[i].id == id) { editorOpen(i); return; }
		}
		if (ui::button({ x + w * 0.52f, by, w * 0.48f, 50 * s }, "Back") || ui::back()) {
			loadContent();
			openScreen(Screen::MainMenu);
			return;
		}
		if (e.closest < 45.0f) ui::text("Warning: the road passes within " + std::to_string((int)e.closest) + " m of itself", x, H - 64 * s, 18 * s, ui::kBad);
		if (e.dirty) ui::text("Unsaved changes", x, H - 40 * s, 18 * s, ui::kBad);
		if (e.statusTimer > 0) ui::text(e.status, 20 * s, H - 40 * s, 22 * s, ui::kAccent);
	}
}
