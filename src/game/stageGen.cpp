#include "game/stageGen.h"
#include "core/noise.h"
#include "core/log.h"
#include <raymath.h>
#include <filesystem>
#include <cmath>
#include <algorithm>

namespace rally {
	StageDef generateStage(const std::string& id, const std::string& name, const std::string& env,
		Surface surface, uint32_t seed, float targetLength) {
		StageDef d;
		d.id = id;
		d.name = name;
		d.environment = env;
		d.surface = surface;
		d.seed = seed;
		d.width = surface == Surface::Tarmac ? 7.0f : (surface == Surface::Snow ? 6.2f : 6.5f);
		d.terrainAmplitude = env == "mountain" ? 70.0f : env == "snow" ? 22.0f : 18.0f;
		d.terrainScale = env == "mountain" ? 420.0f : 280.0f;
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
			for (size_t i = 0; i + 5 < pts.size(); ++i)
				if (Vector2Distance(pts[i], c) < 75.0f) return false;
			return true;
		};

		int guard = 0;
		while (dist < targetLength && guard++ < 4000) {
			float r = rng.uniform();
			// Hairpin sequence (mountain mostly).
			if ((mountain && r < 0.12f) || (!mountain && r < 0.03f)) {
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
		};
		for (const Def& d : defs) {
			std::string path = dir + "/" + d.id + ".json";
			if (std::filesystem::exists(path)) continue;
			StageDef sd = generateStage(d.id, d.name, d.env, d.surf, d.seed, d.len);
			sd.description = d.desc;
			if (std::string(d.env) == "snow") sd.timeOfDay = TimeOfDay::Day;
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
