#include "game/stage.h"
#include "game/meshBuilder.h"
#include "core/log.h"
#include <raymath.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <cmath>
#include <algorithm>

namespace rally {
	const char* weatherName(Weather w) {
		switch (w) { case Weather::Clear: return "Clear"; case Weather::Overcast: return "Overcast"; case Weather::Rain: return "Rain"; default: return "?"; }
	}
	const char* timeOfDayName(TimeOfDay t) {
		switch (t) { case TimeOfDay::Day: return "Day"; case TimeOfDay::Dusk: return "Dusk"; case TimeOfDay::Night: return "Night"; default: return "?"; }
	}

	// ------------------------------------------------------------------
	// JSON
	// ------------------------------------------------------------------
	bool loadStageDef(const std::string& path, StageDef& d) {
		std::ifstream in(path);
		if (!in) return false;
		try {
			nlohmann::json j = nlohmann::json::parse(in);
			d = StageDef{};
			d.id = j.value("id", d.id);
			d.name = j.value("name", d.name);
			d.description = j.value("description", d.description);
			d.environment = j.value("environment", d.environment);
			d.surface = surfaceFromName(j.value("surface", std::string("gravel")));
			d.width = j.value("width", d.width);
			d.seed = j.value("seed", d.seed);
			d.terrainAmplitude = j.value("terrainAmplitude", d.terrainAmplitude);
			d.terrainScale = j.value("terrainScale", d.terrainScale);
			d.treeDensity = j.value("treeDensity", d.treeDensity);
			std::string w = j.value("weather", std::string("clear"));
			d.weather = w == "rain" ? Weather::Rain : w == "overcast" ? Weather::Overcast : Weather::Clear;
			{ std::string tod = j.value("timeOfDay", std::string("day")); d.timeOfDay = tod == "dusk" ? TimeOfDay::Dusk : tod == "night" ? TimeOfDay::Night : TimeOfDay::Day; }
			for (auto& p : j.at("points")) d.points.push_back({ p[0].get<float>(), p[1].get<float>() });
			if (j.contains("surfaceSegments"))
				for (auto& s : j["surfaceSegments"]) d.surfaceSegments.push_back({ s["from"], s["to"], surfaceFromName(s["surface"]) });
			if (j.contains("bridges"))
				for (auto& b : j["bridges"]) d.bridges.push_back({ b["from"], b["to"] });
			if (j.contains("jumps"))
				for (auto& b : j["jumps"]) d.jumps.push_back({ b["at"], b.value("height", 1.0f) });
			if (j.contains("noteOverrides"))
				for (auto& o : j["noteOverrides"]) {
					NoteOverride n;
					n.at = o.value("at", 0.0f);
					n.remove = o.value("remove", false);
					n.severity = o.value("severity", -1);
					n.direction = o.value("direction", 0);
					n.modsSet = o.value("modsSet", 0u);
					n.modsClear = o.value("modsClear", 0u);
					d.noteOverrides.push_back(n);
				}
			return d.points.size() >= 2;
		}
		catch (const std::exception& e) {
			log::error("stage: failed to parse %s: %s", path.c_str(), e.what());
			return false;
		}
	}

	bool saveStageDef(const std::string& path, const StageDef& d) {
		nlohmann::json j;
		j["id"] = d.id; j["name"] = d.name; j["description"] = d.description;
		j["environment"] = d.environment; j["surface"] = surfaceKey(d.surface);
		j["width"] = d.width; j["seed"] = d.seed;
		j["terrainAmplitude"] = d.terrainAmplitude; j["terrainScale"] = d.terrainScale;
		j["treeDensity"] = d.treeDensity;
		j["weather"] = d.weather == Weather::Rain ? "rain" : d.weather == Weather::Overcast ? "overcast" : "clear";
		j["timeOfDay"] = d.timeOfDay == TimeOfDay::Dusk ? "dusk" : d.timeOfDay == TimeOfDay::Night ? "night" : "day";
		auto pts = nlohmann::json::array();
		for (auto& p : d.points) pts.push_back({ std::round(p.x * 10) / 10, std::round(p.y * 10) / 10 });
		j["points"] = pts;
		auto segs = nlohmann::json::array();
		for (auto& s : d.surfaceSegments) segs.push_back({ {"from", s.from}, {"to", s.to}, {"surface", surfaceKey(s.surface)} });
		j["surfaceSegments"] = segs;
		auto br = nlohmann::json::array();
		for (auto& b : d.bridges) br.push_back({ {"from", b.from}, {"to", b.to} });
		j["bridges"] = br;
		auto ju = nlohmann::json::array();
		for (auto& b : d.jumps) ju.push_back({ {"at", b.at}, {"height", b.height} });
		j["jumps"] = ju;
		auto ov = nlohmann::json::array();
		for (auto& o : d.noteOverrides)
			ov.push_back({ {"at", o.at}, {"remove", o.remove}, {"severity", o.severity}, {"direction", o.direction},
				{"modsSet", o.modsSet}, {"modsClear", o.modsClear} });
		j["noteOverrides"] = ov;
		std::ofstream out(path);
		if (!out) return false;
		out << j.dump(1);
		return true;
	}

	// ------------------------------------------------------------------
	// Build
	// ------------------------------------------------------------------
	namespace {
		float smoothstep(float a, float b, float x) {
			float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
			return t * t * (3 - 2 * t);
		}
		Vector2 catmull(Vector2 p0, Vector2 p1, Vector2 p2, Vector2 p3, float t) {
			float t2 = t * t, t3 = t2 * t;
			return {
				0.5f * ((2 * p1.x) + (-p0.x + p2.x) * t + (2 * p0.x - 5 * p1.x + 4 * p2.x - p3.x) * t2 + (-p0.x + 3 * p1.x - 3 * p2.x + p3.x) * t3),
				0.5f * ((2 * p1.y) + (-p0.y + p2.y) * t + (2 * p0.y - 5 * p1.y + 4 * p2.y - p3.y) * t2 + (-p0.y + 3 * p1.y - 3 * p2.y + p3.y) * t3)
			};
		}
		int64_t gridKey(int cx, int cz) { return ((int64_t)cx << 32) ^ (uint32_t)cz; }
	}

	Stage::~Stage() { unload(); }

