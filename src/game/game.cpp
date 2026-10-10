#include "game/game.h"
#include "game/stageGen.h"
#include "core/input.h"
#include "core/ui.h"
#include "core/log.h"
#include <raymath.h>
#include <rlgl.h>
#include <filesystem>
#include <algorithm>
#include <cmath>

namespace rally {
	bool Game::init(const AutoOptions& opts) {
		auto_ = opts;
		Settings& cfg = settings();
		cfg.load(kSettingsPath);
		if (auto_.selfTest || auto_.menuShot || auto_.netHost || auto_.netJoin) {
			cfg.fullscreen = false;
			cfg.windowW = 1280; cfg.windowH = 720;
		}

		unsigned flags = FLAG_WINDOW_RESIZABLE;
		if (cfg.vsync) flags |= FLAG_VSYNC_HINT;
		if (cfg.msaa) flags |= FLAG_MSAA_4X_HINT;
		SetConfigFlags(flags);
		InitWindow(cfg.windowW, cfg.windowH, "2Fast2Rally");
		if (!IsWindowReady()) { log::error("window creation failed"); return false; }
		SetExitKey(KEY_NULL);
		SetWindowMinSize(960, 540);
		if (cfg.fullscreen) {
			int m = GetCurrentMonitor();
			SetWindowSize(GetMonitorWidth(m), GetMonitorHeight(m));
			ToggleFullscreen();
		}
		if (!cfg.vsync) SetTargetFPS(240);
		rlSetClipPlanes(0.08, 5000.0);

		InitAudioDevice();
		SetMasterVolume(cfg.masterVolume);
		ui::init();
		if (!world_.init()) return false;
		synth::init();
		synth::setPaused(true);
		voice_.load("res/audio/notes");

		// Stages: generate any missing defaults (into the source tree too when
		// running from build/, so the generated layouts can be committed).
		std::string stageDir = "res/data/stages";
		if (auto_.genStages) {
			std::error_code ec;
			for (auto& e : std::filesystem::directory_iterator(stageDir, ec)) std::filesystem::remove(e.path(), ec);
		}
		ensureDefaultStages(stageDir);
		if (std::filesystem::exists("../res/data")) {
			if (auto_.genStages) {
				std::error_code ec;
				for (auto& e : std::filesystem::directory_iterator("../res/data/stages", ec)) std::filesystem::remove(e.path(), ec);
			}
			ensureDefaultStages("../res/data/stages");
		}
		loadContent();
		leaderboard_.load("saves/leaderboard.json");

		if (FileExists("res/audio/music/menu.mp3")) {
			menuMusic_ = LoadMusicStream("res/audio/music/menu.mp3");
			musicLoaded_ = menuMusic_.frameCount > 0;
			if (musicLoaded_) { SetMusicVolume(menuMusic_, cfg.musicVolume); PlayMusicStream(menuMusic_); }
		}
		if (!cars_.empty()) menuCarRender_.build(cars_[0]);
		hostName_ = cfg.playerName;
		joinAddress_ = cfg.lastJoinAddress;
		portText_ = std::to_string(cfg.port);

		stageIndex_ = std::clamp(auto_.stage, 0, std::max(0, (int)stageDefs_.size() - 1));
		carIndex_ = std::clamp(auto_.car, 0, std::max(0, (int)cars_.size() - 1));
		weather_ = (Weather)std::clamp(auto_.weather, 0, 2);
		timeOfDay_ = (TimeOfDay)std::clamp(auto_.timeOfDay, 0, 1);

		if (auto_.selfTest) {
			startStage(stageIndex_, carIndex_, weather_, timeOfDay_, auto_.asNavigator ? Role::Navigator : Role::Driver, false);
			run_.aiDriver = true;
		}
		else if (auto_.netHost) {
			session_.host((uint16_t)cfg.port, "HostBot");
			lobby_ = LobbyState{};
			lobby_.hostName = "HostBot";
			lobby_.stage = stageIndex_;
			lobby_.car = carIndex_;
			lobby_.hostReady = true;
			openScreen(Screen::Lobby);
		}
		else if (auto_.netJoin) {
			session_.join("127.0.0.1", (uint16_t)cfg.port, "NavBot");
			joinStatus_ = "Connecting...";
			openScreen(Screen::JoinMenu);
		}
		log::info("2Fast2Rally %s ready (%d stages, %d cars)", RALLY_VERSION, (int)stageDefs_.size(), (int)cars_.size());
		return true;
	}

