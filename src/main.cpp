#include "game/game.h"
#include "core/log.h"
#include "core/crashHandler.h"
#include <cstring>
#include <cstdlib>
#include <string>

// Command line (mostly for testing):
//   --selftest            AI drives --stage N with --car M, logs + screenshots, exits
//   --menushot            screenshot the main menu and exit
//   --nettest-host        automated co-op host (AI driver) on 127.0.0.1
//   --nettest-join        automated co-op navigator joining 127.0.0.1
//   --stage N --car N --weather N --time N --limit SECONDS --shots PREFIX
//   --genstages           regenerate the default stage layouts
int main(int argc, char** argv) {
	rally::AutoOptions opts;
	for (int i = 1; i < argc; ++i) {
		std::string a = argv[i];
		auto next = [&](int def) { return (i + 1 < argc) ? std::atoi(argv[++i]) : def; };
		if (a == "--selftest") opts.selfTest = true;
		else if (a == "--menushot") opts.menuShot = true;
		else if (a == "--nettest-host") opts.netHost = true;
		else if (a == "--nettest-join") opts.netJoin = true;
		else if (a == "--ai") opts.aiDriver = true;
		else if (a == "--navigator") opts.asNavigator = true;
		else if (a == "--screens") opts.screens = true;
		else if (a == "--rally") opts.rally = true;
		else if (a == "--htest") opts.hTest = true;
		else if (a == "--carshots") opts.carShots = true;
		else if (a == "--genstages") opts.genStages = true;
		else if (a == "--stage") opts.stage = next(0);
		else if (a == "--car") opts.car = next(0);
		else if (a == "--weather") opts.weather = next(0);
		else if (a == "--time") opts.timeOfDay = next(0);
		else if (a == "--limit") opts.timeLimit = (float)next(400);
		else if (a == "--navview") opts.navView = next(0);
		else if (a == "--shots" && i + 1 < argc) opts.shotPrefix = argv[++i];
	}

	// Automated runs log to their own file so several can run side by side.
	bool automated = opts.selfTest || opts.menuShot || opts.netHost || opts.netJoin || opts.screens || opts.carShots;
	rally::log::init(automated ? "logs/" + opts.shotPrefix + ".log" : "logs/rally.log");
	rally::crash::install();

	rally::Game game;
	if (!game.init(opts)) {
		rally::log::error("init failed");
		rally::log::shutdown();
		return 1;
	}
	game.run();
	game.shutdown();
	rally::log::info("clean exit");
	rally::log::shutdown();
	return 0;
}