	void Stage::unload() {
		if (meshesBuilt) {
			for (auto& c : chunks) UnloadMesh(c.mesh);
			for (auto& m : roadMesh) if (m.vertexCount) UnloadMesh(m);
			if (shoulderMesh.vertexCount) UnloadMesh(shoulderMesh);
			if (markingsMesh.vertexCount) UnloadMesh(markingsMesh);
			if (bridgeMesh.vertexCount) UnloadMesh(bridgeMesh);
			if (waterMesh.vertexCount) UnloadMesh(waterMesh);
		}
		chunks.clear();
		for (auto& m : roadMesh) m = Mesh{};
		shoulderMesh = markingsMesh = bridgeMesh = waterMesh = Mesh{};
		meshesBuilt = false;
		samples.clear(); height.clear(); roadIdx.clear(); roadDist.clear();
		props.clear(); banners.clear(); propGrid.clear(); dontCut.clear(); spectatorS.clear();
	}

	bool Stage::build(const StageDef& d, Weather w, TimeOfDay t, bool withMeshes) {
		unload();
		def = d;
		weather = w;
		timeOfDay = t;
		noise = Noise(def.seed);
		if (def.points.size() < 2) return false;
		offroad = def.environment == "snow" ? Surface::Snow : (def.environment == "mountain" ? Surface::Grass : Surface::Dirt);
		double t0 = GetTime();
		buildSamples();
		buildProfile();
		buildHeightfield();
		buildProps();
		if (withMeshes) buildMeshes();
		finishS = std::max(100.0f, length() - 170.0f);
		startS = 25.0f;
		splitS = { finishS * 0.36f, finishS * 0.68f };
		log::info("stage: built '%s' (%.0f m, %d props, grid %dx%d) in %.2fs",
			def.name.c_str(), length(), (int)props.size(), nx, nz, GetTime() - t0);
		return true;
	}

	float Stage::baseHeight(float x, float z) const {
		float S = def.terrainScale, A = def.terrainAmplitude;
		if (def.environment == "mountain") {
			float r = noise.ridged(x / S, z / S, 5);
			return A * (r * 1.4f - 0.55f) + A * 0.25f * noise.fbm(x / (S * 0.3f), z / (S * 0.3f), 3);
		}
		float h = A * noise.fbm(x / S, z / S, 5);
		h += A * 0.12f * noise.fbm(x / 45.0f + 17.0f, z / 45.0f - 9.0f, 3);
		return h;
	}

	void Stage::buildSamples() {
		// Dense Catmull-Rom through the control points, then resample at 1 m.
		std::vector<Vector2> pts = def.points;
		std::vector<Vector2> dense;
		int n = (int)pts.size();
		for (int i = 0; i < n - 1; ++i) {
			Vector2 p0 = pts[std::max(0, i - 1)], p1 = pts[i], p2 = pts[i + 1], p3 = pts[std::min(n - 1, i + 2)];
			if (i == 0) p0 = { 2 * p1.x - p2.x, 2 * p1.y - p2.y };
			if (i + 2 > n - 1) p3 = { 2 * p2.x - p1.x, 2 * p2.y - p1.y };
			float segLen = Vector2Distance(p1, p2);
			int steps = std::max(4, (int)(segLen / 0.5f));
			for (int k = 0; k < steps; ++k) dense.push_back(catmull(p0, p1, p2, p3, (float)k / steps));
		}
		dense.push_back(pts.back());

		samples.clear();
		RoadSample s0;
		s0.pos = { dense[0].x, 0, dense[0].y };
		samples.push_back(s0);
		float acc = 0.0f;
		Vector2 last = dense[0];
		for (size_t i = 1; i < dense.size(); ++i) {
			Vector2 cur = dense[i];
			float seg = Vector2Distance(last, cur);
			while (acc + seg >= 1.0f) {
				float need = 1.0f - acc;
				float tt = need / seg;
				Vector2 p = Vector2Lerp(last, cur, tt);
				RoadSample rs;
				rs.pos = { p.x, 0, p.y };
				rs.s = (float)samples.size();
				samples.push_back(rs);
				last = p;
				seg = Vector2Distance(last, cur);
				acc = 0.0f;
			}
			acc += seg;
			last = cur;
		}

		int N = (int)samples.size();
		std::vector<float> heading(N);
		for (int i = 0; i < N; ++i) {
			int a = std::max(0, i - 1), b = std::min(N - 1, i + 1);
			Vector2 d = { samples[b].pos.x - samples[a].pos.x, samples[b].pos.z - samples[a].pos.z };
			d = Vector2Normalize(d);
			samples[i].dir = d;
			samples[i].right = { -d.y, d.x };
			heading[i] = std::atan2(d.x, d.y);
			if (i > 0) {
				while (heading[i] - heading[i - 1] > PI) heading[i] -= 2 * PI;
				while (heading[i] - heading[i - 1] < -PI) heading[i] += 2 * PI;
			}
		}
		for (int i = 0; i < N; ++i) {
			int a = std::max(0, i - 4), b = std::min(N - 1, i + 4);
			samples[i].curvature = (b > a) ? (heading[b] - heading[a]) / (float)(b - a) : 0.0f;
		}
		for (auto& s : samples) {
			s.surface = def.surface;
			for (auto& seg : def.surfaceSegments) if (s.s >= seg.from && s.s <= seg.to) s.surface = seg.surface;
			for (auto& br : def.bridges) if (s.s >= br.from && s.s <= br.to) s.bridge = true;
		}
	}

	void Stage::buildProfile() {
		int N = (int)samples.size();
		std::vector<float> y(N), tmp(N);
		for (int i = 0; i < N; ++i) y[i] = baseHeight(samples[i].pos.x, samples[i].pos.z);
		// Smooth heavily so the road follows the land without being bumpy.
		auto boxBlur = [&](int r) {
			for (int i = 0; i < N; ++i) {
				int a = std::max(0, i - r), b = std::min(N - 1, i + r);
				float sum = 0;
				for (int k = a; k <= b; ++k) sum += y[k];
				tmp[i] = sum / (b - a + 1);
			}
			y.swap(tmp);
		};
		boxBlur(35); boxBlur(35); boxBlur(20);
		// Small crests from a second, shorter-wavelength profile.
		for (int i = 0; i < N; ++i) y[i] += 1.4f * noise.perlin(samples[i].s / 55.0f, 3.7f);
		// Limit the gradient to something a real road would have; the terrain
		// blend turns the difference into cuttings and embankments.
		const float maxGrade = def.environment == "mountain" ? 0.085f : 0.065f;
		for (int pass = 0; pass < 2; ++pass) {
			for (int i = 1; i < N; ++i) y[i] = std::clamp(y[i], y[i - 1] - maxGrade, y[i - 1] + maxGrade);
			for (int i = N - 2; i >= 0; --i) y[i] = std::clamp(y[i], y[i + 1] - maxGrade, y[i + 1] + maxGrade);
			boxBlur(6);
		}
		// Jumps: steep take-off, gentler landing.
		for (auto& j : def.jumps) {
			for (int i = 0; i < N; ++i) {
				float d = samples[i].s - j.at;
				float bump = 0.0f;
				if (d > -14.0f && d <= 0.0f) bump = 0.5f * (1 - std::cos(PI * (d + 14.0f) / 14.0f));
				else if (d > 0.0f && d < 22.0f) bump = 0.5f * (1 + std::cos(PI * d / 22.0f));
				y[i] += bump * j.height;
			}
		}
		// Bridges are flat between their ends.
		for (auto& br : def.bridges) {
			int a = std::clamp((int)br.from - 6, 0, N - 1), b = std::clamp((int)br.to + 6, 0, N - 1);
			for (int i = a; i <= b; ++i) y[i] = y[a] + (y[b] - y[a]) * (float)(i - a) / std::max(1, b - a);
		}
		for (int i = 0; i < N; ++i) samples[i].pos.y = y[i];
	}

