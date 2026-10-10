#include "game/stageGen.h"
#include "core/noise.h"
#include "core/log.h"
#include <raymath.h>
#include <filesystem>
#include <unordered_map>
#include <cmath>
#include <algorithm>

namespace rally {
	namespace {
		Vector2 catmullPt(Vector2 p0, Vector2 p1, Vector2 p2, Vector2 p3, float t) {
			float t2 = t * t, t3 = t2 * t;
			return {
				0.5f * ((2 * p1.x) + (-p0.x + p2.x) * t + (2 * p0.x - 5 * p1.x + 4 * p2.x - p3.x) * t2 + (-p0.x + 3 * p1.x - 3 * p2.x + p3.x) * t3),
				0.5f * ((2 * p1.y) + (-p0.y + p2.y) * t + (2 * p0.y - 5 * p1.y + 4 * p2.y - p3.y) * t2 + (-p0.y + 3 * p1.y - 3 * p2.y + p3.y) * t3)
			};
		}
	}

	float trackClosestApproach(const std::vector<Vector2>& pts, float pathGap) {
		// Sample the same Catmull-Rom curve the stage uses, every ~2 m.
		std::vector<Vector2> d;
		std::vector<float> s;
		int n = (int)pts.size();
		if (n < 2) return 1e9f;
		float acc = 0;
		for (int i = 0; i < n - 1; ++i) {
			Vector2 p0 = pts[std::max(0, i - 1)], p1 = pts[i], p2 = pts[i + 1], p3 = pts[std::min(n - 1, i + 2)];
			if (i == 0) p0 = { 2 * p1.x - p2.x, 2 * p1.y - p2.y };
			if (i + 2 > n - 1) p3 = { 2 * p2.x - p1.x, 2 * p2.y - p1.y };
			int steps = std::max(2, (int)(Vector2Distance(p1, p2) / 2.0f));
			for (int k = 0; k < steps; ++k) {
				Vector2 p = catmullPt(p0, p1, p2, p3, (float)k / steps);
				if (!d.empty()) acc += Vector2Distance(d.back(), p);
				d.push_back(p);
				s.push_back(acc);
			}
		}
		// Brute force over a coarse spatial hash.
		const float cellSize = 40.0f;
		std::unordered_map<int64_t, std::vector<int>> grid;
		auto key = [&](int cx, int cz) { return ((int64_t)cx << 32) ^ (uint32_t)cz; };
		for (int i = 0; i < (int)d.size(); ++i)
			grid[key((int)std::floor(d[i].x / cellSize), (int)std::floor(d[i].y / cellSize))].push_back(i);
		float best = 1e9f;
		for (int i = 0; i < (int)d.size(); ++i) {
			int cx = (int)std::floor(d[i].x / cellSize), cz = (int)std::floor(d[i].y / cellSize);
			for (int dz = -2; dz <= 2; ++dz)
				for (int dx = -2; dx <= 2; ++dx) {
					auto it = grid.find(key(cx + dx, cz + dz));
					if (it == grid.end()) continue;
					for (int j : it->second) {
						if (std::fabs(s[j] - s[i]) < pathGap) continue;
						best = std::min(best, Vector2Distance(d[i], d[j]));
					}
				}
		}
		return best;
	}

	StageDef generateStage(const std::string& id, const std::string& name, const std::string& env,
		Surface surface, uint32_t seed, float targetLength) {
		// Keep trying seeds until the road never comes back near itself.
		for (int attempt = 0; attempt < 40; ++attempt) {
			StageDef d = generateStageOnce(id, name, env, surface, seed + attempt * 7919u, targetLength);
			float gap = trackClosestApproach(d.points, 160.0f);
			if (gap >= 70.0f) {
				if (attempt > 0) log::info("stagegen: %s needed %d attempts (closest approach %.0f m)", id.c_str(), attempt + 1, gap);
				d.seed = seed;
				return d;
			}
		}
		log::warn("stagegen: %s could not avoid self-approach", id.c_str());
		return generateStageOnce(id, name, env, surface, seed, targetLength);
	}

