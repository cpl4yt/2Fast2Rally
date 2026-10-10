#include "game/game.h"
#include "core/input.h"
#include "core/ui.h"
#include "core/log.h"
#include "core/forceFeedback.h"
#include "core/joystick.h"
#include <raymath.h>
#include <rlgl.h>
#include <cmath>
#include <cstdio>
#include <algorithm>

namespace rally {
	namespace {
		const char* keyName(int k) {
			static char buf[16];
			if (k <= 0) return "-";
			if (k >= KEY_A && k <= KEY_Z) { buf[0] = (char)k; buf[1] = 0; return buf; }
			if (k >= KEY_ZERO && k <= KEY_NINE) { buf[0] = (char)k; buf[1] = 0; return buf; }
			if (k >= KEY_F1 && k <= KEY_F12) { std::snprintf(buf, sizeof buf, "F%d", k - KEY_F1 + 1); return buf; }
			switch (k) {
			case KEY_SPACE: return "Space"; case KEY_ESCAPE: return "Esc"; case KEY_ENTER: return "Enter";
			case KEY_TAB: return "Tab"; case KEY_BACKSPACE: return "Backspace"; case KEY_UP: return "Up";
			case KEY_DOWN: return "Down"; case KEY_LEFT: return "Left"; case KEY_RIGHT: return "Right";
			case KEY_LEFT_SHIFT: return "LShift"; case KEY_RIGHT_SHIFT: return "RShift";
			case KEY_LEFT_CONTROL: return "LCtrl"; case KEY_RIGHT_CONTROL: return "RCtrl";
			case KEY_LEFT_ALT: return "LAlt"; case KEY_RIGHT_ALT: return "RAlt";
			case KEY_COMMA: return ","; case KEY_PERIOD: return "."; case KEY_SLASH: return "/";
			case KEY_SEMICOLON: return ";"; case KEY_APOSTROPHE: return "'"; case KEY_MINUS: return "-";
			case KEY_EQUAL: return "="; case KEY_LEFT_BRACKET: return "["; case KEY_RIGHT_BRACKET: return "]";
			case KEY_BACKSLASH: return "\\"; case KEY_GRAVE: return "`";
			default: std::snprintf(buf, sizeof buf, "Key %d", k); return buf;
			}
		}
		const char* padName(int b) {
			switch (b) {
			case 0: return "-";
			case GAMEPAD_BUTTON_LEFT_FACE_UP: return "D-Up"; case GAMEPAD_BUTTON_LEFT_FACE_RIGHT: return "D-Right";
			case GAMEPAD_BUTTON_LEFT_FACE_DOWN: return "D-Down"; case GAMEPAD_BUTTON_LEFT_FACE_LEFT: return "D-Left";
			case GAMEPAD_BUTTON_RIGHT_FACE_UP: return "Y / Tri"; case GAMEPAD_BUTTON_RIGHT_FACE_RIGHT: return "B / Circle";
			case GAMEPAD_BUTTON_RIGHT_FACE_DOWN: return "A / Cross"; case GAMEPAD_BUTTON_RIGHT_FACE_LEFT: return "X / Square";
			case GAMEPAD_BUTTON_LEFT_TRIGGER_1: return "LB"; case GAMEPAD_BUTTON_LEFT_TRIGGER_2: return "LT";
			case GAMEPAD_BUTTON_RIGHT_TRIGGER_1: return "RB"; case GAMEPAD_BUTTON_RIGHT_TRIGGER_2: return "RT";
			case GAMEPAD_BUTTON_MIDDLE_LEFT: return "Back"; case GAMEPAD_BUTTON_MIDDLE: return "Guide";
			case GAMEPAD_BUTTON_MIDDLE_RIGHT: return "Start"; case GAMEPAD_BUTTON_LEFT_THUMB: return "LS";
			case GAMEPAD_BUTTON_RIGHT_THUMB: return "RS";
			default: { static char b2[16]; std::snprintf(b2, sizeof b2, "Btn %d", b); return b2; }
			}
		}
		std::string axisName(const AxisBinding& a) {
			if (a.axis < 0) return "-";
			char buf[48];
			std::snprintf(buf, sizeof buf, "Axis %d%s%s", a.axis, a.invert ? " (inv)" : "", a.mode == 1 ? " pedal" : "");
			return buf;
		}

		std::vector<std::string> weatherNames() { return { "Clear", "Overcast", "Rain" }; }
		std::vector<std::string> timeNames() { return { "Day", "Dusk", "Night" }; }
	}