	int Stage::cellIndex(float x, float z) const {
		int ix = (int)std::floor((x - originX) / cell + 0.5f);
		int iz = (int)std::floor((z - originZ) / cell + 0.5f);
		if (ix < 0 || iz < 0 || ix >= nx || iz >= nz) return -1;
		return iz * nx + ix;
	}

	void Stage::buildHeightfield() {
		float minX = 1e9f, minZ = 1e9f, maxX = -1e9f, maxZ = -1e9f;
		for (auto& s : samples) {
			minX = std::min(minX, s.pos.x); maxX = std::max(maxX, s.pos.x);
			minZ = std::min(minZ, s.pos.z); maxZ = std::max(maxZ, s.pos.z);
		}
		const float margin = 320.0f;
		originX = minX - margin; originZ = minZ - margin;
		nx = (int)((maxX - minX + 2 * margin) / cell) + 1;
		nz = (int)((maxZ - minZ + 2 * margin) / cell) + 1;
		size_t total = (size_t)nx * nz;
		height.assign(total, 0.0f);
		roadIdx.assign(total, -1);
		roadDist.assign(total, 1e9f);

		const float hw = def.width * 0.5f;
		const float R = hw + 20.0f;
		int rc = (int)std::ceil(R / cell);
		for (int i = 0; i < (int)samples.size(); ++i) {
			const RoadSample& s = samples[i];
			int cx = (int)std::floor((s.pos.x - originX) / cell + 0.5f);
			int cz = (int)std::floor((s.pos.z - originZ) / cell + 0.5f);
			for (int dz = -rc; dz <= rc; ++dz) {
				int z = cz + dz;
				if (z < 0 || z >= nz) continue;
				for (int dx = -rc; dx <= rc; ++dx) {
					int x = cx + dx;
					if (x < 0 || x >= nx) continue;
					float wx = originX + x * cell, wz = originZ + z * cell;
					float d = std::sqrt((wx - s.pos.x) * (wx - s.pos.x) + (wz - s.pos.z) * (wz - s.pos.z));
					if (d > R) continue;
					size_t k = (size_t)z * nx + x;
					if (d < roadDist[k]) { roadDist[k] = d; roadIdx[k] = i; }
				}
			}
		}

		// --- Smooth "road height" field on a coarse grid ---
		// Gaussian-weighted average of nearby road heights plus the distance to
		// the nearest road point. The land near the road follows this field and
		// fades into the free-form hills further out, so there are no cliffs
		// where the road and the noise terrain disagree.
		const float cc = 8.0f;
		int cnx = (int)((nx * cell) / cc) + 2, cnz = (int)((nz * cell) / cc) + 2;
		std::vector<float> sumW((size_t)cnx * cnz, 0.0f), sumWY((size_t)cnx * cnz, 0.0f), cDist((size_t)cnx * cnz, 1e9f);
		const float sigma = 40.0f, CR = 150.0f;
		int crc = (int)std::ceil(CR / cc);
		for (int i = 0; i < (int)samples.size(); i += 2) {
			const RoadSample& s = samples[i];
			int cx = (int)std::floor((s.pos.x - originX) / cc + 0.5f);
			int cz = (int)std::floor((s.pos.z - originZ) / cc + 0.5f);
			for (int dz = -crc; dz <= crc; ++dz) {
				int z = cz + dz;
				if (z < 0 || z >= cnz) continue;
				for (int dx = -crc; dx <= crc; ++dx) {
					int x = cx + dx;
					if (x < 0 || x >= cnx) continue;
					float wx = originX + x * cc, wz = originZ + z * cc;
					float d2 = (wx - s.pos.x) * (wx - s.pos.x) + (wz - s.pos.z) * (wz - s.pos.z);
					if (d2 > CR * CR) continue;
					size_t k = (size_t)z * cnx + x;
					float w = std::exp(-d2 / (2 * sigma * sigma));
					sumW[k] += w;
					sumWY[k] += w * s.pos.y;
					cDist[k] = std::min(cDist[k], std::sqrt(d2));
				}
			}
		}
		auto coarse = [&](const std::vector<float>& g, float x, float z, float def0) {
			float fx = (x - originX) / cc, fz = (z - originZ) / cc;
			int ix = std::clamp((int)std::floor(fx), 0, cnx - 2), iz = std::clamp((int)std::floor(fz), 0, cnz - 2);
			float tx = std::clamp(fx - ix, 0.0f, 1.0f), tz = std::clamp(fz - iz, 0.0f, 1.0f);
			size_t k = (size_t)iz * cnx + ix;
			float a = g[k], b = g[k + 1], c = g[k + cnx], d = g[k + cnx + 1];
			if (a > 1e8f || b > 1e8f || c > 1e8f || d > 1e8f) return def0;
			return (a * (1 - tx) + b * tx) * (1 - tz) + (c * (1 - tx) + d * tx) * tz;
		};
		std::vector<float> field((size_t)cnx * cnz, 1e9f);
		for (size_t k = 0; k < field.size(); ++k) if (sumW[k] > 1e-4f) field[k] = sumWY[k] / sumW[k];

		const bool snow = def.environment == "snow";
		const bool forest = def.environment == "forest";
		for (int z = 0; z < nz; ++z) {
			for (int x = 0; x < nx; ++x) {
				size_t k = (size_t)z * nx + x;
				float wx = originX + x * cell, wz = originZ + z * cell;
				float base = baseHeight(wx, wz);
				float dist = coarse(cDist, wx, wz, 1e9f);
				float roadField = coarse(field, wx, wz, base);
				// Land: road-following near the stage, free hills far away.
				float land = roadField + (base - roadField) * smoothstep(hw + 10.0f, 130.0f, std::min(dist, 1e6f));
				int idx = roadIdx[k];
				if (idx < 0) { height[k] = land; continue; }
				const RoadSample& s = samples[idx];
				float d = roadDist[k];
				float roadY = s.pos.y;
				float h;
				if (s.bridge) {
					float gully = roadY - 7.5f;
					h = gully + (std::max(land, gully) - gully) * smoothstep(hw + 5.0f, hw + 20.0f, d);
				}
				else if (d <= hw) {
					float t = d / hw;
					h = roadY - 0.07f * t * t;
				}
				else {
					float shoulder = hw + 0.8f;
					float edge = roadY - 0.07f;
					if (d <= shoulder) h = edge - (d - hw) * 0.1f;
					else {
						float near = edge - 0.08f;
						float u = d - shoulder;
						if (snow) near += 0.75f * std::exp(-(u - 1.6f) * (u - 1.6f) / 1.8f);   // snowbank
						else if (forest) near -= 0.35f * std::exp(-(u - 1.5f) * (u - 1.5f) / 0.9f); // ditch
						float t = smoothstep(0.0f, 12.0f, u);
						h = near + (land - near) * t;
					}
				}
				height[k] = h;
			}
		}
	}