	StageDef generateStageOnce(const std::string& id, const std::string& name, const std::string& env,
		Surface surface, uint32_t seed, float targetLength) {
		StageDef d;
		d.id = id;
		d.name = name;
		d.environment = env;
		d.surface = surface;
		d.seed = seed;
		d.width = surface == Surface::Tarmac ? 7.0f : (surface == Surface::Snow ? 6.2f : 6.5f);
		d.terrainAmplitude = env == "mountain" ? 40.0f : env == "snow" ? 16.0f : 14.0f;
		d.terrainScale = env == "mountain" ? 500.0f : 320.0f;
		d.treeDensity = env == "mountain" ? 0.7f : 1.0f;

		Rng rng(seed * 31u + 7u);
		const bool mountain = env == "mountain";
		const float globalHeading = rng.range(0, 2 * PI);
		float heading = globalHeading;
		Vector2 p = { 0, 0 };
		std::vector<Vector2> pts = { p };
		std::vector<float> turnAt;       // heading change at each point
		std::vector<float> distAt = { 0 };
		float dist = 0;

		auto clear = [&](Vector2 c) {
			if (pts.empty()) return true;
			// Distance from the new point, and from the new segment, to every
			// earlier part of the road (except the last few points).
			Vector2 a = pts.back();
			for (size_t i = 0; i + 4 < pts.size(); ++i) {
				if (Vector2Distance(pts[i], c) < 90.0f) return false;
				Vector2 p = pts[i];
				Vector2 ab = Vector2Subtract(c, a);
				float t = std::clamp(Vector2DotProduct(Vector2Subtract(p, a), ab) / std::max(1e-3f, Vector2DotProduct(ab, ab)), 0.0f, 1.0f);
				if (Vector2Distance(p, Vector2Add(a, Vector2Scale(ab, t))) < 80.0f) return false;
			}
			return true;
		};

		int guard = 0;
		while (dist < targetLength && guard++ < 4000) {
			float r = rng.uniform();
			// (Hairpin sequences disabled: their two legs sit too close together.)
			if (r < 0.0f) {
				float dir = rng.uniform() < 0.5f ? -1.0f : 1.0f;
				std::vector<Vector2> seq;
				float h = heading;
				Vector2 q = p;
				bool ok = true;
				for (int k = 0; k < 4; ++k) {
					h += dir * (k == 0 || k == 3 ? 0.45f : 1.1f);
					float len = (k == 0 || k == 3) ? 22.0f : 16.0f;
					q = { q.x + std::sin(h) * len, q.y + std::cos(h) * len };
					seq.push_back(q);
				}
				Vector2 exit = { q.x + std::sin(h) * 40.0f, q.y + std::cos(h) * 40.0f };
				if (!clear(exit)) ok = false;
				for (auto& s : seq) if (!clear(s)) ok = false;
				if (ok) {
					for (auto& s : seq) { dist += Vector2Distance(pts.back(), s); pts.push_back(s); distAt.push_back(dist); turnAt.push_back(1.1f); }
					heading = h;
					p = q;
					continue;
				}
			}
			bool placed = false;
			for (int tries = 0; tries < 24 && !placed; ++tries) {
				float t = rng.uniform();
				float delta;
				if (t < 0.38f) delta = rng.range(-0.12f, 0.12f);                 // straight-ish
				else if (t < 0.75f) delta = rng.range(0.25f, 0.6f);              // medium
				else delta = rng.range(0.6f, mountain ? 1.25f : 1.05f);          // tight
				if (t >= 0.38f && rng.uniform() < 0.5f) delta = -delta;
				// Pull back toward the overall direction so the stage travels.
				float off = heading - globalHeading;
				while (off > PI) off -= 2 * PI;
				while (off < -PI) off += 2 * PI;
				if (std::fabs(off) > 1.4f && delta * off > 0) delta = -delta;
				float len = t < 0.38f ? rng.range(55, 95) : rng.range(32, 60);
				float nh = heading + delta;
				Vector2 c = { p.x + std::sin(nh) * len, p.y + std::cos(nh) * len };
				if (!clear(c)) continue;
				heading = nh;
				dist += len;
				p = c;
				pts.push_back(c);
				distAt.push_back(dist);
				turnAt.push_back(std::fabs(delta));
				placed = true;
			}
			if (!placed) {
				// Stuck: turn hard back toward the global heading.
				heading = globalHeading + rng.range(-0.3f, 0.3f);
			}
		}
		d.points = pts;

		// Features on straight-ish stretches.
		std::vector<float> straights;
		for (size_t i = 2; i + 2 < turnAt.size(); ++i)
			if (turnAt[i] < 0.13f && turnAt[i - 1] < 0.2f && distAt[i] > 250 && distAt[i] < dist - 250)
				straights.push_back(distAt[i]);
		auto takeStraight = [&](float minGap, std::vector<float>& used) -> float {
			for (int tries = 0; tries < 40 && !straights.empty(); ++tries) {
				float s = straights[rng.irange(0, (int)straights.size() - 1)];
				bool okk = true;
				for (float u : used) if (std::fabs(u - s) < minGap) okk = false;
				if (okk) { used.push_back(s); return s; }
			}
			return -1;
		};
		std::vector<float> used;
		if (env == "forest" || env == "mountain") {
			float s = takeStraight(200, used);
			if (s > 0) d.bridges.push_back({ s - 10, s + 25 });
		}
		int jumps = env == "forest" ? 3 : env == "snow" ? 2 : 1;
		for (int i = 0; i < jumps; ++i) {
			float s = takeStraight(150, used);
			if (s > 0) d.jumps.push_back({ s, env == "mountain" ? 0.9f : rng.range(1.0f, 1.6f) });
		}
		if (env == "forest") {
			float s = rng.range(dist * 0.45f, dist * 0.6f);
			d.surfaceSegments.push_back({ s, s + 90, Surface::Mud });
		}
		return d;
	}

