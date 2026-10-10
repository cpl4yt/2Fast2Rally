#pragma once
#include "game/stage.h"
#include <string>
#include <vector>

namespace rally {
	// Procedurally lays out a stage's control points (random walk with
	// self-intersection checks), plus bridges/jumps/surface patches.
	StageDef generateStage(const std::string& id, const std::string& name, const std::string& environment,
		Surface surface, uint32_t seed, float targetLength);

	// The three beta stages. Writes any missing json into `dir`.
	void ensureDefaultStages(const std::string& dir);

	// Lists stage json files in `dir` (sorted), loading their defs.
	std::vector<StageDef> loadAllStages(const std::string& dir, std::vector<std::string>* paths = nullptr);
}