	// ------------------------------------------------------------------
	// Background: the selected car on a turntable.
	// ------------------------------------------------------------------
	void Game::drawMenuBackground(float dt) {
		static Mesh ground{};
		if (ground.vertexCount == 0) ground = GenMeshPlane(80, 80, 1, 1);
		menuSpin_ += dt * 0.25f;
		Environment env = makeEnvironment("forest", Weather::Clear, TimeOfDay::Dusk);
		env.fogDensity = 0.012f;
		env.lampIntensity = 1.0f;
		world_.setEnvironment(env);

		Camera3D cam{};
		float r = 7.2f;
		cam.position = { std::sin(menuSpin_) * r, 2.1f, std::cos(menuSpin_) * r };
		cam.target = { 0, 0.55f, 0 };
		cam.up = { 0, 1, 0 };
		cam.fovy = 40;
		cam.projection = CAMERA_PERSPECTIVE;
		// Shift the car to the right of the screen so menus sit on the left.
		Vector3 fwd = Vector3Normalize(Vector3Subtract(cam.target, cam.position));
		Vector3 right = Vector3CrossProduct(fwd, { 0, 1, 0 });
		cam.position = Vector3Subtract(cam.position, Vector3Scale(right, 2.6f));
		cam.target = Vector3Subtract(cam.target, Vector3Scale(right, 2.6f));

		menuVisual_ = CarVisual{};
		menuVisual_.position = { 0, menuCarRender_.spec().wheelRadius + menuCarRender_.spec().restLength - menuCarRender_.spec().mountY - 0.06f, 0 };
		menuVisual_.rotation = QuaternionIdentity();
		for (int i = 0; i < 4; ++i) menuVisual_.wheelOffset[i] = menuCarRender_.spec().restLength - 0.06f;
		menuVisual_.dirt = 0.15f;
		menuVisual_.steerAngle = 0.25f;
		menuVisual_.steerInput = 0.4f;

		Vector3 lampPos = { 0, 0.6f, 2.4f };
		world_.beginFrame(cam, lampPos, Vector3Normalize({ 0, -0.1f, 1 }), true, 1.0f);
		world_.drawSky(cam);
		BeginMode3D(cam);
		Material& m = world_.colorMaterial();
		m.maps[MATERIAL_MAP_DIFFUSE].texture = world_.whiteTexture();
		m.maps[MATERIAL_MAP_DIFFUSE].color = Color{ 120, 100, 80, 255 };
		DrawMesh(ground, m, MatrixIdentity());
		menuCarRender_.draw(world_, menuVisual_, dt, true, CarView::Exterior);
		EndMode3D();
		// Darken the left side for menu legibility.
		int W = GetScreenWidth(), H = GetScreenHeight();
		DrawRectangleGradientH(0, 0, (int)(W * 0.62f), H, Color{ 10, 11, 14, 235 }, Color{ 10, 11, 14, 0 });
	}

	// ------------------------------------------------------------------
	void Game::screenMainMenu() {
		float s = ui::s();
		int H = GetScreenHeight();
		float x = 90 * s, y = H * 0.12f;
		if (logo_.id) {
			// Logo (res/logo.png): fit into a 270x270 box at the top left.
			y = H * 0.03f;
			float sc = std::min(460 * s / logo_.width, 270 * s / logo_.height);
			DrawTextureEx(logo_, { x + 95 * s, y }, 0.0f, sc, WHITE);
			y += logo_.height * sc + 6 * s;
			ui::text("Co-op rally: one drives, one navigates.", x + 6 * s, y, 28 * s, ui::kText);
			y += 46 * s;
		}
		else {
			ui::text("2FAST2RALLY", x, y, 110 * s, ui::kAccent, ui::Left, true);
			ui::text("Co-op rally: one drives, one navigates.", x + 6 * s, y + 120 * s, 30 * s, ui::kText);
			y += 200 * s;
		}
		float bw = 460 * s, bh = 56 * s, gap = 10 * s;
		if (ui::button({ x, y, bw, bh }, "Solo")) openScreen(Screen::SoloSetup);
		y += bh + gap;
		if (ui::button({ x, y, bw, bh }, "Host Co-op")) openScreen(Screen::HostMenu);
		y += bh + gap;
		if (ui::button({ x, y, bw, bh }, "Join Co-op")) { joinStatus_.clear(); openScreen(Screen::JoinMenu); }
		y += bh + gap;
		if (ui::button({ x, y, bw, bh }, "Leaderboards")) { boardStage_ = stageIndex_; openScreen(Screen::Leaderboard); }
		y += bh + gap;
		if (ui::button({ x, y, bw, bh }, "Stage Editor")) editorOpen(stageIndex_);
		y += bh + gap;
		if (ui::button({ x, y, bw, bh }, "Settings")) { settingsReturn_ = Screen::MainMenu; openScreen(Screen::Settings); }
		y += bh + gap;
		if (ui::button({ x, y, bw * 0.49f, bh }, "How to Play")) openScreen(Screen::HowToPlay);
		if (ui::button({ x + bw * 0.51f, y, bw * 0.49f, bh }, "Credits")) openScreen(Screen::Credits);
		y += bh + gap;
		if (ui::button({ x, y, bw, bh }, "Quit")) quit_ = true;
		ui::text(std::string("v") + RALLY_VERSION + "  |  F11 fullscreen", x, H - 50 * s, 20 * s, ui::kTextDim);
	}