	float Stage::gridHeight(float x, float z) const {
		float fx = (x - originX) / cell, fz = (z - originZ) / cell;
		int ix = (int)std::floor(fx), iz = (int)std::floor(fz);
		if (ix < 0 || iz < 0 || ix >= nx - 1 || iz >= nz - 1) return baseHeight(x, z);
		float tx = fx - ix, tz = fz - iz;
		size_t k = (size_t)iz * nx + ix;
		float h00 = height[k], h10 = height[k + 1], h01 = height[k + nx], h11 = height[k + nx + 1];
		return (h00 * (1 - tx) + h10 * tx) * (1 - tz) + (h01 * (1 - tx) + h11 * tx) * tz;
	}

	float Stage::heightAt(float x, float z) const {
		int k = cellIndex(x, z);
		if (k >= 0) {
			int idx = roadIdx[k];
			if (idx >= 0 && samples[idx].bridge) {
				const RoadSample& s = samples[idx];
				float lat = std::fabs((x - s.pos.x) * s.right.x + (z - s.pos.z) * s.right.y);
				if (lat <= halfWidth() + 0.45f) return s.pos.y;
			}
		}
		return gridHeight(x, z);
	}

	Surface Stage::surfaceAt(float x, float z) const {
		int k = cellIndex(x, z);
		if (k < 0) return offroad;
		int idx = roadIdx[k];
		if (idx < 0) return offroad;
		const RoadSample& s = samples[idx];
		float lat = std::fabs((x - s.pos.x) * s.right.x + (z - s.pos.z) * s.right.y);
		float hw = halfWidth();
		if (lat <= hw + 0.3f) return s.surface;
		if (lat <= hw + 1.0f) return s.surface == Surface::Tarmac ? Surface::Dirt : s.surface;
		return offroad;
	}

	float Stage::gripMultiplier(Surface s) const {
		if (weather == Weather::Rain && def.environment != "snow") return surfaceProps(s).wetFactor;
		return 1.0f;
	}

	RoadSample Stage::sampleAt(float s) const {
		if (samples.empty()) return {};
		s = std::clamp(s, 0.0f, length());
		int i = std::min((int)s, (int)samples.size() - 2);
		float t = s - i;
		RoadSample a = samples[i];
		const RoadSample& b = samples[i + 1];
		a.pos = Vector3Lerp(a.pos, b.pos, t);
		a.dir = Vector2Normalize(Vector2Lerp(a.dir, b.dir, t));
		a.right = { -a.dir.y, a.dir.x };
		a.s = s;
		return a;
	}

	float Stage::project(Vector3 p, int& hint, float* lateral) const {
		int N = (int)samples.size();
		if (N == 0) return 0;
		auto d2 = [&](int i) {
			float dx = samples[i].pos.x - p.x, dz = samples[i].pos.z - p.z;
			return dx * dx + dz * dz;
		};
		int best = 0;
		if (hint >= 0 && hint < N) {
			best = hint;
			float bd = d2(best);
			// Walk downhill on distance in both directions.
			for (int dir : { -1, 1 }) {
				int i = hint;
				for (int step = 0; step < 400; ++step) {
					int j = i + dir;
					if (j < 0 || j >= N) break;
					float dj = d2(j);
					if (dj > d2(i) && step > 20) break;
					if (dj < bd) { bd = dj; best = j; }
					i = j;
				}
			}
			// If we're far away from the hinted part, fall back to global.
			if (bd > 60.0f * 60.0f) hint = -1;
		}
		if (hint < 0) {
			float bd = 1e30f;
			for (int i = 0; i < N; i += 4) { float d = d2(i); if (d < bd) { bd = d; best = i; } }
			int a = std::max(0, best - 4), b = std::min(N - 1, best + 4);
			for (int i = a; i <= b; ++i) { float d = d2(i); if (d < bd) { bd = d; best = i; } }
		}
		hint = best;
		const RoadSample& s = samples[best];
		float along = (p.x - s.pos.x) * s.dir.x + (p.z - s.pos.z) * s.dir.y;
		if (lateral) *lateral = (p.x - s.pos.x) * s.right.x + (p.z - s.pos.z) * s.right.y;
		return std::clamp(s.s + along, 0.0f, length());
	}

	void Stage::nearbyStatics(Vector3 p, float radius, std::vector<PhysicsEngine::StaticBox>& out) const {
		out.clear();
		int cx = (int)std::floor(p.x / 20.0f), cz = (int)std::floor(p.z / 20.0f);
		int r = (int)std::ceil(radius / 20.0f);
		for (int dz = -r; dz <= r; ++dz)
			for (int dx = -r; dx <= r; ++dx) {
				auto it = propGrid.find(gridKey(cx + dx, cz + dz));
				if (it == propGrid.end()) continue;
				for (int i : it->second) {
					const Prop& pr = props[i];
					float ddx = pr.pos.x - p.x, ddz = pr.pos.z - p.z;
					if (ddx * ddx + ddz * ddz > radius * radius) continue;
					PhysicsEngine::StaticBox sb;
					bool rock = pr.kind >= PropKind::Rock1 && pr.kind <= PropKind::Rock4;
					bool post = pr.kind == PropKind::Post;
					float hx = rock ? 0.8f * pr.scale : post ? 0.08f : 0.28f * pr.scale;
					float hy = rock ? 0.6f * pr.scale : post ? 0.6f : 3.0f;
					sb.center = { pr.pos.x, pr.pos.y + hy - 0.2f, pr.pos.z };
					sb.halfExtents = { hx, hy, hx };
					sb.friction = 0.6f;
					sb.restitution = 0.1f;
					out.push_back(sb);
				}
			}
	}

