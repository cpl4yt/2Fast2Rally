#pragma once
#include <raylib.h>
#include <string>
#include <vector>
#include <deque>
#include "core/physicsEngine.h"
#include "core/settings.h"
#include "core/audioSynth.h"
#include "game/carSpec.h"
#include "game/vehicle.h"
#include "game/stage.h"
#include "game/paceNotes.h"
#include "game/voice.h"
#include "game/world.h"
#include "game/carRender.h"
#include "game/particles.h"
#include "game/session.h"
#include "game/leaderboard.h"

namespace rally {
	enum class Screen { MainMenu, SoloSetup, HostMenu, JoinMenu, Lobby, Settings, Leaderboard, Stage, Editor };
	enum class Role : uint8_t { Driver, Navigator };
	enum class RunPhase : uint8_t { Loading, WaitingPartner, Countdown, Running, Finished };

	// Command-line driven automation (self tests, screenshots).
	struct AutoOptions {
		bool selfTest = false;        // AI drives a stage, logs, screenshots, exits
		bool menuShot = false;        // screenshot the main menu and exit
		bool netHost = false, netJoin = false; // automated co-op test pair
		bool aiDriver = false;        // AI drives in the normal game (debug)
		int stage = 0, car = 0;
		int weather = 0, timeOfDay = 0;
		float timeLimit = 400.0f;
		std::string shotPrefix = "screenshot";
		bool genStages = false;
		int navView = -1;             // force a navigator view (for screenshots)
		bool asNavigator = false;     // self test as the navigator (AI driver)
	};

	struct NoteCallState {
		bool called = false;
		float callDistance = 0;  // metres before the note when called
		float callTime = 0;      // seconds before the note when called
		bool passed = false;
	};

	struct RunState {
		RunPhase phase = RunPhase::Loading;
		Role role = Role::Driver;
		bool online = false;
		bool aiCodriver = true;     // AI calls notes (solo, or partner lost)
		bool aiDriver = false;      // AI drives (navigator practice / tests)
		float countdown = 0;
		int lastBeep = -1;
		double stageTime = 0;
		double simClock = 0;
		float progress = 0;         // metres along the stage
		int hint = -1;
		float lateral = 0;
		int splitIndex = 0;
		float splitTimes[2]{};
		double penalties = 0;
		int resets = 0;
		int cuts = 0;
		double finishTime = 0;
		float finishedFor = 0;
		float offRoadTimer = 0;
		float offRoadDist = 0;      // distance driven since leaving the road
		float lostTimer = 0;
		float lastOnRoadS = 0;
		bool paused = false;
		bool partnerLost = false;
		bool partnerLoaded = false;
		bool resumeFromSnapshot = false;
		float waitTimer = 0;
		std::vector<NoteCallState> calls;
		std::deque<int> hudNotes;   // driver HUD: called, not yet passed
		int navCursor = 0;          // navigator: next note to call
		int earlyCalls = 0, goodCalls = 0, lateCalls = 0, missedCalls = 0;
		std::string message;
		float messageTimer = 0;
		Color messageColor = WHITE;
		int rank = 0;
		bool recorded = false;
		// Audio/visual event counters (also replicated to the navigator).
		uint16_t backfireSeq = 0, shiftSeq = 0, impactSeq = 0;
		float impactStrength = 0;
	};

	class Game {
	public:
		bool init(const AutoOptions& opts);
		void run();
		void shutdown();

	private:
		// --- frame ---
		void frame(float dt);
		void pollNetwork();
		void handleNetEvent(const NetEvent& e);

		// --- menus (gameMenus.cpp) ---
		void drawMenuBackground(float dt);
		void screenMainMenu();
		void screenSoloSetup();
		void screenHostMenu();
		void screenJoinMenu();
		void screenLobby();
		void screenSettings();
		void screenLeaderboard();
		void openScreen(Screen s);
		void hostStartStage();
		void lobbyChanged();

		// --- stage (gameStage.cpp) ---
		bool startStage(int stageIndex, int carIndex, Weather w, TimeOfDay t, Role role, bool online, const nlohmann::json* resume = nullptr);
		void endStage();
		void stageFrame(float dt);
		void simStep(float dt);
		void driverInput(DriveInput& in, float dt);
		void aiDrive(DriveInput& in);
		void aiCodriver();
		void updateProgress();
		void resetToRoad(bool penalty, const char* why);
		void callNote(int idx, bool fromNetwork);
		void finishRun();
		void updateCamera(float dt);
		void updateAudio(float dt);
		void sendCarState();
		void receiveCarState(const CarStatePacket& p);
		const CarVisual& displayCar() const;
		void setMessage(const std::string& m, Color c, float t = 2.5f);
		void drawStage3D(float dt);