	void Game::screenSoloSetup() {
		float s = ui::s();
		int H = GetScreenHeight();
		float x = 90 * s, y = H * 0.1f, w = 640 * s, h = 56 * s, g = 12 * s;
		ui::text("SOLO", x, y, 70 * s, ui::kAccent, ui::Left, true);
		y += 100 * s;
		std::vector<std::string> stages, carNames;
		for (auto& d : stageDefs_) stages.push_back(d.name);
		for (auto& c : cars_) carNames.push_back(c.name);
		int mode = soloRally_ ? 1 : 0;
		if (ui::choice({ x, y, w, h }, "Mode", mode, { "Single stage", "Full rally (" + std::to_string(stageDefs_.size()) + " stages)" })) soloRally_ = mode == 1;
		y += h + g;
		if (!soloRally_) ui::choice({ x, y, w, h }, "Stage", stageIndex_, stages);
		else {
			ui::panel({ x, y, w, h }, Color{ 30, 33, 40, 200 });
			ui::text("All stages back to back, times added up", x + 16 * s, y + h * 0.28f, h * 0.42f, ui::kTextDim);
		}
		y += h + g;
		if (ui::choice({ x, y, w, h }, "Car", carIndex_, carNames)) menuCarRender_.build(cars_[carIndex_]);
		y += h + g;
		int wi = (int)weather_, ti = (int)timeOfDay_;
		if (ui::choice({ x, y, w, h }, "Weather", wi, weatherNames())) weather_ = (Weather)wi;
		y += h + g;
		if (ui::choice({ x, y, w, h }, "Time of day", ti, timeNames())) timeOfDay_ = (TimeOfDay)ti;
		y += h + g;
		int role = soloAsNavigator_ ? 1 : 0;
		if (ui::choice({ x, y, w, h }, "Role", role, { "Driver (AI co-driver)", "Navigator (AI driver)" })) soloAsNavigator_ = role == 1;
		y += h + g;
		ui::choice({ x, y, w, h }, "Transmission", settings().transmission, { "Automatic", "Sequential", "H-pattern + clutch" }); y += h + g * 2;

		if (!stageDefs_.empty()) {
			const StageDef& d = stageDefs_[std::clamp(stageIndex_, 0, (int)stageDefs_.size() - 1)];
			ui::text(d.description, x, y, 22 * s, ui::kTextDim);
			const auto& top = leaderboard_.top(d.id);
			std::string best = top.empty() ? "No time set yet" : "Best: " + ui::formatTime(top.front().time) + "  (" + top.front().driver + " / " + top.front().navigator + ")";
			ui::text(best, x, y + 30 * s, 22 * s, ui::kAccent);
			y += 80 * s;
		}
		if (!cars_.empty()) ui::text(cars_[carIndex_].description, x, y - 14 * s, 20 * s, ui::kTextDim);
		y += 24 * s;
		if (ui::button({ x, y, w * 0.6f, 66 * s }, soloRally_ ? "START RALLY" : "START STAGE")) {
			rally_ = RallyState{};
			int first = stageIndex_;
			if (soloRally_) { rally_.active = true; rally_.count = (int)stageDefs_.size(); first = 0; }
			startStage(first, carIndex_, weather_, timeOfDay_, soloAsNavigator_ ? Role::Navigator : Role::Driver, false);
			return;
		}
		if (ui::button({ x + w * 0.64f, y, w * 0.36f, 66 * s }, "Back") || ui::back()) openScreen(Screen::MainMenu);
	}

	void Game::screenHostMenu() {
		float s = ui::s();
		int H = GetScreenHeight();
		float x = 90 * s, y = H * 0.12f, w = 640 * s, h = 56 * s, g = 12 * s;
		ui::text("HOST CO-OP", x, y, 70 * s, ui::kAccent, ui::Left, true);
		y += 100 * s;
		ui::textField({ x, y, w, h }, "Your name", settings().playerName, 20); y += h + g;
		ui::textField({ x, y, w, h }, "UDP port", portText_, 5); y += h + g * 2;
		ui::text("Your partner joins using one of these addresses:", x, y, 22 * s, ui::kTextDim); y += 32 * s;
		static std::vector<std::string> ips = Session::localAddresses();
		for (auto& ip : ips) { ui::text(ip, x + 20 * s, y, 26 * s, ui::kText); y += 32 * s; }
		ui::text("Over the internet: forward the UDP port on your router,", x, y + 8 * s, 20 * s, ui::kTextDim);
		ui::text("or use a VPN like Tailscale / ZeroTier / WireGuard.", x, y + 34 * s, 20 * s, ui::kTextDim);
		y += 84 * s;
		if (ui::button({ x, y, w * 0.6f, 66 * s }, "Create Lobby")) {
			int port = std::atoi(portText_.c_str());
			if (port <= 0 || port > 65535) port = 27420;
			settings().port = port;
			settings().save(kSettingsPath);
			if (session_.host((uint16_t)port, settings().playerName)) {
				lobby_ = LobbyState{};
				lobby_.hostName = settings().playerName;
				lobby_.stage = stageIndex_;
				lobby_.car = carIndex_;
				lobby_.weather = (int)weather_;
				lobby_.timeOfDay = (int)timeOfDay_;
				openScreen(Screen::Lobby);
			}
			else joinStatus_ = "Could not open UDP port " + std::to_string(port) + " (already in use?)";
		}
		if (ui::button({ x + w * 0.64f, y, w * 0.36f, 66 * s }, "Back") || ui::back()) openScreen(Screen::MainMenu);
		if (!joinStatus_.empty()) ui::text(joinStatus_, x, y + 84 * s, 22 * s, ui::kBad);
	}

	void Game::screenJoinMenu() {
		float s = ui::s();
		int H = GetScreenHeight();
		float x = 90 * s, y = H * 0.12f, w = 640 * s, h = 56 * s, g = 12 * s;
		ui::text("JOIN CO-OP", x, y, 70 * s, ui::kAccent, ui::Left, true);
		y += 100 * s;
		ui::textField({ x, y, w, h }, "Your name", settings().playerName, 20); y += h + g;
		ui::textField({ x, y, w, h }, "Host address", joinAddress_, 64); y += h + g;
		ui::textField({ x, y, w, h }, "UDP port", portText_, 5); y += h + g * 2;
		bool connecting = session_.active() && !session_.partnerConnected();
		if (connecting) {
			connectTimer_ += GetFrameTime();
			if (connectTimer_ > 10.0f) { session_.stop(); joinStatus_ = "No answer from host. Check the address, port and firewall."; }
		}
		if (ui::button({ x, y, w * 0.6f, 66 * s }, connecting ? "Connecting..." : "Join", !connecting)) {
			int port = std::atoi(portText_.c_str());
			if (port <= 0 || port > 65535) port = 27420;
			settings().port = port;
			settings().lastJoinAddress = joinAddress_;
			settings().save(kSettingsPath);
			connectTimer_ = 0;
			if (session_.join(joinAddress_, (uint16_t)port, settings().playerName)) joinStatus_ = "Connecting to " + joinAddress_ + "...";
			else joinStatus_ = "Could not resolve " + joinAddress_;
		}
		if (ui::button({ x + w * 0.64f, y, w * 0.36f, 66 * s }, "Back") || ui::back()) { session_.stop(); openScreen(Screen::MainMenu); }
		if (!joinStatus_.empty()) ui::text(joinStatus_, x, y + 84 * s, 22 * s, ui::kTextDim);
	}