	void Game::loadContent() {
		stageDefs_ = loadAllStages("res/data/stages", &stagePaths_);
		reloadCars();
	}

	void Game::reloadCars() {
		std::vector<CarSpec> c;
		if (loadCarSpecs("res/data/cars.json", c)) cars_ = c;
		if (cars_.empty()) cars_.push_back(CarSpec{});
	}

	std::string Game::stagePath(int i) const {
		return (i >= 0 && i < (int)stagePaths_.size()) ? stagePaths_[i] : std::string();
	}

	void Game::shutdown() {
		endStage();
		session_.stop();
		settings().save(kSettingsPath);
		menuCarRender_.unload();
		carRender_.unload();
		if (musicLoaded_) UnloadMusicStream(menuMusic_);
		voice_.unload();
		synth::shutdown();
		world_.shutdown();
		ui::shutdown();
		CloseAudioDevice();
		CloseWindow();
	}

	void Game::run() {
		while (!quit_ && !WindowShouldClose()) {
			float dt = std::min(GetFrameTime(), 0.1f);
			frame(dt);
		}
	}

	void Game::applyVideoSettings() {
		Settings& cfg = settings();
		if (cfg.fullscreen != IsWindowFullscreen()) {
			if (cfg.fullscreen) {
				int m = GetCurrentMonitor();
				SetWindowSize(GetMonitorWidth(m), GetMonitorHeight(m));
				ToggleFullscreen();
			}
			else {
				ToggleFullscreen();
				SetWindowSize(cfg.windowW, cfg.windowH);
			}
		}
		if (cfg.vsync) { SetWindowState(FLAG_VSYNC_HINT); SetTargetFPS(0); }
		else { ClearWindowState(FLAG_VSYNC_HINT); SetTargetFPS(240); }
		SetMasterVolume(cfg.masterVolume);
	}

	void Game::takeScreenshot(const std::string& name) {
		rlDrawRenderBatchActive(); // flush the 2D UI batch first
		TakeScreenshot(name.c_str());
		log::info("screenshot: %s", name.c_str());
	}

	void Game::openScreen(Screen s) {
		screen_ = s;
		ui::resetFocus();
	}