		// --- HUD (gameHud.cpp) ---
		void drawHud();
		void drawDriverHud();
		void drawNavigatorHud();
		void drawNoteIcon(const PaceNote& n, Rectangle r, float alpha, bool highlight);
		void drawNoteTimeline(Rectangle r);
		void drawPaceBook(Rectangle r);
		void drawPauseMenu();
		void drawResults();
		void drawCountdown();
		void drawDebug();

		// --- editor (editor.cpp) ---
		void editorOpen(int stageIndex);
		void editorFrame(float dt);
		void editorRebuildPreview();

		// --- helpers ---
		void loadContent();
		void reloadCars();
		std::string stagePath(int i) const;
		void takeScreenshot(const std::string& name);
		void applyVideoSettings();

		AutoOptions auto_;
		Screen screen_ = Screen::MainMenu;
		Screen settingsReturn_ = Screen::MainMenu;
		bool quit_ = false;

		World world_;
		Stage stage_;
		PhysicsEngine physics_;
		Vehicle car_;
		CarRenderer carRender_;
		CarRenderer menuCarRender_;
		Particles particles_;
		Voice voice_;
		Session session_;
		Leaderboard leaderboard_;
		Music menuMusic_{};
		bool musicLoaded_ = false;

		std::vector<CarSpec> cars_;
		std::vector<StageDef> stageDefs_;
		std::vector<std::string> stagePaths_;
		std::vector<PaceNote> notes_;

		RunState run_;
		int stageIndex_ = 0, carIndex_ = 0;
		Weather weather_ = Weather::Clear;
		TimeOfDay timeOfDay_ = TimeOfDay::Day;
		bool soloAsNavigator_ = false;

		// Camera
		Camera3D cam_{};
		int camMode_ = 0;           // driver: 0 chase, 1 near chase, 2 bonnet, 3 cockpit
		int navView_ = 0;           // navigator: 0 co-driver seat, 1 chase, 2 map, 3 pace book
		Vector3 camPos_{}, camLook_{};
		float camYaw_ = 0;
		bool camInit_ = false;
		bool debugOverlay_ = false;

		// Fixed-step accumulator & render interpolation of the local car.
		double accumulator_ = 0;
		CarVisual prevVisual_{}, curVisual_{};
		CarVisual renderVisual_{};

		// Navigator side: remote snapshots.
		struct Snapshot { CarStatePacket pkt; double recvTime; };
		std::deque<Snapshot> snapshots_;
		CarVisual remoteVisual_{};
		uint16_t seenBackfire_ = 0, seenShift_ = 0, seenImpact_ = 0;
		int lastGearHeard_ = 1;
		uint32_t sendSeq_ = 0;
		float sendTimer_ = 0;
		std::vector<PhysicsEngine::StaticBox> statics_;

		// Lobby / connection
		LobbyState lobby_;
		std::string joinStatus_;
		std::string hostName_, joinAddress_;
		std::string portText_;
		float connectTimer_ = 0;

		// Settings screen state
		int settingsTab_ = 0;
		int rebindAction_ = -1;      // waiting for a key/pad for this action
		int rebindKind_ = 0;         // 0 key, 1 alt key, 2 pad button, 3 axis(steer/throttle/brake)
		int rebindAxis_ = 0;

		// Leaderboard screen
		int boardStage_ = 0;

		// Menu background
		float menuSpin_ = 0;
		CarVisual menuVisual_{};

		// Editor state
		struct EditorState {
			int stage = 0;
			StageDef def;
			std::string path;
			Camera2D cam2d{};
			int selected = -1;
			bool dragging = false;
			bool dirty = false;
			int selectedNote = -1;
			bool showNotes = true;
			std::vector<PaceNote> previewNotes;
			Stage preview;          // samples only (no meshes) for notes/length
			std::string status;
			float statusTimer = 0;
		} ed_;

		// Self test bookkeeping
		double fpsFrames_ = 0, fpsTime_ = 0, fpsMin_ = 1e9;
		float aiStuckTimer_ = 0;
		float aiLastStuckS_ = -1000;
		int resetShots_ = 0;
		float autoTimer_ = 0;
		int autoShots_ = 0;
		float autoNextLog_ = 0;
		bool autoStarted_ = false;
	};
}