	// ------------------------------------------------------------------
	// Props
	// ------------------------------------------------------------------
	void Stage::buildProps() {
		Rng rng(def.seed * 7919u + 13u);
		const float hw = halfWidth();
		const float L = length();
		const std::string& env = def.environment;

		auto clearOfRoad = [&](float x, float z, float minDist) {
			int k = cellIndex(x, z);
			if (k < 0) return true;
			if (roadIdx[k] < 0) return true;
			return roadDist[k] > minDist;
		};
		auto add = [&](PropKind kind, float x, float z, float scale, Color tint, bool collides) {
			Prop p{ kind, { x, heightAt(x, z) - 0.1f, z }, rng.range(0, 2 * PI), scale, tint, collides };
			props.push_back(p);
		};
		auto treeKind = [&]() {
			float r = rng.uniform();
			if (env == "snow") return r < 0.55f ? PropKind::Pine : r < 0.9f ? PropKind::Pine2 : PropKind::Birch;
			if (env == "mountain") return r < 0.35f ? PropKind::Oak : r < 0.6f ? PropKind::Oak2 : r < 0.85f ? PropKind::Pine : PropKind::DeadTree;
			return r < 0.4f ? PropKind::Pine : r < 0.65f ? PropKind::Pine2 : r < 0.85f ? PropKind::Birch : PropKind::Birch2;
		};
		Color treeTint = env == "snow" ? Color{ 225, 235, 245, 255 } : WHITE;

		// Roadside trees.
		int nTrees = (int)(L * (env == "mountain" ? 0.9f : 2.4f) * def.treeDensity);
		for (int i = 0; i < nTrees; ++i) {
			float s = rng.range(0, L);
			const RoadSample rs = sampleAt(s);
			float side = rng.uniform() < 0.5f ? -1.0f : 1.0f;
			float u = rng.uniform();
			float d = hw + 4.0f + u * u * 110.0f;
			float x = rs.pos.x + rs.right.x * side * d, z = rs.pos.z + rs.right.y * side * d;
			if (!clearOfRoad(x, z, hw + 3.5f)) continue;
			add(treeKind(), x, z, rng.range(0.8f, 1.35f), treeTint, d < hw + 14.0f);
		}
		// Background forest scattered over the whole terrain.
		int nBg = (int)(nx * nz * cell * cell / (env == "mountain" ? 2600.0f : 900.0f) * def.treeDensity);
		nBg = std::min(nBg, 2600);
		for (int i = 0; i < nBg; ++i) {
			float x = originX + rng.range(0, nx * cell), z = originZ + rng.range(0, nz * cell);
			if (!clearOfRoad(x, z, hw + 6.0f)) continue;
			add(treeKind(), x, z, rng.range(0.9f, 1.5f), treeTint, false);
		}
		// Rocks.
		int nRocks = (int)(L * (env == "mountain" ? 0.35f : 0.12f));
		for (int i = 0; i < nRocks; ++i) {
			float s = rng.range(0, L);
			const RoadSample rs = sampleAt(s);
			float side = rng.uniform() < 0.5f ? -1.0f : 1.0f;
			float d = hw + 3.0f + rng.range(0, 40);
			float x = rs.pos.x + rs.right.x * side * d, z = rs.pos.z + rs.right.y * side * d;
			if (!clearOfRoad(x, z, hw + 2.5f)) continue;
			PropKind k = (PropKind)((int)PropKind::Rock1 + rng.irange(0, 3));
			add(k, x, z, rng.range(0.6f, 1.6f), env == "snow" ? Color{ 230, 235, 245, 255 } : WHITE, d < hw + 10.0f);
		}
		// Grass tufts along the verges.
		if (env != "snow") {
			int nGrass = (int)(L * 0.5f);
			for (int i = 0; i < nGrass; ++i) {
				float s = rng.range(0, L);
				const RoadSample rs = sampleAt(s);
				if (rs.bridge) continue;
				float side = rng.uniform() < 0.5f ? -1.0f : 1.0f;
				float d = hw + rng.range(1.2f, 6.0f);
				float x = rs.pos.x + rs.right.x * side * d, z = rs.pos.z + rs.right.y * side * d;
				if (!clearOfRoad(x, z, hw + 1.0f)) continue;
				add(PropKind::GrassTall, x, z, rng.range(0.7f, 1.3f), WHITE, false);
			}
		}
		// Marker posts.
		for (float s = 40.0f; s < L - 10; s += 50.0f) {
			const RoadSample rs = sampleAt(s);
			if (rs.bridge) continue;
			for (float side : { -1.0f, 1.0f }) {
				float d = hw + 1.3f;
				float x = rs.pos.x + rs.right.x * side * d, z = rs.pos.z + rs.right.y * side * d;
				if (!clearOfRoad(x, z, hw + 1.0f)) continue; // hairpins: would sit on the other leg
				add(PropKind::Post, x, z, 1.0f, WHITE, true);
			}
		}
		// "Don't cut" hazards: rocks/stumps on the inside of tight corners.
		for (int i = 60; i < (int)samples.size() - 60; i += 1) {
			const RoadSample& rs = samples[i];
			if (std::fabs(rs.curvature) < 1.0f / 40.0f || std::fabs(rs.curvature) > 1.0f / 16.0f || rs.bridge) continue;
			// Only at the apex (local curvature max).
			if (std::fabs(rs.curvature) < std::fabs(samples[i - 1].curvature) || std::fabs(rs.curvature) < std::fabs(samples[i + 1].curvature)) continue;
			if (rng.uniform() > 0.45f) continue;
			float side = rs.curvature > 0 ? -1.0f : 1.0f; // inside: left turn -> left (-right)
			for (int k = -6; k <= 6; k += 4) {
				const RoadSample q = sampleAt(rs.s + k);
				float d = hw + 1.9f + rng.range(0, 0.4f);
				float x = q.pos.x + q.right.x * side * d, z = q.pos.z + q.right.y * side * d;
				if (!clearOfRoad(x, z, hw + 1.5f)) continue;
				PropKind kind = env == "forest" && rng.uniform() < 0.4f ? PropKind::Stump : (PropKind)((int)PropKind::Rock1 + rng.irange(0, 2));
				add(kind, x, z, kind == PropKind::Stump ? 1.3f : rng.range(0.55f, 0.8f), env == "snow" ? Color{ 225, 230, 240, 255 } : WHITE, true);
			}
			dontCut.push_back({ rs.s, side });
			i += 30;
		}
		// Spectators at a handful of tight corners, standing well back on the outside.
		{
			std::vector<int> candidates;
			for (int i = 200; i < (int)samples.size() - 200; ++i)
				if (std::fabs(samples[i].curvature) > 1.0f / 30.0f && !samples[i].bridge) candidates.push_back(i);
			int groups = std::min(7, (int)candidates.size() / 20);
			float lastS = -1000;
			for (int g = 0; g < groups * 4 && (int)spectatorS.size() < groups; ++g) {
				int i = candidates[rng.irange(0, (int)candidates.size() - 1)];
				const RoadSample& rs = samples[i];
				if (std::fabs(rs.s - lastS) < 300) continue;
				lastS = rs.s;
				spectatorS.push_back(rs.s);
				float side = rs.curvature > 0 ? 1.0f : -1.0f; // outside
				int count = rng.irange(6, 14);
				for (int c = 0; c < count; ++c) {
					const RoadSample q = sampleAt(rs.s + rng.range(-14, 14));
					float d = hw + rng.range(9.0f, 13.0f);
					float x = q.pos.x + q.right.x * side * d, z = q.pos.z + q.right.y * side * d;
					static const Color jackets[] = { {200, 40, 40, 255}, {40, 90, 200, 255}, {240, 200, 40, 255}, {40, 150, 70, 255}, {230, 110, 20, 255}, {60, 60, 70, 255}, {180, 180, 190, 255} };
					Prop p{ PropKind::Spectator, { x, heightAt(x, z), z }, std::atan2(-side * q.right.x, -side * q.right.y), rng.range(0.92f, 1.08f), jackets[rng.irange(0, 6)], false };
					props.push_back(p);
				}
			}
			std::sort(spectatorS.begin(), spectatorS.end());
		}

		// Collision grid.
		for (int i = 0; i < (int)props.size(); ++i) {
			if (!props[i].collides) continue;
			int cx = (int)std::floor(props[i].pos.x / 20.0f), cz = (int)std::floor(props[i].pos.z / 20.0f);
			propGrid[gridKey(cx, cz)].push_back(i);
		}

		// Banners / gantries.
		auto banner = [&](float s, const char* text, Color c, bool gantry) {
			RoadSample rs = sampleAt(s);
			banners.push_back({ rs.pos, std::atan2(rs.dir.x, rs.dir.y), def.width + 3.0f, text, c, gantry });
		};
		float fin = std::max(100.0f, L - 170.0f);
		banner(25.0f, "START", Color{ 40, 160, 60, 255 }, true);
		banner(fin * 0.36f, "SPLIT 1", Color{ 230, 160, 20, 255 }, false);
		banner(fin * 0.68f, "SPLIT 2", Color{ 230, 160, 20, 255 }, false);
		banner(fin, "FINISH", Color{ 200, 30, 30, 255 }, true);
		banner(std::min(L - 2.0f, fin + 110.0f), "STOP", Color{ 200, 30, 30, 255 }, false);
	}