	// Starts the lobby's stage, or the current rally stage when a rally is running.
	void Game::hostStartStage() {
		if (lobby_.rally && !rally_.active) {
			rally_ = RallyState{};
			rally_.active = true;
			rally_.count = (int)stageDefs_.size();
		}
		if (!lobby_.rally) rally_ = RallyState{};
		int stage = rally_.active ? rally_.index : lobby_.stage;
		nlohmann::json j = { {"stage", stage}, {"car", lobby_.car}, {"weather", lobby_.weather},
			{"timeOfDay", lobby_.timeOfDay}, {"driverIsHost", lobby_.driverIsHost},
			{"rally", rally_.active}, {"rallyIndex", rally_.index}, {"rallyCount", rally_.count}, {"rallyTotal", rally_.total} };
		session_.sendStart(j);
		startStage(stage, lobby_.car, (Weather)lobby_.weather, (TimeOfDay)lobby_.timeOfDay,
			lobby_.driverIsHost ? Role::Driver : Role::Navigator, true);
	}

	void Game::screenLobby() {
		float s = ui::s();
		int H = GetScreenHeight();
		bool host = session_.isHost();
		float x = 90 * s, y = H * 0.08f, w = 680 * s, h = 54 * s, g = 10 * s;
		ui::text("CO-OP LOBBY", x, y, 70 * s, ui::kAccent, ui::Left, true);
		y += 96 * s;

		std::string hostN = lobby_.hostName, clientN = lobby_.clientConnected ? lobby_.clientName : "(waiting...)";
		std::string driverN = lobby_.driverIsHost ? hostN : clientN;
		std::string navN = lobby_.driverIsHost ? clientN : hostN;
		ui::panel({ x, y, w, 120 * s });
		ui::text("DRIVER", x + 20 * s, y + 18 * s, 22 * s, ui::kTextDim);
		ui::text(driverN, x + 20 * s, y + 46 * s, 34 * s, ui::kText);
		ui::text("NAVIGATOR", x + w * 0.52f, y + 18 * s, 22 * s, ui::kTextDim);
		ui::text(navN, x + w * 0.52f, y + 46 * s, 34 * s, ui::kText);
		bool hostReady = lobby_.hostReady, clientReady = lobby_.clientReady;
		ui::text(lobby_.driverIsHost ? (hostReady ? "READY" : "not ready") : (clientReady ? "READY" : "not ready"), x + 20 * s, y + 88 * s, 20 * s, (lobby_.driverIsHost ? hostReady : clientReady) ? ui::kGood : ui::kTextDim);
		ui::text(lobby_.driverIsHost ? (clientReady ? "READY" : "not ready") : (hostReady ? "READY" : "not ready"), x + w * 0.52f, y + 88 * s, 20 * s, (lobby_.driverIsHost ? clientReady : hostReady) ? ui::kGood : ui::kTextDim);
		y += 136 * s;

		std::vector<std::string> stages, carNames;
		for (auto& d : stageDefs_) stages.push_back(d.name);
		for (auto& c : cars_) carNames.push_back(c.name);
		if (host) {
			bool changed = false;
			changed |= ui::choice({ x, y, w, h }, "Stage", lobby_.stage, stages); y += h + g;
			if (ui::choice({ x, y, w, h }, "Car", lobby_.car, carNames)) { changed = true; menuCarRender_.build(cars_[lobby_.car]); }
			y += h + g;
			changed |= ui::choice({ x, y, w, h }, "Weather", lobby_.weather, weatherNames()); y += h + g;
			changed |= ui::choice({ x, y, w, h }, "Time of day", lobby_.timeOfDay, timeNames()); y += h + g;
			{
				int mode = lobby_.rally ? 1 : 0;
				if (ui::choice({ x, y, w, h }, "Mode", mode, { "Single stage", "Full rally (" + std::to_string(stageDefs_.size()) + " stages)" })) { lobby_.rally = mode == 1; changed = true; }
				y += h + g;
			}
			if (ui::button({ x, y, w, h }, "Swap Driver / Navigator")) { lobby_.driverIsHost = !lobby_.driverIsHost; changed = true; }
			y += h + g;
			if (changed) { lobby_.hostReady = false; lobby_.clientReady = false; lobbyChanged(); }
		}
		else {
			auto line = [&](const std::string& a, const std::string& b) {
				ui::text(a, x + 16 * s, y + 12 * s, 24 * s, ui::kTextDim);
				ui::text(b, x + w - 16 * s, y + 12 * s, 24 * s, ui::kText, ui::Right);
				y += h + g;
			};
			line("Stage", lobby_.stage < (int)stages.size() ? stages[lobby_.stage] : "?");
			line("Car", lobby_.car < (int)carNames.size() ? carNames[lobby_.car] : "?");
			line("Weather", weatherNames()[std::clamp(lobby_.weather, 0, 2)]);
			line("Time of day", timeNames()[std::clamp(lobby_.timeOfDay, 0, 2)]);
			line("Mode", lobby_.rally ? "Full rally" : "Single stage");
			ui::text("The host picks the stage, car and roles.", x + 16 * s, y + 8 * s, 20 * s, ui::kTextDim);
			y += h + g;
		}

		bool& myReady = host ? lobby_.hostReady : lobby_.clientReady;
		bool before = myReady;
		ui::toggle({ x, y, w, h }, "I'm ready", myReady);
		if (myReady != before) {
			if (host) lobbyChanged();
			else session_.sendLobbyRequest({ {"ready", myReady} });
		}
		y += h + g * 2;

		if (host) {
			bool canStart = lobby_.clientConnected && lobby_.hostReady && lobby_.clientReady;
			if (ui::button({ x, y, w * 0.6f, 66 * s }, "START STAGE", canStart) || (auto_.netHost && canStart)) { hostStartStage(); return; }
			if (!lobby_.clientConnected) {
				static std::vector<std::string> ips = Session::localAddresses();
				std::string tip = "Waiting for partner. Share: ";
				for (size_t i = 0; i < ips.size() && i < 2; ++i) tip += (i ? ", " : "") + ips[i];
				tip += " port " + std::to_string(session_.port());
				ui::text(tip, x, y + 80 * s, 22 * s, ui::kTextDim);
			}
		}
		else ui::text(lobby_.hostReady && lobby_.clientReady ? "Waiting for host to start..." : "Get ready!", x, y + 20 * s, 24 * s, ui::kTextDim);
		if (ui::button({ x + w * 0.64f, y, w * 0.36f, 66 * s }, "Leave") || ui::back()) {
			session_.stop();
			lobby_ = LobbyState{};
			openScreen(Screen::MainMenu);
		}
		if (session_.partnerConnected()) {
			char buf[64];
			std::snprintf(buf, sizeof buf, "Ping %u ms", session_.ping());
			ui::text(buf, x, H - 50 * s, 20 * s, ui::kTextDim);
		}
	}