	void Game::frame(float dt) {
		Settings& cfg = settings();
		if (!ui::typing()) input::update(dt);
		pollNetwork();

		bool inStage = screen_ == Screen::Stage;
		if (musicLoaded_) {
			SetMusicVolume(menuMusic_, cfg.musicVolume * (inStage || screen_ == Screen::Editor ? 0.0f : 1.0f));
			UpdateMusicStream(menuMusic_);
		}
		voice_.setVolume(cfg.voiceVolume);
		voice_.update(dt);
		if (!inStage) synth::setPaused(true);
		if (IsKeyPressed(KEY_F11)) { cfg.fullscreen = !cfg.fullscreen; applyVideoSettings(); }

		BeginDrawing();
		ClearBackground(BLACK);
		ui::beginFrame();
		switch (screen_) {
		case Screen::Stage: stageFrame(dt); break;
		case Screen::Editor: editorFrame(dt); break;
		default:
			drawMenuBackground(dt);
			switch (screen_) {
			case Screen::MainMenu: screenMainMenu(); break;
			case Screen::SoloSetup: screenSoloSetup(); break;
			case Screen::HostMenu: screenHostMenu(); break;
			case Screen::JoinMenu: screenJoinMenu(); break;
			case Screen::Lobby: screenLobby(); break;
			case Screen::Settings: screenSettings(); break;
			case Screen::Leaderboard: screenLeaderboard(); break;
			default: break;
			}
			break;
		}
		if (cfg.showFps) DrawFPS(GetScreenWidth() - 90, 8);

		// --- Automation ---
		autoTimer_ += dt;
		if (screen_ == Screen::Stage && run_.phase == RunPhase::Running) {
			fpsFrames_ += 1; fpsTime_ += GetFrameTime();
			float inst = 1.0f / std::max(1e-4f, GetFrameTime());
			if (fpsTime_ > 2.0) fpsMin_ = std::min(fpsMin_, (double)inst);
		}
		if (auto_.menuShot && autoTimer_ > 1.5f && autoShots_ == 0) {
			takeScreenshot(auto_.shotPrefix + "_menu.png");
			++autoShots_;
			quit_ = true;
		}
		if (auto_.selfTest && screen_ == Screen::Stage) {
			if (run_.phase == RunPhase::Running && autoShots_ < 6 && run_.stageTime > 4.0 + autoShots_ * 18.0) {
				static const int cams[] = { 0, 3, 1, 2, 0, 0 };
				takeScreenshot(auto_.shotPrefix + "_" + std::to_string(autoShots_) + ".png");
				++autoShots_;
				camMode_ = cams[std::min(autoShots_, 5)];
				navView_ = autoShots_ % 4;
			}
			if (autoTimer_ > autoNextLog_) {
				autoNextLog_ += 5.0f;
				Vector3 p = car_.position();
				log::info("selftest: t=%.1f phase=%d s=%.0f/%.0f lat=%.1f speed=%.1f km/h gear=%d rpm=%.0f resets=%d pos=(%.0f,%.1f,%.0f) up=%.2f",
					run_.stageTime, (int)run_.phase, run_.progress, stage_.finishS, run_.lateral, car_.speed() * 3.6f, car_.gear(), car_.rpm(), run_.resets, p.x, p.y, p.z, car_.up().y);
			}
			if ((run_.phase == RunPhase::Finished && run_.finishedFor > 3.0f) || autoTimer_ > auto_.timeLimit) {
				if (run_.phase == RunPhase::Finished) takeScreenshot(auto_.shotPrefix + "_results.png");
				log::info("selftest: done finished=%d time=%.2f penalties=%.0f resets=%d cuts=%d notes=%d good=%d avgFps=%.0f minFps=%.0f",
					run_.phase == RunPhase::Finished ? 1 : 0, run_.finishTime, run_.penalties, run_.resets, run_.cuts, (int)notes_.size(), run_.goodCalls,
					fpsFrames_ / std::max(0.001, fpsTime_), fpsMin_);
				quit_ = true;
			}
		}
		if ((auto_.netHost || auto_.netJoin) && screen_ == Screen::Stage) {
			if (run_.phase == RunPhase::Running && autoShots_ < 2 && run_.stageTime > 10.0 + autoShots_ * 25.0) {
				takeScreenshot(auto_.shotPrefix + (auto_.netHost ? "_host_" : "_join_") + std::to_string(autoShots_) + ".png");
				++autoShots_;
				if (auto_.netJoin) navView_ = (navView_ + 2) % 4;
			}
			if (autoTimer_ > auto_.timeLimit || (run_.phase == RunPhase::Finished && run_.finishedFor > 4.0f)) {
				if (run_.phase == RunPhase::Finished) takeScreenshot(auto_.shotPrefix + (auto_.netHost ? "_host_results.png" : "_join_results.png"));
				log::info("nettest: %s done phase=%d time=%.2f progress=%.0f good=%d late=%d early=%d missed=%d",
					auto_.netHost ? "host" : "join", (int)run_.phase, run_.finishTime, run_.progress, run_.goodCalls, run_.lateCalls, run_.earlyCalls, run_.missedCalls);
				quit_ = true;
			}
		}
		if ((auto_.netHost || auto_.netJoin) && autoTimer_ > auto_.timeLimit + 30) quit_ = true;

		EndDrawing();
	}

	void Game::pollNetwork() {
		if (!session_.active()) return;
		std::vector<NetEvent> evs;
		session_.poll(evs);
		for (const NetEvent& e : evs) handleNetEvent(e);
	}

	void Game::lobbyChanged() {
		if (session_.isHost()) session_.sendLobby(lobby_);
	}