	void ensureDefaultStages(const std::string& dir) {
		std::error_code ec;
		std::filesystem::create_directories(dir, ec);
		struct Def { const char* id; const char* name; const char* env; Surface surf; uint32_t seed; float len; const char* desc; };
		const Def defs[] = {
			{ "01_kallio_forest", "Kallio Forest", "forest", Surface::Gravel, 1207, 3000, "Fast, flowing gravel through pine forest. Watch the jumps and the bridge." },
			{ "02_col_de_brume", "Col de Brume", "mountain", Surface::Tarmac, 4421, 3000, "Twisting mountain tarmac with hairpins and big drops." },
			{ "03_frostvik_snow", "Frostvik Snow", "snow", Surface::Snow, 7731, 2000, "Snowbanks, low grip and long sweepers. Lean on the banks." },
			{ "04_ridgeback_gravel", "Ridgeback Gravel", "mountain", Surface::Gravel, 9157, 2600, "Loose mountain gravel with hairpins and drops. Brake early, turn in late." },
			{ "05_lakeside_sprint", "Lakeside Sprint", "forest", Surface::Tarmac, 3389, 2200, "Fast forest tarmac broken up by gravel sections. Watch the surface changes." },
		};
		for (const Def& d : defs) {
			std::string path = dir + "/" + d.id + ".json";
			if (std::filesystem::exists(path)) continue;
			StageDef sd = generateStage(d.id, d.name, d.env, d.surf, d.seed, d.len);
			sd.description = d.desc;
			if (std::string(d.id) == "05_lakeside_sprint") {
				// Mixed surface: gravel sections between the tarmac.
				sd.surfaceSegments.push_back({ d.len * 0.28f, d.len * 0.42f, Surface::Gravel });
				sd.surfaceSegments.push_back({ d.len * 0.66f, d.len * 0.8f, Surface::Gravel });
			}
			if (saveStageDef(path, sd)) log::info("stagegen: wrote %s (%d points)", path.c_str(), (int)sd.points.size());
		}
	}

	std::vector<StageDef> loadAllStages(const std::string& dir, std::vector<std::string>* paths) {
		std::vector<std::string> files;
		std::error_code ec;
		for (auto& e : std::filesystem::directory_iterator(dir, ec))
			if (e.path().extension() == ".json") files.push_back(e.path().generic_string());
		std::sort(files.begin(), files.end());
		std::vector<StageDef> out;
		if (paths) paths->clear();
		for (auto& f : files) {
			StageDef d;
			if (loadStageDef(f, d)) {
				out.push_back(d);
				if (paths) paths->push_back(f);
			}
		}
		return out;
	}
}