	// ------------------------------------------------------------------
	// Meshes
	// ------------------------------------------------------------------
	void Stage::buildMeshes() {
		const std::string& env = def.environment;
		const float hw = halfWidth();

		// --- Terrain chunks (3 m render cells) ---
		const int step = 2;           // grid cells per render cell
		const int chunkCells = 40;    // render cells per chunk side
		int rnx = (nx - 1) / step, rnz = (nz - 1) / step;
		auto vtxHeight = [&](int gx, int gz) {
			size_t k = (size_t)gz * nx + gx;
			float h = height[k];
			// Sink terrain under the road corridor a little so the coarse mesh
			// never pokes through the road/shoulder meshes drawn on top.
			if (roadIdx[k] >= 0 && roadDist[k] < hw + 3.5f && !samples[roadIdx[k]].bridge) h -= 0.3f;
			return h;
		};
		// Multiplied with the terrain texture (grass.png / generated snow).
		Color base = env == "snow" ? Color{ 245, 248, 255, 255 } : env == "mountain" ? Color{ 235, 240, 220, 255 } : Color{ 225, 235, 215, 255 };
		Color rock = env == "snow" ? Color{ 175, 180, 192, 255 } : Color{ 165, 150, 140, 255 };
		Noise colorNoise(def.seed + 99);
		for (int cz0 = 0; cz0 < rnz; cz0 += chunkCells) {
			for (int cx0 = 0; cx0 < rnx; cx0 += chunkCells) {
				MeshBuilder mb;
				int cx1 = std::min(rnx, cx0 + chunkCells), cz1 = std::min(rnz, cz0 + chunkCells);
				float minY = 1e9f, maxY = -1e9f;
				auto V = [&](int rx, int rz, Vector3& p, Vector3& n, Color& c) {
					int gx = rx * step, gz = rz * step;
					p = { originX + gx * cell, vtxHeight(gx, gz), originZ + gz * cell };
					int gx0 = std::max(0, gx - step), gx1 = std::min(nx - 1, gx + step);
					int gz0 = std::max(0, gz - step), gz1 = std::min(nz - 1, gz + step);
					float dhx = (height[(size_t)gz * nx + gx1] - height[(size_t)gz * nx + gx0]) / ((gx1 - gx0) * cell);
					float dhz = (height[(size_t)gz1 * nx + gx] - height[(size_t)gz0 * nx + gx]) / ((gz1 - gz0) * cell);
					n = Vector3Normalize({ -dhx, 1.0f, -dhz });
					float slope = 1.0f - n.y;
					float t = std::clamp((slope - 0.12f) * 4.0f, 0.0f, 1.0f);
					float v = 0.88f + 0.24f * colorNoise.fbm(p.x / 30.0f, p.z / 30.0f, 3);
					c = { (unsigned char)std::clamp((base.r + (rock.r - base.r) * t) * v, 0.0f, 255.0f),
						  (unsigned char)std::clamp((base.g + (rock.g - base.g) * t) * v, 0.0f, 255.0f),
						  (unsigned char)std::clamp((base.b + (rock.b - base.b) * t) * v, 0.0f, 255.0f), 255 };
					minY = std::min(minY, p.y); maxY = std::max(maxY, p.y);
				};
				for (int rz = cz0; rz < cz1; ++rz) {
					for (int rx = cx0; rx < cx1; ++rx) {
						Vector3 p[4], n[4]; Color c[4];
						V(rx, rz, p[0], n[0], c[0]);
						V(rx + 1, rz, p[1], n[1], c[1]);
						V(rx + 1, rz + 1, p[2], n[2], c[2]);
						V(rx, rz + 1, p[3], n[3], c[3]);
						Vector2 t[4];
						for (int q = 0; q < 4; ++q) t[q] = { p[q].x / 9.0f, p[q].z / 9.0f };
						// CCW seen from above (+y): 0 -> 3 -> 2, 0 -> 2 -> 1
						mb.vertex(p[0], n[0], t[0], c[0]); mb.vertex(p[3], n[3], t[3], c[3]); mb.vertex(p[2], n[2], t[2], c[2]);
						mb.vertex(p[0], n[0], t[0], c[0]); mb.vertex(p[2], n[2], t[2], c[2]); mb.vertex(p[1], n[1], t[1], c[1]);
					}
				}
				TerrainChunk ch;
				ch.mesh = mb.upload();
				ch.bounds.min = { originX + cx0 * step * cell, minY, originZ + cz0 * step * cell };
				ch.bounds.max = { originX + cx1 * step * cell, maxY, originZ + cz1 * step * cell };
				chunks.push_back(ch);
			}
		}

		// --- Road surface strips per surface type ---
		MeshBuilder roads[(int)Surface::Count];
		MeshBuilder shoulder, marks, bridge, water;
		int N = (int)samples.size();
		const int rs = 2;
		Color white = WHITE;
		Color shoulderCol = env == "snow" ? Color{ 225, 230, 240, 255 } : Color{ 120, 110, 90, 255 };
		for (int i = 0; i + rs < N; i += rs) {
			const RoadSample& a = samples[i];
			const RoadSample& b = samples[i + rs];
			auto edge = [&](const RoadSample& s, float off, float lift) {
				return Vector3{ s.pos.x + s.right.x * off, s.pos.y + lift, s.pos.z + s.right.y * off };
			};
			float crownL = -0.07f;
			Vector3 aL = edge(a, -hw, crownL + 0.04f), aR = edge(a, hw, crownL + 0.04f), aC = edge(a, 0, 0.04f);
			Vector3 bL = edge(b, -hw, crownL + 0.04f), bR = edge(b, hw, crownL + 0.04f), bC = edge(b, 0, 0.04f);
			float va = a.s / 2.5f, vb = b.s / 2.5f;
			MeshBuilder& rb = roads[(int)a.surface];
			// two halves for the crown; CCW from above with right = -x side...
			Vector3 up = { 0, 1, 0 };
			auto quadUp = [&](MeshBuilder& m, Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3, Vector2 t0, Vector2 t1, Vector2 t2, Vector2 t3, Color c) {
				// p0,p1 at sample a (left->right), p2,p3 at sample b (right->left)
				Vector3 n = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(p3, p0), Vector3Subtract(p1, p0)));
				if (n.y < 0) n = Vector3Negate(n);
				(void)up;
				// Emit with winding that faces up.
				Vector3 cr = Vector3CrossProduct(Vector3Subtract(p1, p0), Vector3Subtract(p2, p0));
				if (cr.y >= 0) {
					m.vertex(p0, n, t0, c); m.vertex(p1, n, t1, c); m.vertex(p2, n, t2, c);
					m.vertex(p0, n, t0, c); m.vertex(p2, n, t2, c); m.vertex(p3, n, t3, c);
				}
				else {
					m.vertex(p0, n, t0, c); m.vertex(p2, n, t2, c); m.vertex(p1, n, t1, c);
					m.vertex(p0, n, t0, c); m.vertex(p3, n, t3, c); m.vertex(p2, n, t2, c);
				}
			};
			quadUp(rb, aL, aC, bC, bL, { 0, va }, { 0.5f * def.width / 2.5f, va }, { 0.5f * def.width / 2.5f, vb }, { 0, vb }, white);
			quadUp(rb, aC, aR, bR, bC, { 0.5f * def.width / 2.5f, va }, { def.width / 2.5f, va }, { def.width / 2.5f, vb }, { 0.5f * def.width / 2.5f, vb }, white);

