#pragma once
#include <raylib.h>
#include <string>
#include <vector>
#include <cstdint>
#include <unordered_map>
#include "core/physicsEngine.h"
#include "core/noise.h"
#include "game/surface.h"

namespace rally {
	enum class Weather { Clear = 0, Overcast, Rain, Count };
	enum class TimeOfDay { Day = 0, Dusk, Night, Count };
	const char* weatherName(Weather w);
	const char* timeOfDayName(TimeOfDay t);

	struct SurfaceSegment { float from = 0, to = 0; Surface surface = Surface::Gravel; };
	struct Range { float from = 0, to = 0; };
	struct JumpDef { float at = 0; float height = 1.0f; };
	// Manual pace-note edit, matched to the generated note nearest `at`.
	struct NoteOverride {
		float at = 0;
		bool remove = false;
		int severity = -1;          // -1 = keep generated
		int direction = 0;          // 0 keep, -1 left, +1 right
		uint32_t modsSet = 0, modsClear = 0;
	};

	// The editable, saved description of a stage (res/data/stages/*.json).
	struct StageDef {
		std::string id = "stage";
		std::string name = "Stage";
		std::string description;
		std::string environment = "forest";  // forest | mountain | snow
		std::string event = "Season Opener";  // rally event this stage belongs to
		Surface surface = Surface::Gravel;
		float width = 6.5f;
		uint32_t seed = 1;
		float terrainAmplitude = 20.0f;
		float terrainScale = 260.0f;
		float treeDensity = 1.0f;
		std::vector<Vector2> points;         // control points (x, z)
		std::vector<SurfaceSegment> surfaceSegments;
		std::vector<Range> bridges;
		std::vector<JumpDef> jumps;
		std::vector<NoteOverride> noteOverrides;
		Weather weather = Weather::Clear;
		TimeOfDay timeOfDay = TimeOfDay::Day;
	};

	bool loadStageDef(const std::string& path, StageDef& out);
	bool saveStageDef(const std::string& path, const StageDef& def);

	struct RoadSample {
		Vector3 pos{};      // centre line, road surface height
		Vector2 dir{};      // xz unit tangent
		Vector2 right{};    // xz unit right
		float s = 0;        // distance from stage origin
		float curvature = 0;// rad/m, + = left
		Surface surface = Surface::Gravel;
		bool bridge = false;
	};

	enum class PropKind : uint8_t {
		Pine, Pine2, Birch, Birch2, Oak, Oak2, DeadTree, Rock1, Rock2, Rock3, Rock4, Stump, Log,
		GrassTall, Post, Spectator, Count
	};

	struct Prop {
		PropKind kind;
		Vector3 pos;
		float yaw;
		float scale;
		Color tint;
		bool collides;
	};

	struct Banner {
		Vector3 pos;
		float yaw;
		float width;
		std::string text;
		Color color;
		bool gantry;
	};

	struct TerrainChunk {
		Mesh mesh{};
		BoundingBox bounds{};
	};

	class Stage : public Ground {
	public:
		~Stage();
		// Builds everything (samples, heightfield, meshes, props). Expensive
		// (~0.5-1.5 s) - call from a loading screen.
		bool build(const StageDef& def, Weather weather, TimeOfDay tod, bool buildMeshes = true);
		void unload();
		bool loaded() const { return !samples.empty(); }

		// --- Ground ---
		float heightAt(float x, float z) const override;
		Surface surfaceAt(float x, float z) const override;
		float gripMultiplier(Surface s) const override;

		// --- Road queries ---
		float length() const { return samples.empty() ? 0.0f : samples.back().s; }
		RoadSample sampleAt(float s) const;
		// Progress (s) of the road point nearest p. `hint` (in/out) makes the
		// search local; pass -1 for a full search. lateral = signed distance
		// to the centre line (+ = right).
		float project(Vector3 p, int& hint, float* lateral = nullptr) const;
		float halfWidth() const { return def.width * 0.5f; }

		void nearbyStatics(Vector3 p, float radius, std::vector<PhysicsEngine::StaticBox>& out) const;

		StageDef def;
		Weather weather = Weather::Clear;
		TimeOfDay timeOfDay = TimeOfDay::Day;
		std::vector<RoadSample> samples;     // 1 m spacing
		float startS = 25.0f, finishS = 0.0f;
		std::vector<float> splitS;

		// Heightfield
		float originX = 0, originZ = 0, cell = 1.5f;
		int nx = 0, nz = 0;
		std::vector<float> height;
		std::vector<int> roadIdx;
		std::vector<float> roadDist;

		// Props & rendering data
		std::vector<Prop> props;
		std::vector<Banner> banners;
		std::vector<TerrainChunk> chunks;
		Mesh roadMesh[(int)Surface::Count]{};
		Mesh shoulderMesh{};
		Mesh markingsMesh{};
		Mesh bridgeMesh{};
		Mesh waterMesh{};
		bool meshesBuilt = false;
		Surface offroad = Surface::Grass;
		float baseHeight(float x, float z) const;

		// Corners with a rock/stump on the inside (pace note "don't cut").
		// x = s of the hazard, y = side (-1 left, +1 right).
		std::vector<Vector2> dontCut;
		// Spectator groups (s positions) - used for "caution" notes.
		std::vector<float> spectatorS;

	private:
		Noise noise{ 1 };
		std::unordered_map<int64_t, std::vector<int>> propGrid; // collidable props, 20 m cells
		void buildSamples();
		void buildProfile();
		void buildHeightfield();
		void buildProps();
		void buildMeshes();
		float gridHeight(float x, float z) const;
		int cellIndex(float x, float z) const;
	};

	// Draws a stage map (2D) into a rectangle: road line, checkpoints, car.
	void drawStageMap(const Stage& stage, Rectangle r, float carS, Vector3 carPos, float carYaw, bool northUp);
}