	void Game::handleNetEvent(const NetEvent& e) {
		bool host = session_.isHost();
		switch (e.kind) {
		case NetEvent::Connected:
			if (host) {
				lobby_.clientName = e.text;
				lobby_.clientConnected = true;
				lobby_.clientReady = false;
				lobbyChanged();
				if (screen_ == Screen::Stage && run_.online) {
					// Partner rejoined mid-stage: send them straight back in.
					nlohmann::json j = { {"stage", stageIndex_}, {"car", carIndex_}, {"weather", (int)weather_},
						{"timeOfDay", (int)timeOfDay_}, {"driverIsHost", lobby_.driverIsHost} };
					const CarVisual& v = displayCar();
					j["resume"] = { {"time", run_.stageTime}, {"progress", run_.progress}, {"penalties", run_.penalties},
						{"resets", run_.resets}, {"splitIndex", run_.splitIndex}, {"split0", run_.splitTimes[0]}, {"split1", run_.splitTimes[1]},
						{"pos", {v.position.x, v.position.y, v.position.z}}, {"rot", {v.rotation.x, v.rotation.y, v.rotation.z, v.rotation.w}},
						{"vel", {v.velocity.x, v.velocity.y, v.velocity.z}} };
					session_.sendStart(j);
					run_.partnerLost = false;
					run_.partnerLoaded = false;
					if (run_.role == Role::Driver) { run_.phase = RunPhase::WaitingPartner; run_.waitTimer = 0; }
					run_.paused = false;
					setMessage(e.text + " rejoined", ui::kGood);
				}
				if (auto_.netHost) lobby_.hostReady = true;
			}
			else {
				joinStatus_ = "Connected to " + e.text;
				if (screen_ == Screen::JoinMenu) openScreen(Screen::Lobby);
			}
			break;
		case NetEvent::Disconnected:
			if (host) {
				lobby_.clientConnected = false;
				lobby_.clientName.clear();
				lobby_.clientReady = false;
			}
			if (screen_ == Screen::Stage && run_.online) {
				run_.partnerLost = true;
				run_.paused = run_.phase == RunPhase::Running || run_.phase == RunPhase::Countdown;
				setMessage("Partner disconnected", ui::kBad, 4.0f);
			}
			else if (!host) {
				joinStatus_ = "Disconnected from host.";
				if (screen_ == Screen::Lobby) openScreen(Screen::JoinMenu);
			}
			break;
		case NetEvent::Rejected:
			joinStatus_ = e.text;
			session_.stop();
			if (screen_ != Screen::JoinMenu) openScreen(Screen::JoinMenu);
			break;
		case NetEvent::Lobby:
			if (!host) {
				lobby_.fromJson(e.json);
				if (screen_ == Screen::JoinMenu) openScreen(Screen::Lobby);
				if (auto_.netJoin && !lobby_.clientReady) session_.sendLobbyRequest({ {"ready", true} });
			}
			break;
		case NetEvent::LobbyRequest:
			if (host) {
				if (e.json.contains("ready")) lobby_.clientReady = e.json["ready"].get<bool>();
				lobbyChanged();
			}
			break;
		case NetEvent::Start:
			if (!host) {
				const auto& j = e.json;
				bool driverIsHost = j.value("driverIsHost", true);
				lobby_.driverIsHost = driverIsHost;
				const nlohmann::json* resume = j.contains("resume") ? &j["resume"] : nullptr;
				startStage(j.value("stage", 0), j.value("car", 0), (Weather)j.value("weather", 0), (TimeOfDay)j.value("timeOfDay", 0),
					driverIsHost ? Role::Navigator : Role::Driver, true, resume);
			}
			break;
		case NetEvent::CarState:
			if (screen_ == Screen::Stage && run_.role == Role::Navigator) receiveCarState(e.car);
			break;
		case NetEvent::NoteCall:
			if (screen_ == Screen::Stage && run_.role == Role::Driver) callNote(e.value, true);
			break;
		case NetEvent::StageEvent: {
			if (screen_ != Screen::Stage) break;
			std::string kind = e.json.value("kind", "");
			if (kind == "loaded") run_.partnerLoaded = true;
			else if (kind == "countdown" && run_.role == Role::Navigator) {
				run_.phase = RunPhase::Countdown;
				run_.countdown = e.json.value("seconds", 4.0f);
				run_.lastBeep = -1;
			}
			else if (kind == "finish" && run_.role == Role::Navigator) {
				run_.stageTime = e.json.value("stageTime", run_.stageTime);
				run_.penalties = e.json.value("penalties", run_.penalties);
				run_.resets = e.json.value("resets", run_.resets);
				run_.cuts = e.json.value("cuts", run_.cuts);
				run_.splitTimes[0] = e.json.value("split0", run_.splitTimes[0]);
				run_.splitTimes[1] = e.json.value("split1", run_.splitTimes[1]);
				run_.splitIndex = 2;
				finishRun();
			}
			else if (kind == "message") setMessage(e.json.value("text", ""), ui::kAccent);
			break;
		}
		case NetEvent::Pause:
			if (screen_ == Screen::Stage) run_.paused = e.value != 0;
			break;
		case NetEvent::BackToLobby:
			if (screen_ == Screen::Stage) endStage();
			lobby_.clientReady = false;
			openScreen(Screen::Lobby);
			break;
		case NetEvent::Chat:
			setMessage(e.text, ui::kAccent);
			break;
		}
	}
}