	// ------------------------------------------------------------------
	void Game::screenSettings() {
		Settings& cfg = settings();
		float s = ui::s();
		int H = GetScreenHeight();
		float x = 70 * s, y = H * 0.06f, w = 760 * s, h = 46 * s, g = 8 * s;
		DrawRectangle(0, 0, GetScreenWidth(), H, Color{ 10, 11, 14, 190 });
		ui::text("SETTINGS", x, y, 64 * s, ui::kAccent, ui::Left, true);
		y += 86 * s;
		const char* tabs[] = { "Graphics", "Audio", "Controls", "Gameplay" };
		float tw = (w - 30 * s) / 4;
		for (int i = 0; i < 4; ++i) {
			Rectangle r = { x + i * (tw + 10 * s), y, tw, h };
			if (ui::button(r, tabs[i])) { settingsTab_ = i; rebindAction_ = -1; }
			if (settingsTab_ == i) DrawRectangle((int)r.x, (int)(r.y + r.height - 4 * s), (int)r.width, (int)(4 * s), ui::kAccent);
		}
		y += h + 20 * s;

		bool video = false;
		switch (settingsTab_) {
		case 0: {
			video |= ui::toggle({ x, y, w, h }, "Fullscreen (F11)", cfg.fullscreen); y += h + g;
			video |= ui::toggle({ x, y, w, h }, "V-Sync", cfg.vsync); y += h + g;
			ui::toggle({ x, y, w, h }, "4x MSAA (restart to apply)", cfg.msaa); y += h + g;
			ui::slider({ x, y, w, h }, "Field of view", cfg.fov, 55, 100, "%.0f", 1.0f); y += h + g;
			ui::slider({ x, y, w, h }, "Draw distance", cfg.drawDistance, 200, 900, "%.0f m", 1.0f); y += h + g;
			ui::choice({ x, y, w, h }, "Particles", cfg.particleQuality, { "Off", "Low", "High" }); y += h + g;
			ui::toggle({ x, y, w, h }, "Show FPS", cfg.showFps); y += h + g;
			break;
		}
		case 1: {
			if (ui::slider({ x, y, w, h }, "Master volume", cfg.masterVolume, 0, 1)) SetMasterVolume(cfg.masterVolume);
			y += h + g;
			ui::slider({ x, y, w, h }, "Engine", cfg.engineVolume, 0, 1); y += h + g;
			ui::slider({ x, y, w, h }, "Effects (surface, wind, impacts)", cfg.effectsVolume, 0, 1); y += h + g;
			ui::slider({ x, y, w, h }, "Co-driver voice", cfg.voiceVolume, 0, 1); y += h + g;
			ui::slider({ x, y, w, h }, "Menu music", cfg.musicVolume, 0, 1); y += h + g;
			if (ui::button({ x, y, w * 0.5f, h }, "Test voice")) voice_.say({ "left", "4", "tightens", "into", "right", "2", "dontcut" }, true);
			y += h + g;
			break;
		}
		case 2: {
			// Rebinding capture.
			if (rebindAction_ >= 0) {
				if (rebindKind_ == 3) {
					AxisBinding b;
					if (input::pollAxisCapture(b)) {
						AxisBinding* target = rebindAxis_ == 0 ? &cfg.steerAxis : rebindAxis_ == 1 ? &cfg.throttleAxis : rebindAxis_ == 2 ? &cfg.brakeAxis : &cfg.clutchAxis;
						*target = b;
						rebindAction_ = -1;
					}
					if (IsKeyPressed(KEY_ESCAPE)) rebindAction_ = -1;
				}
				else if (rebindKind_ == 2) {
					// Gamepad button (raylib) or any wheel/shifter button (DirectInput).
					int b = input::pollAnyPadButton();
					int jb = input::pollAnyJoyButton();
					if (b > 0) { cfg.pad[rebindAction_] = b; cfg.joy[rebindAction_] = 0; rebindAction_ = -1; }
					else if (jb > 0) { cfg.joy[rebindAction_] = jb; cfg.pad[rebindAction_] = 0; rebindAction_ = -1; }
					else if (IsKeyPressed(KEY_DELETE)) { cfg.pad[rebindAction_] = 0; cfg.joy[rebindAction_] = 0; rebindAction_ = -1; }
					else if (IsKeyPressed(KEY_ESCAPE)) rebindAction_ = -1;
				}
				else {
					int k = input::pollAnyKey();
					if (k > 0) {
						if (k == KEY_DELETE) k = 0;
						(rebindKind_ == 0 ? cfg.keys : cfg.keysAlt)[rebindAction_] = k;
						rebindAction_ = -1;
					}
				}
			}
			float rowH = 34 * s;
			float c1 = x + w * 0.42f, cw = w * 0.19f;
			ui::text("Action", x, y, 20 * s, ui::kTextDim);
			ui::text("Key", c1, y, 20 * s, ui::kTextDim);
			ui::text("Alt key", c1 + cw + 6 * s, y, 20 * s, ui::kTextDim);
			ui::text("Gamepad", c1 + 2 * (cw + 6 * s), y, 20 * s, ui::kTextDim);
			y += 28 * s;
			for (int a = 0; a < (int)Action::Count; ++a) {
				ui::text(actionName((Action)a), x, y + 6 * s, 21 * s, ui::kText);
				for (int kind = 0; kind < 3; ++kind) {
					Rectangle r = { c1 + kind * (cw + 6 * s), y, cw, rowH - 4 * s };
					bool waiting = rebindAction_ == a && rebindKind_ == kind;
					std::string label = waiting ? "press..." : kind == 0 ? keyName(cfg.keys[a]) : kind == 1 ? keyName(cfg.keysAlt[a]) : (cfg.joy[a] > 0 ? input::joyButtonName(cfg.joy[a]) : padName(cfg.pad[a]));
					if (ui::button(r, label)) { rebindAction_ = a; rebindKind_ = kind; }
				}
				y += rowH;
			}
			// --- Right column: gamepad / wheel ---
			float x2 = x + w + 50 * s;
			float w2 = std::min(w, GetScreenWidth() - x2 - 40 * s);
			float y2 = H * 0.06f + 86 * s + h + 20 * s;
			float c2 = x2 + w2 * 0.36f, cw2 = w2 * 0.2f;
			std::string pad = IsGamepadAvailable(cfg.gamepadIndex) ? std::string("Gamepad: ") + GetGamepadName(cfg.gamepadIndex) : "Gamepad: none";
			ui::text(pad, x2, y2, 20 * s, ui::kTextDim); y2 += 26 * s;
			for (int d = 0; d < joy::deviceCount(); ++d) { ui::text("Dev" + std::to_string(d + 1) + ": " + joy::deviceName(d), x2, y2, 18 * s, ui::kTextDim); y2 += 22 * s; }
			if (joy::deviceCount() == 0) { ui::text("No wheels / pedals / shifters found (plug in, then restart)", x2, y2, 18 * s, ui::kTextDim); y2 += 22 * s; }
			y2 += 4 * s;
			ui::choice({ x2, y2, w2, h * 0.85f }, "Controller slot", cfg.gamepadIndex, { "1", "2", "3", "4" }); y2 += h;
			const char* axisLabels[] = { "Steering axis", "Throttle axis", "Brake axis", "Clutch axis" };
			AxisBinding* axes[] = { &cfg.steerAxis, &cfg.throttleAxis, &cfg.brakeAxis, &cfg.clutchAxis };
			for (int i = 0; i < 4; ++i) {
				ui::text(axisLabels[i], x2, y2 + 6 * s, 21 * s, ui::kText);
				bool waiting = rebindAction_ == 100 + i;
				if (ui::button({ c2, y2, cw2 * 1.4f, rowH - 4 * s }, waiting ? "move it..." : axisName(*axes[i]))) {
					rebindAction_ = 100 + i; rebindKind_ = 3; rebindAxis_ = i; input::beginAxisCapture();
				}
				if (ui::button({ c2 + cw2 * 1.5f, y2, cw2 * 0.7f, rowH - 4 * s }, axes[i]->invert ? "Inverted" : "Invert")) axes[i]->invert = !axes[i]->invert;
				if (ui::button({ c2 + cw2 * 2.3f, y2, cw2 * 0.85f, rowH - 4 * s }, axes[i]->mode == 0 ? "Centered" : "Pedal")) axes[i]->mode = axes[i]->mode == 0 ? 1 : 0;
				y2 += rowH;
			}
			y2 += 6 * s;
			ui::slider({ x2, y2, w2, h * 0.85f }, "Steering deadzone", cfg.steerDeadzone, 0, 0.3f); y2 += h;
			ui::slider({ x2, y2, w2, h * 0.85f }, "Steering linearity", cfg.steerLinearity, 1, 2.5f, "%.2f", 1.0f); y2 += h;
			ui::slider({ x2, y2, w2, h * 0.85f }, "Keyboard steering speed", cfg.keyboardSteerSpeed, 1, 8, "%.1f", 1.0f); y2 += h;
			if (ui::slider({ x2, y2, w2, h * 0.85f }, "Wheel rotation (match your wheel)", cfg.wheelRotation, 180, 1080, "%.0f deg", 1.0f)) cfg.wheelRotation = std::round(cfg.wheelRotation / 10.0f) * 10.0f;
			y2 += h + 8 * s;
			ui::text(ffb::wheelAvailable() ? std::string("Force feedback: ") + ffb::wheelName() : "Force feedback: no FFB wheel found", x2, y2, 20 * s, ffb::wheelAvailable() ? ui::kGood : ui::kTextDim);
			y2 += 30 * s;
			ui::slider({ x2, y2, w2, h * 0.85f }, "Force feedback strength", cfg.ffbStrength, 0, 1); y2 += h;
			ui::toggle({ x2, y2, w2, h * 0.85f }, "Invert force feedback", cfg.ffbInvert); y2 += h;
			ui::toggle({ x2, y2, w2, h * 0.85f }, "Gamepad rumble", cfg.rumble); y2 += h;
			if (ui::button({ x2, y2, w2 * 0.45f, h * 0.85f }, "Reset controls")) cfg.resetControls();
			break;
		}
		case 3: {
			ui::textField({ x, y, w, h }, "Player name", cfg.playerName, 20); y += h + g;
			ui::toggle({ x, y, w, h }, "Show speed in mph", cfg.useMph); y += h + g;
			ui::choice({ x, y, w, h }, "Transmission", cfg.transmission, { "Automatic", "Sequential (Q/E)", "H-pattern + clutch" }); y += h + g;
			ui::toggle({ x, y, w, h }, "ABS", cfg.abs); y += h + g;
			ui::toggle({ x, y, w, h }, "Traction control", cfg.tcs); y += h + g;
			ui::toggle({ x, y, w, h }, "Anti-flip assist", cfg.antiFlip); y += h + g;
			ui::toggle({ x, y, w, h }, "AI co-driver voice", cfg.aiCodriverVoice); y += h + g;
			ui::choice({ x, y, w, h }, "Default camera", cfg.defaultCamera, { "Chase", "Near chase", "Bonnet", "Cockpit" }); y += h + g;
			break;
		}
		}
		if (video) applyVideoSettings();

		float by = H - 100 * s;
		if (ui::button({ x, by, 300 * s, 60 * s }, "Save & Back") || (ui::back() && rebindAction_ < 0)) {
			cfg.save(kSettingsPath);
			cfg.manualGearbox = cfg.transmission != 0;
			openScreen(settingsReturn_ == Screen::Stage ? Screen::MainMenu : settingsReturn_);
		}
	}

