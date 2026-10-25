// Splits the "LowPoly_Cars_01" OBJ (a line-up of ~35 vehicles as 212 loose
// objects) into one body mesh + one wheel mesh per vehicle, ready for the game.
//
//   carsplit big.obj --list
//       one line per vehicle: index, centre, size, object/triangle counts
//   carsplit big.obj --export INDEX out_prefix
//       writes out_prefix_body.obj, out_prefix_wheel.obj and prints metadata
//       (wheel centres/radius/width, body size, roof height) as key=value
//
// Vehicles are found by grouping objects whose footprints overlap. Wheels are
// found as groups of faces that are connected to each other, roughly round
// about the x axis, and sit on the ground. The body is recentred on its
// footprint with the ground at y = 0 and the front at +z.
//
// Build: g++ -O2 tools/carsplit.cpp -o carsplit.exe
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

struct V3 { float x = 0, y = 0, z = 0; };
struct Corner { int v, t, n; };
struct Tri { Corner c[3]; };
struct Box {
	V3 mn{ FLT_MAX, FLT_MAX, FLT_MAX }, mx{ -FLT_MAX, -FLT_MAX, -FLT_MAX };
	void add(const V3& p) { mn = { std::min(mn.x, p.x), std::min(mn.y, p.y), std::min(mn.z, p.z) }; mx = { std::max(mx.x, p.x), std::max(mx.y, p.y), std::max(mx.z, p.z) }; }
	V3 size() const { return { mx.x - mn.x, mx.y - mn.y, mx.z - mn.z }; }
	V3 centre() const { return { (mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f, (mn.z + mx.z) * 0.5f }; }
};
struct Obj { std::string name; std::vector<Tri> tris; Box box; };

static std::vector<V3> P, N;
static std::vector<std::pair<float, float>> T;
static std::vector<Obj> objs;

static void parse(const char* path) {
	FILE* f = fopen(path, "r");
	if (!f) { fprintf(stderr, "cannot open %s\n", path); exit(1); }
	static char line[1 << 16];
	while (fgets(line, sizeof line, f)) {
		if (line[0] == 'o' && line[1] == ' ') {
			Obj o; o.name = line + 2;
			while (!o.name.empty() && (o.name.back() == '\n' || o.name.back() == '\r')) o.name.pop_back();
			objs.push_back(o);
		}
		else if (line[0] == 'v' && line[1] == ' ') { V3 v; sscanf(line + 2, "%f %f %f", &v.x, &v.y, &v.z); P.push_back(v); }
		else if (line[0] == 'v' && line[1] == 't') { float u, w; sscanf(line + 3, "%f %f", &u, &w); T.push_back({ u, w }); }
		else if (line[0] == 'v' && line[1] == 'n') { V3 v; sscanf(line + 3, "%f %f %f", &v.x, &v.y, &v.z); N.push_back(v); }
		else if (line[0] == 'f' && line[1] == ' ' && !objs.empty()) {
			std::vector<Corner> cs;
			char* p = line + 2;
			while (*p) {
				while (*p == ' ') ++p;
				if (*p == '\n' || *p == '\r' || !*p) break;
				Corner c{ 0, 0, 0 };
				c.v = atoi(p);
				while (*p && *p != '/' && *p != ' ' && *p != '\n') ++p;
				if (*p == '/') { ++p; if (*p != '/') c.t = atoi(p); while (*p && *p != '/' && *p != ' ' && *p != '\n') ++p; if (*p == '/') { ++p; c.n = atoi(p); } }
				while (*p && *p != ' ' && *p != '\n') ++p;
				cs.push_back(c);
			}
			for (size_t i = 1; i + 1 < cs.size(); ++i) {
				Tri t; t.c[0] = cs[0]; t.c[1] = cs[i]; t.c[2] = cs[i + 1];
				objs.back().tris.push_back(t);
				for (auto& c : t.c) objs.back().box.add(P[c.v - 1]);
			}
		}
	}
	fclose(f);
}

struct Cluster { std::vector<int> objs; Box box; int tris = 0; };

static std::vector<Cluster> cluster() {
	int n = (int)objs.size();
	std::vector<int> parent(n);
	for (int i = 0; i < n; ++i) parent[i] = i;
	auto find = [&](int a) { while (parent[a] != a) a = parent[a] = parent[parent[a]]; return a; };
	auto overlap = [](const Box& a, const Box& b) {
		float ox = std::min(a.mx.x, b.mx.x) - std::max(a.mn.x, b.mn.x);
		float oz = std::min(a.mx.z, b.mx.z) - std::max(a.mn.z, b.mn.z);
		return ox > 0.02f && oz > 0.02f;
	};
	for (int i = 0; i < n; ++i)
		for (int j = i + 1; j < n; ++j)
			if (!objs[i].tris.empty() && !objs[j].tris.empty() && overlap(objs[i].box, objs[j].box)) parent[find(i)] = find(j);
	std::map<int, Cluster> m;
	for (int i = 0; i < n; ++i) {
		if (objs[i].tris.empty()) continue;
		Cluster& c = m[find(i)];
		c.objs.push_back(i);
		c.box.add(objs[i].box.mn); c.box.add(objs[i].box.mx);
		c.tris += (int)objs[i].tris.size();
	}
	std::vector<Cluster> out;
	for (auto& kv : m) out.push_back(kv.second);
	// Row by row (z), then left to right (x).
	std::sort(out.begin(), out.end(), [](const Cluster& a, const Cluster& b) {
		float za = a.box.centre().z, zb = b.box.centre().z;
		if (std::fabs(za - zb) > 3.0f) return za < zb;
		return a.box.centre().x < b.box.centre().x;
	});
	return out;
}

// Connected pieces of a cluster (triangles that share a vertex index, per object).
struct Piece { std::vector<std::pair<int, int>> tris; Box box; };   // (object, triangle)

static std::vector<Piece> pieces(const Cluster& c) {
	std::vector<Piece> out;
	for (int oi : c.objs) {
		const Obj& o = objs[oi];
		std::map<int, int> owner;     // vertex -> union-find root over triangle indices
		int nt = (int)o.tris.size();
		std::vector<int> parent(nt);
		for (int i = 0; i < nt; ++i) parent[i] = i;
		auto find = [&](int a) { while (parent[a] != a) a = parent[a] = parent[parent[a]]; return a; };
		for (int i = 0; i < nt; ++i)
			for (auto& cn : o.tris[i].c) {
				auto it = owner.find(cn.v);
				if (it == owner.end()) owner[cn.v] = i;
				else parent[find(i)] = find(it->second);
			}
		std::map<int, Piece> m;
		for (int i = 0; i < nt; ++i) {
			Piece& p = m[find(i)];
			p.tris.push_back({ oi, i });
			for (auto& cn : o.tris[i].c) p.box.add(P[cn.v - 1]);
		}
		for (auto& kv : m) out.push_back(kv.second);
	}
	return out;
}

struct WheelInfo { V3 centre; float radius, width; std::vector<int> piece; };

// Pieces that look like a wheel: round about x, near the ground, 0.5-1.1 m across.
static bool wheelLike(const Piece& p, float groundY) {
	V3 s = p.box.size();
	if (s.x < 0.12f || s.x > 0.6f) return false;
	if (s.y < 0.45f || s.y > 1.15f || s.z < 0.45f || s.z > 1.15f) return false;
	if (std::fabs(s.y - s.z) > 0.12f) return false;
	return p.box.mn.y < groundY + 0.12f;
}

int main(int argc, char** argv) {
	if (argc < 3) { printf("usage: carsplit big.obj --list | --export INDEX prefix\n"); return 1; }
	parse(argv[1]);
	std::vector<Cluster> cl = cluster();
	if (strcmp(argv[2], "--list") == 0) {
		for (size_t i = 0; i < cl.size(); ++i) {
			V3 c = cl[i].box.centre(), s = cl[i].box.size();
			std::vector<Piece> ps = pieces(cl[i]);
			int w = 0;
			for (auto& p : ps) if (wheelLike(p, cl[i].box.mn.y)) ++w;
			printf("%2zu  centre (%6.1f, %6.1f)  size %5.2f x %5.2f x %5.2f  objects %3zu  tris %6d  wheelPieces %d\n", i, c.x, c.z, s.x, s.y, s.z, cl[i].objs.size(), cl[i].tris, w);
		}
		return 0;
	}
	if (strcmp(argv[2], "--uvstats") == 0 && argc >= 4) {
		// Which palette cells a vehicle uses, with the height range of the faces using each.
		const Cluster& c = cl[atoi(argv[3])];
		struct S { int n = 0; float ymin = 9, ymax = -9, zmin = 9, zmax = -9, xmax = 0; };
		std::map<std::pair<int, int>, S> m;
		float ground = c.box.mn.y;
		for (int oi : c.objs)
			for (auto& t : objs[oi].tris) {
				float u = 0, w = 0, y = 0, z = 0, x = 0;
				for (auto& cn : t.c) { u += T[cn.t - 1].first; w += T[cn.t - 1].second; y += P[cn.v - 1].y; z += P[cn.v - 1].z; x += P[cn.v - 1].x; }
				u /= 3; w /= 3; y = y / 3 - ground; z /= 3; x /= 3;
				S& s = m[{ (int)std::floor(u * 20), (int)std::floor(w * 20) }];
				++s.n; s.ymin = std::min(s.ymin, y); s.ymax = std::max(s.ymax, y); s.zmin = std::min(s.zmin, z); s.zmax = std::max(s.zmax, z); s.xmax = std::max(s.xmax, std::fabs(x));
			}
		std::vector<std::pair<std::pair<int, int>, S>> v(m.begin(), m.end());
		std::sort(v.begin(), v.end(), [](auto& a, auto& b) { return a.second.n > b.second.n; });
		for (auto& e : v) printf("uv(%.2f,%.2f)  tris %4d  y %.2f..%.2f\n", e.first.first / 20.0f, e.first.second / 20.0f, e.second.n, e.second.ymin, e.second.ymax);
		return 0;
	}
	if (strcmp(argv[2], "--profile") == 0 && argc >= 4) {
		// Side silhouette along the centre line: highest point per 0.2 m of length,
		// to read off bonnet / windscreen / roof / boot heights.
		const Cluster& c = cl[atoi(argv[3])];
		float ground = c.box.mn.y;
		float zmin = c.box.mn.z, zmax = c.box.mx.z;
		int bins = (int)((zmax - zmin) / 0.2f) + 1;
		std::vector<float> top(bins, -1);
		for (int oi : c.objs)
			for (auto& t : objs[oi].tris)
				for (int i = 0; i <= 14; ++i)
					for (int j = 0; j <= 14 - i; ++j) {
						float a = i / 14.0f, b2 = j / 14.0f, c2 = 1 - a - b2;
						const V3 &p0 = P[t.c[0].v - 1], &p1 = P[t.c[1].v - 1], &p2 = P[t.c[2].v - 1];
						V3 p{ p0.x * a + p1.x * b2 + p2.x * c2, p0.y * a + p1.y * b2 + p2.y * c2, p0.z * a + p1.z * b2 + p2.z * c2 };
						if (std::fabs(p.x - (c.box.mn.x + c.box.mx.x) * 0.5f) > 0.5f) continue;
						int bi = (int)((p.z - zmin) / 0.2f);
						if (bi >= 0 && bi < bins) top[bi] = std::max(top[bi], p.y - ground);
					}
		float cz = (zmin + zmax) * 0.5f;
		for (int b = bins - 1; b >= 0; --b) {
			printf("z %+5.2f  y %.2f ", zmin + b * 0.2f - cz, top[b]);
			for (int k = 0; k < (int)(top[b] * 25); ++k) putchar('#');
			putchar('\n');
		}
		return 0;
	}
	if (strcmp(argv[2], "--export") != 0 || argc < 5) return 1;
	int idx = atoi(argv[3]);
	std::string prefix = argv[4];
	if (idx < 0 || idx >= (int)cl.size()) { fprintf(stderr, "bad index\n"); return 1; }
	const Cluster& c = cl[idx];
	float ground = c.box.mn.y;
	std::vector<Piece> ps = pieces(c);

	// Wheels: round pieces, then any smaller piece sitting inside a wheel's bounds (rims).
	std::vector<WheelInfo> wheels;
	std::vector<int> isWheelPiece(ps.size(), -1);
	for (size_t i = 0; i < ps.size(); ++i)
		if (wheelLike(ps[i], ground)) {
			WheelInfo w; w.centre = ps[i].box.centre(); w.radius = ps[i].box.size().y * 0.5f; w.width = ps[i].box.size().x;
			w.piece.push_back((int)i);
			isWheelPiece[i] = (int)wheels.size();
			wheels.push_back(w);
		}
	for (size_t i = 0; i < ps.size(); ++i) {
		if (isWheelPiece[i] >= 0) continue;
		V3 pc = ps[i].box.centre();
		for (size_t w = 0; w < wheels.size(); ++w) {
			V3 wc = wheels[w].centre;
			if (std::fabs(pc.x - wc.x) < 0.25f && std::fabs(pc.y - wc.y) < wheels[w].radius * 0.75f && std::fabs(pc.z - wc.z) < wheels[w].radius * 0.75f
				&& ps[i].box.size().y < wheels[w].radius * 1.7f && ps[i].box.size().z < wheels[w].radius * 1.7f) {
				isWheelPiece[i] = (int)w; wheels[w].piece.push_back((int)i); break;
			}
		}
	}
	// Four wheels: the lowest-centred four of equal size.
	if (wheels.size() < 4) { fprintf(stderr, "found only %zu wheels\n", wheels.size()); }

	float cx = (c.box.mn.x + c.box.mx.x) * 0.5f, cz = (c.box.mn.z + c.box.mx.z) * 0.5f;
	// Body centre in x/z from the non-wheel geometry only, so the wheels don't bias it.
	Box bodyBox;
	for (size_t i = 0; i < ps.size(); ++i) if (isWheelPiece[i] < 0) { bodyBox.add(ps[i].box.mn); bodyBox.add(ps[i].box.mx); }
	cx = (bodyBox.mn.x + bodyBox.mx.x) * 0.5f; cz = (bodyBox.mn.z + bodyBox.mx.z) * 0.5f;

	auto writePieces = [&](const std::string& path, const std::vector<int>& which, V3 origin, float maxCentroidY = 1e9f) {
		FILE* f = fopen(path.c_str(), "w");
		std::map<int, int> vm, tm, nm;
		std::vector<int> vo, to, no;
		auto remap = [](std::map<int, int>& m, std::vector<int>& order, int k) { auto it = m.find(k); if (it != m.end()) return it->second; int id = (int)order.size() + 1; m[k] = id; order.push_back(k); return id; };
		std::vector<std::string> faces;
		for (int pi : which)
			for (auto& ot : ps[pi].tris) {
				const Tri& t = objs[ot.first].tris[ot.second];
				if (maxCentroidY < 1e8f) {
					float cy = (P[t.c[0].v - 1].y + P[t.c[1].v - 1].y + P[t.c[2].v - 1].y) / 3 - origin.y;
					if (cy > maxCentroidY) continue;
				}
				char b[160]; int o = 0;
				o += snprintf(b + o, sizeof b - o, "f");
				for (auto& cn : t.c) o += snprintf(b + o, sizeof b - o, " %d/%d/%d", remap(vm, vo, cn.v), remap(tm, to, cn.t), remap(nm, no, cn.n));
				faces.push_back(b);
			}
		for (int k : vo) fprintf(f, "v %.4f %.4f %.4f\n", P[k - 1].x - origin.x, P[k - 1].y - origin.y, P[k - 1].z - origin.z);
		for (int k : to) fprintf(f, "vt %.5f %.5f\n", T[k - 1].first, T[k - 1].second);
		for (int k : no) fprintf(f, "vn %.4f %.4f %.4f\n", N[k - 1].x, N[k - 1].y, N[k - 1].z);
		for (auto& s : faces) fprintf(f, "%s\n", s.c_str());
		fclose(f);
		return (int)faces.size();
	};

	std::vector<int> body;
	for (size_t i = 0; i < ps.size(); ++i) if (isWheelPiece[i] < 0) body.push_back((int)i);

	// --- Side profile along the centre line (highest surface per 0.2 m) ---
	const float bin = 0.2f;
	int bins = (int)((bodyBox.mx.z - bodyBox.mn.z) / bin) + 1;
	std::vector<float> top(bins, -1.0f);
	for (int pi : body)
		for (auto& ot : ps[pi].tris) {
			const Tri& t = objs[ot.first].tris[ot.second];
			const V3 &p0 = P[t.c[0].v - 1], &p1 = P[t.c[1].v - 1], &p2 = P[t.c[2].v - 1];
			for (int i = 0; i <= 14; ++i)
				for (int j = 0; j <= 14 - i; ++j) {
					float a = i / 14.0f, b2 = j / 14.0f, c2 = 1 - a - b2;
					V3 p{ p0.x * a + p1.x * b2 + p2.x * c2, p0.y * a + p1.y * b2 + p2.y * c2, p0.z * a + p1.z * b2 + p2.z * c2 };
					if (std::fabs(p.x - cx) > 0.5f) continue;
					int bi = (int)((p.z - bodyBox.mn.z) / bin);
					if (bi >= 0 && bi < bins) top[bi] = std::max(top[bi], p.y - ground);
				}
		}
	float roofY = 0;
	for (float v : top) roofY = std::max(roofY, v);
	// Bonnet/boot level: the most common height among the lower bins.
	std::map<int, int> hist;
	for (float v : top) if (v > 0.3f && v < roofY * 0.8f) ++hist[(int)std::lround(v * 50)];
	int modeKey = 0, modeN = 0;
	for (auto& kv : hist) if (kv.second > modeN) { modeN = kv.second; modeKey = kv.first; }
	float belt = modeN ? modeKey / 50.0f : roofY * 0.65f;
	auto zOf = [&](int b) { return bodyBox.mn.z + (b + 0.5f) * bin - cz; };
	// Front: first bin (from the nose) rising above the belt; first reaching the roof.
	int bFront = bins - 1;
	while (bFront > 0 && top[bFront] < belt + 0.07f) --bFront;
	int bRoofF = bins - 1;
	while (bRoofF > 0 && top[bRoofF] < roofY - 0.08f) --bRoofF;
	// Rear: from the middle of the roof backwards.
	int bRoofB = bRoofF;
	while (bRoofB > 0 && top[bRoofB - 1] >= roofY - 0.1f) --bRoofB;
	int bBack = bRoofB;
	while (bBack > 0 && top[bBack - 1] > belt + 0.07f) --bBack;
	float cabFront = zOf(bFront) + bin * 0.5f, roofFront = zOf(bRoofF) - bin * 0.5f;
	float cabBack = zOf(bBack) - bin * 0.5f, roofBack = zOf(bRoofB) - bin * 0.5f;

	int bt = writePieces(prefix + "_body.obj", body, { cx, ground, cz });
	int lt = writePieces(prefix + "_lower.obj", body, { cx, ground, cz }, belt + 0.03f);

	// One wheel (front-left-most, i.e. largest x then largest z), centred on its axle.
	int best = -1;
	for (size_t w = 0; w < wheels.size(); ++w)
		if (best < 0 || wheels[w].centre.x + wheels[w].centre.z * 0.01f > wheels[best].centre.x + wheels[best].centre.z * 0.01f) best = (int)w;
	int wt = 0;
	if (best >= 0) wt = writePieces(prefix + "_wheel.obj", wheels[best].piece, wheels[best].centre);

	float rad = 0, wid = 0, track = 0, zf = -1e9f, zr = 1e9f;
	for (auto& w : wheels) {
		rad += w.radius; wid += w.width; track += std::fabs(w.centre.x - cx);
		zf = std::max(zf, w.centre.z - cz); zr = std::min(zr, w.centre.z - cz);
	}
	int nw = std::max<int>(1, (int)wheels.size());
	rad /= nw; wid /= nw; track /= nw;
	V3 bs = bodyBox.size();
	FILE* j = fopen((prefix + ".json").c_str(), "w");
	fprintf(j, "{\n  \"length\": %.3f, \"width\": %.3f, \"height\": %.3f,\n", bs.z, bs.x, roofY);
	fprintf(j, "  \"wheelRadius\": %.3f, \"wheelWidth\": %.3f, \"trackHalf\": %.3f, \"frontAxleZ\": %.3f, \"rearAxleZ\": %.3f,\n", rad, wid, track, zf, zr);
	fprintf(j, "  \"belt\": %.3f, \"roofY\": %.3f, \"cabFront\": %.3f, \"cabBack\": %.3f, \"roofFront\": %.3f, \"roofBack\": %.3f\n}\n", belt, roofY, cabFront, cabBack, roofFront, roofBack);
	fclose(j);
	printf("tris body %d (lower %d) wheel %d, wheels %zu | %.2f x %.2f x %.2f | wheel r %.3f track %.3f axles %.3f/%.3f | belt %.2f roof %.2f cabFront %.2f cabBack %.2f roofFront %.2f roofBack %.2f\n",
		bt, lt, wt, wheels.size(), bs.x, roofY, bs.z, rad, track, zf, zr, belt, roofY, cabFront, cabBack, roofFront, roofBack);
	return 0;
}