			if (!a.bridge && !b.bridge) {
				for (float side : { -1.0f, 1.0f }) {
					float o0 = hw, o1 = hw + 1.0f, o2 = hw + 2.8f;
					auto sp = [&](const RoadSample& s, float off) {
						Vector3 p = edge(s, side * off, 0);
						p.y = heightAt(p.x, p.z) + 0.03f;
						if (off == hw) p.y = s.pos.y + crownL + 0.035f;
						return p;
					};
					// Skip verge strips that would lie over another part of the
					// road (the two legs of a hairpin).
					// A verge point belongs to this stretch only if its nearest
					// road sample is close by along the stage.
					bool foreign = false;
					for (const RoadSample* rsp : { &a, &b })
						for (float off : { o1, o2 }) {
							Vector3 q = edge(*rsp, side * off, 0);
							int ck = cellIndex(q.x, q.z);
							if (ck >= 0 && roadIdx[ck] >= 0 && std::abs(roadIdx[ck] - i) > 25) foreign = true;
						}
					if (foreign) continue;
					Color c0 = shoulderCol, c1 = shoulderCol;
					c1.a = 255;
					quadUp(shoulder, sp(a, o0), sp(a, o1), sp(b, o1), sp(b, o0), { 0, va }, { 0.3f, va }, { 0.3f, vb }, { 0, vb }, c0);
					quadUp(shoulder, sp(a, o1), sp(a, o2), sp(b, o2), sp(b, o1), { 0.3f, va }, { 0.8f, va }, { 0.8f, vb }, { 0.3f, vb }, c1);
				}
			}

			// Tarmac markings: dashed centre line + solid edge lines.
			if (a.surface == Surface::Tarmac && b.surface == Surface::Tarmac) {
				Color mc = { 235, 235, 225, 255 };
				if (std::fmod(a.s, 9.0f) < 4.5f) {
					quadUp(marks, edge(a, -0.08f, 0.055f), edge(a, 0.08f, 0.055f), edge(b, 0.08f, 0.055f), edge(b, -0.08f, 0.055f), {}, {}, {}, {}, mc);
				}
				for (float side : { -1.0f, 1.0f }) {
					float o = side * (hw - 0.35f);
					quadUp(marks, edge(a, o - 0.07f, crownL + 0.065f), edge(a, o + 0.07f, crownL + 0.065f), edge(b, o + 0.07f, crownL + 0.065f), edge(b, o - 0.07f, crownL + 0.065f), {}, {}, {}, {}, mc);
				}
			}