	void Game::screenLeaderboard() {
		float s = ui::s();
		int H = GetScreenHeight();
		float x = 90 * s, y = H * 0.08f, w = 1000 * s;
		ui::text("LEADERBOARDS", x, y, 70 * s, ui::kAccent, ui::Left, true);
		y += 100 * s;
		std::vector<std::string> stages;
		for (auto& d : stageDefs_) stages.push_back(d.name);
		stages.push_back("Full Rally (all stages)");
		ui::choice({ x, y, w * 0.6f, 54 * s }, "Stage", boardStage_, stages);
		y += 74 * s;
		if (!stageDefs_.empty()) {
			bool rallyBoard = boardStage_ >= (int)stageDefs_.size();
			const auto& top = leaderboard_.top(rallyBoard ? std::string("rally_all") : stageDefs_[std::clamp(boardStage_, 0, (int)stageDefs_.size() - 1)].id);
			float cols[] = { 0, 60, 230, 430, 630, 860 };
			const char* heads[] = { "#", "Time", "Driver", "Navigator", "Car", "Date" };
			for (int i = 0; i < 6; ++i) ui::text(heads[i], x + cols[i] * s, y, 22 * s, ui::kTextDim);
			y += 36 * s;
			if (top.empty()) ui::text("No times yet - go set one!", x, y, 26 * s, ui::kText);
			for (size_t i = 0; i < top.size(); ++i) {
				const RunRecord& r = top[i];
				Color c = i == 0 ? ui::kAccent : ui::kText;
				ui::text(std::to_string(i + 1), x + cols[0] * s, y, 26 * s, c);
				ui::text(ui::formatTime(r.time) + (r.penalties > 0 ? "*" : ""), x + cols[1] * s, y, 26 * s, c);
				ui::text(r.driver, x + cols[2] * s, y, 24 * s, ui::kText);
				ui::text(r.navigator, x + cols[3] * s, y, 24 * s, ui::kText);
				ui::text(r.car, x + cols[4] * s, y, 22 * s, ui::kTextDim);
				ui::text(r.date, x + cols[5] * s, y, 22 * s, ui::kTextDim);
				y += 40 * s;
			}
			ui::text("* includes penalties", x, H - 160 * s, 20 * s, ui::kTextDim);
		}
		if (ui::button({ x, H - 110 * s, 300 * s, 60 * s }, "Back") || ui::back()) openScreen(Screen::MainMenu);
	}

