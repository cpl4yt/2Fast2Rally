#pragma once
#include "game/stage.h"
#include <string>
#include <vector>

namespace rally {
	// Procedurally lays out a stage's control points (random walk with
	// self-intersection checks), plus bridges/jumps/surface patches.
	StageDef generateStage(const std::string& id, const std::string& name, const std::string& environment,
		Surface surface, uint32_t seed, float targetLength);
	StageDef generateStageOnce(const std::string& id, const std::string& name, const std::string& environment,
		Surface surface, uint32_t seed, float targetLength);

	// Closest distance between two parts of the road that are at least
	// `pathGap` metres apart along it. Small values mean the track comes back
	// near (or across) itself.
	float trackClosestApproach(const std::vector<Vector2>& points, float pathGap);

	// The three beta stages. Writes any missing json into `dir`.
	void ensureDefaultStages(const std::string& dir);

	// Lists stage json files in `dir` (sorted), loading their defs.
	std::vector<StageDef> loadAllStages(const std::string& dir, std::vector<std::string>* paths = nullptr);
}