			// Bridge deck, rails, pillars.
			if (a.bridge) {
				bool concrete = env == "mountain";
				Color deck = concrete ? Color{ 150, 150, 150, 255 } : Color{ 120, 85, 55, 255 };
				Color rail = concrete ? Color{ 190, 190, 190, 255 } : Color{ 95, 65, 40, 255 };
				Vector3 mid = Vector3Lerp(a.pos, b.pos, 0.5f);
				float yaw = std::atan2(a.dir.x, a.dir.y);
				bridge.box({ mid.x, mid.y - 0.25f, mid.z }, { hw + 0.5f, 0.25f, rs * 0.5f + 0.05f }, deck, yaw);
				for (float side : { -1.0f, 1.0f }) {
					Vector3 rp = edge(a, side * (hw + 0.35f), 0);
					bridge.box({ rp.x, rp.y + 0.5f, rp.z }, { 0.07f, 0.5f, 0.07f }, rail, yaw);
					Vector3 rm = { (edge(a, side * (hw + 0.35f), 0).x + edge(b, side * (hw + 0.35f), 0).x) * 0.5f, mid.y + 1.0f,
								   (edge(a, side * (hw + 0.35f), 0).z + edge(b, side * (hw + 0.35f), 0).z) * 0.5f };
					bridge.box(rm, { 0.06f, 0.06f, rs * 0.5f + 0.05f }, rail, yaw);
				}
				if (((int)a.s) % 10 == 0) {
					for (float side : { -1.0f, 1.0f }) {
						Vector3 pp = edge(a, side * (hw - 0.5f), 0);
						bridge.box({ pp.x, pp.y - 5.0f, pp.z }, { 0.35f, 4.8f, 0.35f }, deck, yaw);
					}
				}
			}
		}
		// Water under bridges.
		for (auto& br : def.bridges) {
			RoadSample a = sampleAt(br.from - 12), b = sampleAt(br.to + 12);
			float y = std::min(a.pos.y, b.pos.y) - 6.6f;
			Vector3 c = Vector3Lerp(a.pos, b.pos, 0.5f);
			float len = Vector3Distance(a.pos, b.pos) * 0.5f + 10.0f;
			float yaw = std::atan2(a.dir.x, a.dir.y);
			Matrix m = MatrixMultiply(MatrixRotateY(yaw + PI * 0.5f), MatrixTranslate(c.x, y, c.z));
			Vector3 q[4] = { {-len, 0, -30}, {len, 0, -30}, {len, 0, 30}, {-len, 0, 30} };
			for (auto& v : q) v = Vector3Transform(v, m);
			water.vertex(q[0], { 0, 1, 0 }, { 0, 0 }, { 60, 110, 140, 200 });
			water.vertex(q[3], { 0, 1, 0 }, { 0, 1 }, { 60, 110, 140, 200 });
			water.vertex(q[2], { 0, 1, 0 }, { 1, 1 }, { 60, 110, 140, 200 });
			water.vertex(q[0], { 0, 1, 0 }, { 0, 0 }, { 60, 110, 140, 200 });
			water.vertex(q[2], { 0, 1, 0 }, { 1, 1 }, { 60, 110, 140, 200 });
			water.vertex(q[1], { 0, 1, 0 }, { 1, 0 }, { 60, 110, 140, 200 });
		}
		for (int i = 0; i < (int)Surface::Count; ++i) roadMesh[i] = roads[i].upload();
		shoulderMesh = shoulder.upload();
		markingsMesh = marks.upload();
		bridgeMesh = bridge.upload();
		waterMesh = water.upload();
		meshesBuilt = true;
	}

	// ------------------------------------------------------------------
	// Map
	// ------------------------------------------------------------------
	void drawStageMap(const Stage& stage, Rectangle r, float carS, Vector3 carPos, float carYaw, bool) {
		if (stage.samples.empty()) return;
		float minX = 1e9f, minZ = 1e9f, maxX = -1e9f, maxZ = -1e9f;
		for (size_t i = 0; i < stage.samples.size(); i += 8) {
			auto& s = stage.samples[i];
			minX = std::min(minX, s.pos.x); maxX = std::max(maxX, s.pos.x);
			minZ = std::min(minZ, s.pos.z); maxZ = std::max(maxZ, s.pos.z);
		}
		float pad = 12.0f;
		float sx = (r.width - 2 * pad) / std::max(1.0f, maxX - minX);
		float sz = (r.height - 2 * pad) / std::max(1.0f, maxZ - minZ);
		float sc = std::min(sx, sz);
		float offX = r.x + pad + ((r.width - 2 * pad) - (maxX - minX) * sc) * 0.5f;
		float offY = r.y + pad + ((r.height - 2 * pad) - (maxZ - minZ) * sc) * 0.5f;
		// Map x is mirrored so that "left" on screen is the driver's left when heading north (+z up).
		auto toMap = [&](Vector3 p) { return Vector2{ offX + (maxX - p.x) * sc, offY + (maxZ - p.z) * sc }; };
		Vector2 prev = toMap(stage.samples[0].pos);
		for (size_t i = 4; i < stage.samples.size(); i += 4) {
			Vector2 cur = toMap(stage.samples[i].pos);
			bool done = stage.samples[i].s < carS;
			Color c = done ? Color{ 255, 176, 0, 255 } : Color{ 220, 220, 225, 255 };
			if (stage.samples[i].bridge) c = Color{ 120, 170, 230, 255 };
			DrawLineEx(prev, cur, 3.0f, c);
			prev = cur;
		}
		auto marker = [&](float s, Color c) {
			Vector2 p = toMap(stage.sampleAt(s).pos);
			DrawCircleV(p, 5, c);
		};
		marker(stage.startS, { 40, 200, 80, 255 });
		for (float s : stage.splitS) marker(s, { 255, 190, 30, 255 });
		marker(stage.finishS, { 230, 50, 50, 255 });
		Vector2 cp = toMap(carPos);
		Vector2 fwd = { -std::sin(carYaw), -std::cos(carYaw) };
		Vector2 side = { -fwd.y, fwd.x };
		DrawTriangle({ cp.x + fwd.x * 10, cp.y + fwd.y * 10 }, { cp.x - fwd.x * 6 - side.x * 6, cp.y - fwd.y * 6 - side.y * 6 },
			{ cp.x - fwd.x * 6 + side.x * 6, cp.y - fwd.y * 6 + side.y * 6 }, { 255, 60, 60, 255 });
		DrawTriangle({ cp.x + fwd.x * 10, cp.y + fwd.y * 10 }, { cp.x - fwd.x * 6 + side.x * 6, cp.y - fwd.y * 6 + side.y * 6 },
			{ cp.x - fwd.x * 6 - side.x * 6, cp.y - fwd.y * 6 - side.y * 6 }, { 255, 60, 60, 255 });
	}
}