	// ------------------------------------------------------------------
	void Game::screenHowToPlay() {
		float s = ui::s();
		int W = GetScreenWidth(), H = GetScreenHeight();
		DrawRectangle(0, 0, W, H, Color{ 10, 11, 14, 200 });
		float x = 70 * s, y = 40 * s;
		ui::text("HOW TO PLAY", x, y, 64 * s, ui::kAccent, ui::Left, true);
		y += 84 * s;
		float colW = W * 0.5f - 90 * s;
		float lx = x, rx = W * 0.5f + 20 * s;
		float ly = y, ry = y;
		auto head = [&](float cx, float& cy, const char* t) { ui::text(t, cx, cy, 30 * s, ui::kAccent); cy += 40 * s; };
		auto line = [&](float cx, float& cy, const std::string& t, Color c = ui::kText) { ui::text(t, cx, cy, 22 * s, c); cy += 30 * s; };
		(void)colW;

		head(lx, ly, "ONE DRIVES, ONE NAVIGATES");
		line(lx, ly, "The navigator has the pace notes and the map.", ui::kTextDim);
		line(lx, ly, "The driver only sees the notes the navigator calls.", ui::kTextDim);
		line(lx, ly, "Talk to each other - or use the built-in co-driver voice.", ui::kTextDim);
		ly += 12 * s;
		head(lx, ly, "DRIVER");
		line(lx, ly, "W / S  throttle, brake & reverse     A / D  steer");
		line(lx, ly, "Space  handbrake     R  reset to road (+5 s)");
		line(lx, ly, "C  camera     B  look back     Esc  pause");
		line(lx, ly, "Gamepad / wheel: triggers or pedals, stick or wheel.");
		line(lx, ly, "H-pattern: hold Shift (clutch) + 1-6, 0 = R, N = neutral, I = start");
		ly += 12 * s;
		head(lx, ly, "NAVIGATOR");
		line(lx, ly, "Space (or A button, or click)  call the next note");
		line(lx, ly, "Tab  switch view: seat / chase / map / pace book");
		line(lx, ly, "Call a note 2-6 seconds before the corner:");
		line(lx, ly, "the list shows  wait  /  CALL NOW  /  LATE!");
		line(lx, ly, "Split times and deltas are yours to tell the driver.");

		head(rx, ry, "READING PACE NOTES");
		PaceNote ex[4];
		ex[0].direction = -1; ex[0].severity = 4;
		ex[1].direction = 1; ex[1].severity = 2; ex[1].mods = MOD_TIGHTENS; ex[1].link = "into";
		ex[2].direction = -1; ex[2].severity = 0; ex[2].mods = MOD_DONTCUT;
		ex[3].type = NoteType::Crest; ex[3].link = "100";
		for (int i = 0; i < 4; ++i) {
			Rectangle r = { rx, ry + i * 74 * s, 120 * s, 68 * s };
			drawNoteIcon(ex[i], r, 1.0f, false);
			ui::text(noteText(ex[i]), r.x + r.width + 16 * s, r.y + 22 * s, 24 * s, ui::kText);
		}
		ry += 4 * 74 * s + 10 * s;
		line(rx, ry, "Numbers: 1 = slowest ... 6 = nearly flat out.");
		line(rx, ry, "Hairpin and square = slower than a 1.");
		line(rx, ry, "tightens / opens: the corner gets sharper / wider.");
		line(rx, ry, "don't cut: rocks on the inside.   caution: danger.");
		line(rx, ry, "Links: into = straight after, and = close, 50/70/100 = metres.");
		ry += 12 * s;
		head(rx, ry, "RULES");
		line(rx, ry, "Reset or roll over: +5 s.   Cut a corner: +2 s.");
		line(rx, ry, "Rally mode: every stage back to back, times added up.");

		if (ui::button({ x, H - 100 * s, 340 * s, 62 * s }, firstRun_ ? "Let's go!" : "Back") || ui::back()) {
			firstRun_ = false;
			settings().save(kSettingsPath);
			openScreen(Screen::MainMenu);
		}
	}

	void Game::screenCredits() {
		float s = ui::s();
		int W = GetScreenWidth(), H = GetScreenHeight();
		DrawRectangle(0, 0, W, H, Color{ 10, 11, 14, 170 });
		float x = 90 * s, y = 60 * s;
		ui::text("CREDITS", x, y, 64 * s, ui::kAccent, ui::Left, true);
		y += 100 * s;
		auto item = [&](const char* a, const char* b) {
			ui::text(a, x, y, 26 * s, ui::kText);
			ui::text(b, x + 20 * s, y + 32 * s, 21 * s, ui::kTextDim);
			y += 74 * s;
		};
		item("2Fast2Rally", "Christian Larsen - game design, logo and the Prairie engine it is built on.");
		item("Prairie engine", "Rigid-body physics, ENet networking, tree/rock/grass models and textures, menu music.");
		item("raylib 6.0", "Ramon Santamaria and contributors - zlib license.");
		item("ENet", "Lee Salzman - MIT license.");
		item("JSON for Modern C++", "Niels Lohmann - MIT license.");
		item("Cherry Bomb One font", "Satsuyako - SIL Open Font License.");
		item("Co-driver voice", "Generated with Windows text-to-speech (tools/genvoice.ps1).");
		item("Engine, surface and weather sounds", "Synthesised in real time - no samples.");
		ui::text(std::string("Version ") + RALLY_VERSION, x, H - 150 * s, 22 * s, ui::kTextDim);
		if (ui::button({ x, H - 110 * s, 300 * s, 60 * s }, "Back") || ui::back()) openScreen(Screen::MainMenu);
		(void)W;
	}
}
