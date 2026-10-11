#include "game/carRender.h"
#include "game/world.h"
#include "game/meshBuilder.h"
#include "core/ui.h"
#include "core/settings.h"
#include <raymath.h>
#include <rlgl.h>
#include <cmath>
#include <algorithm>
#include <functional>
#include <string>

namespace rally {
	namespace {
		struct Section { float z, halfW, yBot, yTop, topInset; };

		// Cross-section polygon (x left-positive), 8 points.
		void sectionPoints(const Section& s, Vector3 out[8]) {
			float w = s.halfW, b = s.yBot, t = s.yTop, in = s.topInset;
			float c = std::min(0.07f, (t - b) * 0.25f);
			out[0] = { w - 0.05f, b, s.z };
			out[1] = { w, b + c, s.z };
			out[2] = { w, t - c, s.z };
			out[3] = { w - in, t, s.z };
			out[4] = { -w + in, t, s.z };
			out[5] = { -w, t - c, s.z };
			out[6] = { -w, b + c, s.z };
			out[7] = { -w + 0.05f, b, s.z };
		}

		// Emits a quad facing away from `center`.
		void outwardQuad(MeshBuilder& mb, Vector3 a, Vector3 b, Vector3 c, Vector3 d, Vector3 center, Color col) {
			Vector3 n = Vector3CrossProduct(Vector3Subtract(b, a), Vector3Subtract(c, a));
			Vector3 mid = Vector3Scale(Vector3Add(Vector3Add(a, b), Vector3Add(c, d)), 0.25f);
			if (Vector3DotProduct(n, Vector3Subtract(mid, center)) < 0) mb.quad(a, d, c, b, col);
			else mb.quad(a, b, c, d, col);
		}

		using FaceFn = std::function<void(int band, int face, Color& col, bool& glass)>;

		void loft(MeshBuilder& solid, MeshBuilder* glass, const std::vector<Section>& secs, FaceFn fn) {
			float cy = 0;
			for (auto& s : secs) cy += (s.yBot + s.yTop) * 0.5f;
			cy /= secs.size();
			for (size_t i = 0; i + 1 < secs.size(); ++i) {
				Vector3 A[8], B[8];
				sectionPoints(secs[i], A);
				sectionPoints(secs[i + 1], B);
				Vector3 center = { 0, cy, (secs[i].z + secs[i + 1].z) * 0.5f };
				for (int j = 0; j < 8; ++j) {
					int k = (j + 1) % 8;
					Color col = WHITE; bool isGlass = false;
					fn((int)i, j, col, isGlass);
					if (col.a == 0) continue; // face dropped
					MeshBuilder& target = (isGlass && glass) ? *glass : solid;
					outwardQuad(target, A[j], A[k], B[k], B[j], center, col);
				}
			}
			// End caps.
			for (int end = 0; end < 2; ++end) {
				const Section& s = end == 0 ? secs.front() : secs.back();
				Vector3 P[8];
				sectionPoints(s, P);
				Color col = WHITE; bool g = false;
				fn(end == 0 ? -1 : -2, 0, col, g);
				if (col.a == 0) continue;
				Vector3 c = { 0, (s.yBot + s.yTop) * 0.5f, s.z };
				Vector3 outward = { 0, 0, end == 0 ? -1.0f : 1.0f };
				for (int j = 0; j < 8; ++j) {
					Vector3 a = P[j], b = P[(j + 1) % 8];
					Vector3 n = Vector3CrossProduct(Vector3Subtract(a, c), Vector3Subtract(b, c));
					if (Vector3DotProduct(n, outward) >= 0) solid.tri(c, a, b, col);
					else solid.tri(c, b, a, col);
				}
			}
		}

		// Cylinder along the local x axis.
		void cylinderX(MeshBuilder& mb, Vector3 c, float r, float halfW, int sides, Color side, Color cap) {
			for (int i = 0; i < sides; ++i) {
				float a0 = 2 * PI * i / sides, a1 = 2 * PI * (i + 1) / sides;
				Vector3 p0 = { c.x - halfW, c.y + std::cos(a0) * r, c.z + std::sin(a0) * r };
				Vector3 p1 = { c.x - halfW, c.y + std::cos(a1) * r, c.z + std::sin(a1) * r };
				Vector3 q0 = { c.x + halfW, p0.y, p0.z }, q1 = { c.x + halfW, p1.y, p1.z };
				outwardQuad(mb, p0, p1, q1, q0, c, side);
				Vector3 cl = { c.x - halfW, c.y, c.z }, cr = { c.x + halfW, c.y, c.z };
				Vector3 n = Vector3CrossProduct(Vector3Subtract(p0, cl), Vector3Subtract(p1, cl));
				if (n.x < 0) mb.tri(cl, p0, p1, cap); else mb.tri(cl, p1, p0, cap);
				n = Vector3CrossProduct(Vector3Subtract(q0, cr), Vector3Subtract(q1, cr));
				if (n.x > 0) mb.tri(cr, q0, q1, cap); else mb.tri(cr, q1, q0, cap);
			}
		}

		Color shade(Color c, float k) {
			return { (unsigned char)std::clamp(c.r * k, 0.0f, 255.0f), (unsigned char)std::clamp(c.g * k, 0.0f, 255.0f), (unsigned char)std::clamp(c.b * k, 0.0f, 255.0f), c.a };
		}
	}

	void CarRenderer::unload() {
		if (!built_) return;
		for (Mesh* m : { &body_, &cabin_, &glass_, &wheel_, &interior_, &crew_, &steering_, &headL_, &headR_, &pod_, &tail_, &numberQuad_ })
			if (m->vertexCount) UnloadMesh(*m);
		UnloadRenderTexture(numberRT_);
		built_ = false;
	}

	void CarRenderer::build(const CarSpec& spec) {
		unload();
		spec_ = spec;
		const float W = spec.halfExtents.x, Hh = spec.halfExtents.y, Lz = spec.halfExtents.z;
		const Color bodyC = spec.bodyColor, stripe = spec.stripeColor, accent = spec.accentColor;
		const Color black = { 28, 28, 32, 255 }, glassC = { 40, 55, 70, 150 };
		MeshBuilder body, glass, interior, crew, wheel, steer, hl, hr, pod, tail;

		// --- Body style ---
		// Proportions and details per style; the physics box stays the same.
		struct Shape {
			float beltOff = 0.02f, roofH = 0.5f, cabBack = -1.6f, cabFront = 0.8f;
			float rearSlope = 0.42f, frontSlope = 0.72f, roofInset = 0.16f;
			float noseDrop = 0.24f, tailDrop = 0.04f;
			int wing = 0;          // 0 none, 1 roof spoiler, 2 ducktail, 3 big rear wing
			float flare = 0.0f;    // wheel-arch flares
			bool flareBlack = false, roofScoop = false, splitter = false, diffuser = false;
			bool quadLights = false, roundLights = false, roofRack = false, bullbar = false;
			bool snorkel = false, louvres = false, bonnetVent = false, wedgeHull = false;
		} sh;
		switch (spec.style) {
		case 1: // wedge supercar (Bianca)
			sh.beltOff = -0.02f; sh.roofH = 0.42f; sh.cabBack = -1.25f; sh.cabFront = 0.35f; sh.rearSlope = 0.55f;
			sh.noseDrop = 0.32f; sh.wing = 2; sh.flare = 0.05f; sh.louvres = true; sh.wedgeHull = true;
			break;
		case 2: // supermini (Vespera)
			sh.beltOff = 0.03f; sh.roofH = 0.52f; sh.cabBack = -Lz + 0.48f; sh.cabFront = 0.62f; sh.rearSlope = 0.22f;
			sh.frontSlope = 0.7f; sh.roofInset = 0.15f; sh.noseDrop = 0.22f; sh.wing = 1; sh.flare = 0.04f; sh.splitter = true;
			break;
		case 3: // saloon (Imperia)
			sh.roofH = 0.48f; sh.cabBack = -1.3f; sh.cabFront = 0.72f; sh.rearSlope = 0.5f; sh.frontSlope = 0.68f;
			sh.noseDrop = 0.2f; sh.wing = 3; sh.flare = 0.07f; sh.roofScoop = true; sh.bonnetVent = true; sh.splitter = true;
			break;
		case 4: // Group B coupe (Quattra)
			sh.beltOff = 0.0f; sh.roofH = 0.46f; sh.cabBack = -1.25f; sh.cabFront = 0.35f; sh.rearSlope = 0.3f;
			sh.frontSlope = 0.55f; sh.roofInset = 0.12f; sh.noseDrop = 0.1f; sh.wing = 3; sh.flare = 0.14f;
			sh.splitter = true; sh.quadLights = true; sh.bonnetVent = true; sh.louvres = true;
			break;
		case 5: // classic mini (Mito)
			sh.beltOff = 0.04f; sh.roofH = 0.5f; sh.cabBack = -Lz + 0.37f; sh.cabFront = 0.5f; sh.rearSlope = 0.08f;
			sh.frontSlope = 0.38f; sh.roofInset = 0.12f; sh.noseDrop = 0.14f; sh.tailDrop = 0.06f;
			sh.flare = 0.04f; sh.flareBlack = true; sh.roundLights = true;
			break;
		case 6: // modern WRC (Vortex)
			sh.beltOff = 0.0f; sh.roofH = 0.48f; sh.cabBack = -1.5f; sh.cabFront = 0.7f; sh.rearSlope = 0.45f;
			sh.frontSlope = 0.75f; sh.roofInset = 0.18f; sh.noseDrop = 0.3f; sh.wing = 3; sh.flare = 0.16f;
			sh.splitter = true; sh.diffuser = true; sh.roofScoop = true; sh.bonnetVent = true;
			break;
		case 7: // 70s two-door saloon (Escala)
			sh.roofH = 0.46f; sh.cabBack = -1.0f; sh.cabFront = 0.35f; sh.rearSlope = 0.3f; sh.frontSlope = 0.5f;
			sh.roofInset = 0.08f; sh.noseDrop = 0.06f; sh.tailDrop = 0.0f; sh.wing = 2; sh.flare = 0.07f;
			sh.flareBlack = true; sh.quadLights = true; sh.roundLights = true;
			break;
		case 8: // rally-raid 4x4 (Banshee)
			sh.beltOff = 0.0f; sh.roofH = 0.58f; sh.cabBack = -1.6f; sh.cabFront = 0.7f; sh.rearSlope = 0.12f;
			sh.frontSlope = 0.55f; sh.roofInset = 0.1f; sh.noseDrop = 0.12f; sh.flare = 0.12f; sh.flareBlack = true;
			sh.roofRack = true; sh.bullbar = true; sh.snorkel = true; sh.roundLights = true;
			break;
		default: // 0: 90s turbo hatch (Kestrel)
			sh.flare = 0.06f; sh.wing = 1; sh.roofScoop = true; sh.splitter = true; sh.bonnetVent = true;
			break;
		}
		const bool wedge = sh.wedgeHull;
		const float yb = -Hh + 0.04f;          // body bottom
		const float belt = Hh + sh.beltOff;    // top of the lower body
		float roofY = belt + sh.roofH;
		float cabFront = sh.cabFront, cabBack = sh.cabBack;
		const Color roofC = spec.roofColor.a > 0 ? spec.roofColor : shade(bodyC, 0.95f);
		const Color flareC = sh.flareBlack ? black : bodyC;

		// --- Lower body ---
		std::vector<Section> lower;
		if (!wedge) {
			lower = {
				{ -Lz, W - 0.08f, yb + 0.06f, belt - 0.04f - sh.tailDrop, 0.05f },
				{ -Lz + 0.12f, W, yb, belt - sh.tailDrop * 0.5f, 0.05f },
				{ cabBack, W, yb, belt, 0.05f },
				{ cabFront, W, yb, belt - 0.01f, 0.05f },
				{ Lz - 0.5f, W, yb, belt - sh.noseDrop * 0.15f, 0.05f },
				{ Lz - 0.12f, W - 0.03f, yb, belt - sh.noseDrop * 0.6f, 0.08f },
				{ Lz, W - 0.12f, yb + 0.06f, belt - sh.noseDrop, 0.10f },
			};
		}
		else {
			lower = {
				{ -Lz, W - 0.06f, yb + 0.06f, belt + 0.02f, 0.05f },
				{ -Lz + 0.1f, W, yb, belt + 0.04f, 0.05f },
				{ cabBack, W, yb, belt + 0.027f, 0.05f },
				{ -0.2f, W, yb, belt, 0.05f },
				{ cabFront, W - 0.01f, yb, belt - 0.06f, 0.06f },
				{ Lz - 0.3f, W - 0.02f, yb, belt - 0.2f, 0.08f },
				{ Lz, W - 0.1f, yb + 0.05f, belt - 0.32f, 0.1f },
			};
		}
		Color trim = { 45, 45, 50, 255 };
		Color lowerTone = spec.livery == 1 ? accent : bodyC;   // two-tone livery
		loft(body, nullptr, lower, [&](int band, int face, Color& c, bool&) {
			c = bodyC;
			if (face == 0 || face == 6) c = shade(lowerTone, 0.7f);          // lower chamfers
			if (face == 7) c = shade(bodyC, 0.35f);                         // underside
			if (band == -1 || band == -2) c = black;                        // bumpers
			// Under the cabin the top of the hull is the interior floor/door tops.
			if (band >= 0 && lower[band].z >= cabBack - 1e-3f && lower[band + 1].z <= cabFront + 1e-3f && (face == 3 || face == 2 || face == 4)) c = trim;
		});

		// --- Cabin / greenhouse (own mesh: drawn inside-out for in-car views) ---
		MeshBuilder cabin;
		std::vector<Section> cab = {
			{ cabBack, W - 0.06f, belt - 0.01f, belt + 0.05f, 0.08f },
			{ cabBack + sh.rearSlope, W - 0.1f, belt - 0.01f, roofY, sh.roofInset },
			{ cabFront - sh.frontSlope, W - 0.12f, belt - 0.01f, roofY, sh.roofInset },
			{ cabFront, W - 0.06f, belt - 0.01f, belt + 0.05f, 0.08f },
		};
		loft(cabin, &glass, cab, [&](int band, int face, Color& c, bool& g) {
			c = bodyC;
			bool sideFace = face == 1 || face == 5 || face == 2 || face == 4;
			if (band == 0 && face == 3) { g = true; c = glassC; }      // rear window
			if (band == 2 && face == 3) { g = true; c = glassC; }      // windscreen
			if (band == 1 && sideFace) { g = true; c = glassC; }       // side windows
			if (band == 1 && face == 3) c = roofC;                     // roof
			if (face == 7 || face == 0 || face == 6 || band < 0) c.a = 0; // no floor/caps: it sits on the hull
		});
		cabin_ = cabin.upload();

		// --- Livery ---
		switch (spec.livery) {
		case 1: // two-tone: lower sides in the accent colour + a pinstripe
			for (float sx : { 1.0f, -1.0f }) {
				body.box({ sx * (W + 0.005f), (yb + belt - 0.2f) * 0.5f, 0.0f }, { 0.005f, (belt - 0.2f - yb) * 0.5f - 0.04f, Lz - 0.2f }, accent);
				body.box({ sx * (W + 0.007f), belt - 0.19f, 0.0f }, { 0.005f, 0.018f, Lz - 0.22f }, stripe);
			}
			break;
		case 2: // diagonal sweep along the doors
			for (float sx : { 1.0f, -1.0f }) {
				Matrix m = MatrixMultiply(MatrixRotateX(0.22f), MatrixTranslate(sx * (W + 0.006f), belt - 0.2f, 0.1f));
				body.boxTransformed({ 0.006f, 0.07f, Lz - 0.45f }, m, stripe);
				Matrix m2 = MatrixMultiply(MatrixRotateX(0.22f), MatrixTranslate(sx * (W + 0.007f), belt - 0.32f, 0.12f));
				body.boxTransformed({ 0.006f, 0.03f, Lz - 0.55f }, m2, accent);
			}
			break;
		case 3: // twin racing stripes over bonnet, roof and boot
			for (float sx : { 0.13f, -0.13f }) {
				body.box({ sx, belt - sh.noseDrop * 0.3f + 0.012f, (cabFront + Lz) * 0.5f }, { 0.06f, 0.012f, (Lz - cabFront) * 0.5f - 0.05f }, stripe);
				body.box({ sx, roofY + 0.006f, (cabBack + sh.rearSlope + cabFront - sh.frontSlope) * 0.5f }, { 0.06f, 0.006f, (cabFront - sh.frontSlope - cabBack - sh.rearSlope) * 0.5f }, stripe);
			}
			for (float sx : { 1.0f, -1.0f }) body.box({ sx * (W + 0.006f), belt - 0.16f, 0.0f }, { 0.006f, 0.03f, Lz - 0.3f }, stripe);
			break;
		default: // side stripe + pinstripe
			for (float sx : { 1.0f, -1.0f }) {
				body.box({ sx * (W + 0.006f), belt - 0.16f, 0.0f }, { 0.006f, 0.05f, Lz - 0.25f }, stripe);
				body.box({ sx * (W + 0.006f), belt - 0.26f, 0.0f }, { 0.006f, 0.025f, Lz - 0.3f }, accent);
			}
			break;
		}
		if (spec.livery != 3 && !sh.roofRack)
			body.box({ 0, roofY + 0.005f, (cabBack + sh.rearSlope + cabFront - sh.frontSlope) * 0.5f }, { 0.18f, 0.005f, 0.35f }, stripe);

		// --- Aero ---
		if (sh.wing == 1) body.box({ 0.0f, roofY + 0.04f, cabBack + sh.rearSlope * 0.9f }, { W - 0.2f, 0.025f, 0.12f }, accent);
		if (sh.wing == 2) body.box({ 0.0f, belt + 0.06f - sh.tailDrop, -Lz + 0.14f }, { W - 0.12f, 0.02f, 0.14f }, black);
		if (sh.wing == 3) {
			float wy = belt + 0.3f - sh.tailDrop, wz = -Lz + 0.24f;
			body.box({ 0, wy, wz }, { W - 0.04f, 0.022f, 0.17f }, accent);
			for (float sx : { 1.0f, -1.0f }) {
				body.box({ sx * (W - 0.32f), (belt + wy) * 0.5f, wz + 0.04f }, { 0.025f, (wy - belt) * 0.5f, 0.06f }, black);
				body.box({ sx * (W - 0.02f), wy + 0.03f, wz }, { 0.012f, 0.09f, 0.19f }, black);
			}
		}
		if (sh.splitter) body.box({ 0, yb + 0.01f, Lz + 0.06f }, { W - 0.08f, 0.015f, 0.08f }, black);
		if (sh.diffuser) {
			body.box({ 0, yb + 0.04f, -Lz - 0.06f }, { W - 0.25f, 0.03f, 0.06f }, black);
			for (int i = -2; i <= 2; ++i) body.box({ i * 0.22f, yb + 0.09f, -Lz - 0.07f }, { 0.01f, 0.05f, 0.06f }, black);
		}
		if (sh.roofScoop) body.box({ 0, roofY + 0.035f, cabFront - sh.frontSlope - 0.12f }, { 0.13f, 0.035f, 0.13f }, accent);
		if (sh.bonnetVent) body.box({ 0, belt - sh.noseDrop * 0.25f + 0.006f, Lz - 0.62f }, { 0.28f, 0.008f, 0.16f }, black);
		if (sh.louvres)
			for (int i = 0; i < 5; ++i) body.box({ 0, belt + 0.03f + i * 0.004f, cabBack - 0.08f - i * 0.09f }, { W - 0.22f, 0.008f, 0.025f }, black);

		// --- Rally-raid kit ---
		if (sh.roofRack) {
			float ry = roofY + 0.08f, rz0 = cabBack + sh.rearSlope + 0.05f, rz1 = cabFront - sh.frontSlope - 0.05f;
			for (float sx : { 1.0f, -1.0f }) body.box({ sx * (W - 0.2f), ry, (rz0 + rz1) * 0.5f }, { 0.02f, 0.02f, (rz1 - rz0) * 0.5f }, black);
			for (float z : { rz0, (rz0 + rz1) * 0.5f, rz1 }) body.box({ 0, ry, z }, { W - 0.2f, 0.02f, 0.02f }, black);
			Matrix m = MatrixMultiply(MatrixRotateZ(PI * 0.5f), MatrixTranslate(0, ry + 0.12f, rz0 + 0.45f));
			MeshBuilder spare;
			cylinderX(spare, { 0, 0, 0 }, spec.wheelRadius, spec.wheelWidth * 0.5f, 16, Color{ 30, 30, 32, 255 }, Color{ 60, 60, 64, 255 });
			for (size_t k = 0; k + 2 < spare.pos.size(); k += 3) {
				Vector3 p = Vector3Transform({ spare.pos[k], spare.pos[k + 1], spare.pos[k + 2] }, m);
				Vector3 n = Vector3Transform({ spare.nrm[k], spare.nrm[k + 1], spare.nrm[k + 2] }, MatrixRotateZ(PI * 0.5f));
				body.vertex(p, n, { 0, 0 }, Color{ spare.col[k / 3 * 4], spare.col[k / 3 * 4 + 1], spare.col[k / 3 * 4 + 2], 255 });
			}
		}
		if (sh.bullbar) {
			float by = yb + 0.2f;
			body.box({ 0, by + 0.12f, Lz + 0.14f }, { W - 0.2f, 0.025f, 0.025f }, black);
			body.box({ 0, by - 0.02f, Lz + 0.14f }, { W - 0.15f, 0.025f, 0.025f }, black);
			for (float sx : { 0.3f, -0.3f }) body.box({ sx, by + 0.05f, Lz + 0.14f }, { 0.025f, 0.12f, 0.025f }, black);
		}
		if (sh.snorkel) {
			body.box({ -(W + 0.04f), (belt + roofY) * 0.5f, cabFront - 0.05f }, { 0.04f, (roofY - belt) * 0.5f + 0.05f, 0.04f }, black);
			body.box({ -(W + 0.04f), roofY + 0.07f, cabFront }, { 0.05f, 0.04f, 0.07f }, black);
		}

		// --- Bumpers, skid plate, mud flaps, exhaust, mirrors ---
		body.box({ 0, yb + 0.12f, Lz + 0.02f }, { W - 0.1f, 0.1f, 0.05f }, black);
		body.box({ 0, yb + 0.12f, -Lz - 0.02f }, { W - 0.08f, 0.1f, 0.05f }, black);
		body.box({ 0, yb - 0.01f, Lz - 0.6f }, { W - 0.25f, 0.02f, 0.5f }, Color{ 150, 150, 155, 255 });
		// Grille.
		body.box({ 0, belt - sh.noseDrop - 0.1f, Lz + 0.005f }, { W * 0.32f, 0.05f, 0.01f }, Color{ 18, 18, 20, 255 });
		for (float sx : { 1.0f, -1.0f }) {
			float flapW = spec.style == 7 || spec.style == 8 ? 0.17f : 0.13f;
			body.box({ sx * (spec.trackHalf - 0.02f), yb + 0.02f, spec.rearAxleZ - spec.wheelRadius - 0.08f }, { flapW, 0.15f, 0.01f }, Color{ 230, 230, 230, 255 });
			body.box({ sx * (spec.trackHalf - 0.02f), yb + 0.02f, spec.frontAxleZ - spec.wheelRadius - 0.1f }, { flapW, 0.12f, 0.01f }, black);
			body.box({ sx * (W + 0.06f), belt + 0.12f, cabFront - 0.1f }, { 0.05f, 0.04f, 0.06f }, bodyC);
			body.box({ sx * (W + 0.03f), belt + 0.12f, cabFront - 0.1f }, { 0.03f, 0.012f, 0.03f }, black);
		}
		body.box({ -0.45f, yb + 0.02f, -Lz - 0.05f }, { 0.05f, 0.05f, 0.08f }, Color{ 90, 90, 95, 255 });
		// Wheel arches: blackout inside, plus flares.
		const float wheelY = spec.mountY - spec.restLength + 0.08f;
		for (int i = 0; i < 4; ++i) {
			float sx = (i % 2 == 0) ? 1.0f : -1.0f;
			float z = i < 2 ? spec.frontAxleZ : spec.rearAxleZ;
			body.box({ sx * (W - 0.02f), wheelY + spec.wheelRadius * 0.5f, z }, { 0.03f, 0.12f, spec.wheelRadius + 0.08f }, black);
			if (sh.flare > 0.0f) {
				float fx = sx * (W - 0.02f + sh.flare * 0.5f);
				body.box({ fx, wheelY + spec.wheelRadius + 0.02f, z }, { sh.flare * 0.5f + 0.03f, 0.05f, spec.wheelRadius + 0.12f }, flareC);
				for (float dz : { -1.0f, 1.0f })
					body.box({ fx, wheelY + spec.wheelRadius * 0.45f, z + dz * (spec.wheelRadius + 0.09f) }, { sh.flare * 0.5f + 0.03f, spec.wheelRadius * 0.55f, 0.04f }, flareC);
			}
		}

		// --- Lights ---
		float lightY = belt - sh.noseDrop - 0.08f, lightZ = Lz - 0.02f;
		Color lampC = { 255, 250, 225, 255 };
		auto discZ = [](MeshBuilder& mb, Vector3 c, float r, Color col) {
			const int n = 14;
			for (int k = 0; k < n; ++k) {
				float a0 = 2 * PI * k / n, a1 = 2 * PI * (k + 1) / n;
				Vector3 p0 = { c.x + std::cos(a0) * r, c.y + std::sin(a0) * r, c.z };
				Vector3 p1 = { c.x + std::cos(a1) * r, c.y + std::sin(a1) * r, c.z };
				mb.tri(c, p0, p1, col); // faces +z (forward)
				// thin rim so it reads from the side
				outwardQuad(mb, p0, p1, { p1.x, p1.y, c.z - 0.03f }, { p0.x, p0.y, c.z - 0.03f }, { c.x, c.y, c.z - 0.015f }, Color{ 50, 50, 55, 255 });
			}
		};
		if (sh.roundLights) {
			if (sh.quadLights) {
				discZ(hl, { 0.42f, lightY, lightZ + 0.012f }, 0.075f, lampC); discZ(hl, { 0.64f, lightY, lightZ + 0.012f }, 0.075f, lampC);
				discZ(hr, { -0.42f, lightY, lightZ + 0.012f }, 0.075f, lampC); discZ(hr, { -0.64f, lightY, lightZ + 0.012f }, 0.075f, lampC);
			}
			else {
				discZ(hl, { W * 0.62f, lightY, lightZ + 0.012f }, 0.095f, lampC);
				discZ(hr, { -W * 0.62f, lightY, lightZ + 0.012f }, 0.095f, lampC);
			}
		}
		else if (sh.quadLights) {
			hl.box({ 0.42f, lightY, lightZ }, { 0.1f, 0.055f, 0.03f }, lampC); hl.box({ 0.66f, lightY, lightZ }, { 0.1f, 0.055f, 0.03f }, lampC);
			hr.box({ -0.42f, lightY, lightZ }, { 0.1f, 0.055f, 0.03f }, lampC); hr.box({ -0.66f, lightY, lightZ }, { 0.1f, 0.055f, 0.03f }, lampC);
		}
		else {
			hl.box({ 0.55f, lightY, lightZ }, { 0.16f, 0.06f, 0.03f }, lampC);
			hr.box({ -0.55f, lightY, lightZ }, { 0.16f, 0.06f, 0.03f }, lampC);
		}
		float podY = sh.bullbar ? lightY + 0.05f : lightY + 0.16f, podZ = sh.bullbar ? Lz + 0.17f : Lz + 0.07f;
		for (int i = 0; i < 4; ++i) {
			float x = -0.45f + i * 0.3f;
			discZ(pod, { x, podY, podZ + 0.01f }, 0.1f, Color{ 255, 248, 220, 255 });
		}
		pod.box({ 0, podY, podZ - 0.03f }, { 0.62f, 0.12f, 0.02f }, black);
		float tailY = belt - sh.tailDrop - 0.12f;
		tail.box({ W * 0.68f, tailY, -Lz - 0.005f }, { 0.17f, 0.05f, 0.02f }, Color{ 220, 20, 20, 255 });
		tail.box({ -W * 0.68f, tailY, -Lz - 0.005f }, { 0.17f, 0.05f, 0.02f }, Color{ 220, 20, 20, 255 });

		// --- Wheel (centred on origin, axle along x) ---
		const float R = spec.wheelRadius, hwW = spec.wheelWidth * 0.5f;
		cylinderX(wheel, { 0, 0, 0 }, R, hwW, 22, Color{ 28, 28, 30, 255 }, Color{ 38, 38, 40, 255 });           // tyre
		cylinderX(wheel, { 0, 0, 0 }, R * 0.97f, hwW + 0.003f, 22, Color{ 22, 22, 24, 255 }, Color{ 45, 45, 48, 255 }); // sidewall
		cylinderX(wheel, { 0, 0, 0 }, R * 0.64f, hwW + 0.008f, 16, shade(spec.rimColor, 0.8f), shade(spec.rimColor, 0.55f)); // rim barrel
		for (int i = 0; i < 5; ++i) {
			float a = i * 2 * PI / 5;
			Matrix m = MatrixMultiply(MatrixTranslate(0, R * 0.32f, 0), MatrixRotateX(a));
			wheel.boxTransformed({ hwW + 0.014f, R * 0.3f, 0.028f }, m, spec.rimColor);
		}
		cylinderX(wheel, { 0, 0, 0 }, R * 0.16f, hwW + 0.02f, 10, shade(spec.rimColor, 0.9f), shade(spec.rimColor, 1.1f));   // hub cap
		cylinderX(wheel, { 0, 0, 0 }, R * 0.5f, hwW - 0.02f, 16, Color{ 90, 90, 95, 255 }, Color{ 110, 110, 115, 255 });     // brake disc

		// --- Interior (seen from the in-car cameras) ---
		interior.box({ 0, belt + 0.02f, cabFront - 0.12f }, { W - 0.14f, 0.09f, 0.16f }, trim);           // dashboard
		interior.box({ 0, roofY - 0.03f, (cabFront + cabBack) * 0.5f }, { W - 0.18f, 0.02f, (cabFront - cabBack) * 0.42f }, Color{ 70, 70, 75, 255 }); // headliner
		for (float sx : { 1.0f, -1.0f }) {
			// A-pillars (as thin slanted boxes).
			Vector3 a = { sx * (W - 0.13f), belt + 0.04f, cabFront - 0.05f }, b = { sx * (W - 0.16f), roofY - 0.02f, cabFront - 0.7f };
			Vector3 mid = Vector3Scale(Vector3Add(a, b), 0.5f);
			float len = Vector3Distance(a, b) * 0.5f;
			float pitch = std::atan2(b.y - a.y, a.z - b.z);
			Matrix m = MatrixMultiply(MatrixRotateX(pitch), MatrixTranslate(mid.x, mid.y, mid.z));
			interior.boxTransformed({ 0.022f, 0.022f, len }, m, trim);
			// Roll-cage tubes.
			interior.box({ sx * (W - 0.2f), (belt + roofY) * 0.5f, cabBack + 0.75f }, { 0.025f, (roofY - belt) * 0.5f, 0.025f }, Color{ 200, 200, 205, 255 });
			// Seats.
			interior.box({ sx * 0.38f, belt - 0.12f, -0.55f }, { 0.22f, 0.06f, 0.24f }, Color{ 25, 25, 30, 255 });
			interior.box({ sx * 0.38f, belt + 0.18f, -0.78f }, { 0.22f, 0.32f, 0.05f }, Color{ 25, 25, 30, 255 });
		}
		interior.box({ 0, roofY - 0.05f, cabBack + 0.75f }, { W - 0.2f, 0.025f, 0.025f }, Color{ 200, 200, 205, 255 });
		// Crew: driver on the left (+x), co-driver on the right.
		Color suit1 = accent, suit2 = shade(accent, 0.8f);
		// Crew head height: under the roof whatever the body style.
		const float hc = std::min(belt + 0.5f, roofY - 0.2f);
		for (int seat = 0; seat < 2; ++seat) {
			float sx = seat == 0 ? 0.38f : -0.38f;
			Color suit = seat == 0 ? suit1 : suit2;
			crew.box({ sx, hc - 0.38f, -0.58f }, { 0.17f, 0.24f, 0.11f }, suit);
			crew.box({ sx, belt - 0.06f, -0.28f }, { 0.15f, 0.06f, 0.2f }, suit);
			MeshBuilder tmp;
			// Helmet as a rough sphere (octahedron-ish).
			Vector3 h = { sx, hc, -0.55f };
			float r = 0.13f;
			Vector3 top = { h.x, h.y + r, h.z }, bot = { h.x, h.y - r, h.z };
			Vector3 ring[8];
			for (int k = 0; k < 8; ++k) ring[k] = { h.x + std::cos(k * PI / 4) * r, h.y, h.z + std::sin(k * PI / 4) * r };
			Color helmet = seat == 0 ? Color{ 240, 240, 245, 255 } : stripe;
			for (int k = 0; k < 8; ++k) {
				Vector3 a = ring[k], b = ring[(k + 1) % 8];
				outwardQuad(crew, a, b, top, top, h, helmet);
				outwardQuad(crew, a, b, bot, bot, h, helmet);
			}
			crew.box({ sx, hc - 0.01f, -0.44f }, { 0.09f, 0.04f, 0.02f }, Color{ 20, 20, 25, 255 }); // visor
			if (seat == 1) crew.box({ sx, belt + 0.16f, -0.32f }, { 0.1f, 0.01f, 0.07f }, Color{ 250, 250, 240, 255 }); // pace-note book
		}
		// Steering wheel (origin at hub, in the x-y plane).
		{
			int segs = 14;
			float rr = 0.15f;
			for (int k = 0; k < segs; ++k) {
				float a0 = 2 * PI * k / segs, a1 = 2 * PI * (k + 1) / segs;
				Vector3 p0 = { std::cos(a0) * rr, std::sin(a0) * rr, 0 }, p1 = { std::cos(a1) * rr, std::sin(a1) * rr, 0 };
				Vector3 mid = Vector3Scale(Vector3Add(p0, p1), 0.5f);
				float len = Vector3Distance(p0, p1) * 0.5f;
				float ang = std::atan2(p1.y - p0.y, p1.x - p0.x);
				Matrix m = MatrixMultiply(MatrixRotateZ(ang), MatrixTranslate(mid.x, mid.y, mid.z));
				steer.boxTransformed({ len + 0.01f, 0.018f, 0.018f }, m, Color{ 30, 30, 30, 255 });
			}
			steer.box({ 0, -0.08f, 0 }, { 0.02f, 0.08f, 0.015f }, Color{ 60, 60, 60, 255 });
			steer.box({ 0, rr, 0.005f }, { 0.012f, 0.02f, 0.02f }, Color{ 230, 50, 40, 255 });
		}

		body_ = body.upload(); glass_ = glass.upload(); wheel_ = wheel.upload();
		interior_ = interior.upload(); crew_ = crew.upload(); steering_ = steer.upload();
		headL_ = hl.upload(); headR_ = hr.upload(); pod_ = pod.upload(); tail_ = tail.upload();

		// Door numbers.
		numberRT_ = LoadRenderTexture(128, 128);
		BeginTextureMode(numberRT_);
		ClearBackground(Color{ 0, 0, 0, 0 });
		DrawCircle(64, 64, 60, WHITE);
		DrawCircleLines(64, 64, 60, BLACK);
		std::string num = std::to_string(spec.number);
		Vector2 sz = MeasureTextEx(ui::font(), num.c_str(), 76, 1);
		DrawTextEx(ui::font(), num.c_str(), { 64 - sz.x * 0.5f, 64 - sz.y * 0.5f }, 76, 1, BLACK);
		EndTextureMode();
		SetTextureFilter(numberRT_.texture, TEXTURE_FILTER_BILINEAR);
		MeshBuilder q;
		q.quad({ 0, -0.5f, -0.5f }, { 0, -0.5f, 0.5f }, { 0, 0.5f, 0.5f }, { 0, 0.5f, -0.5f }, WHITE, { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 });
		numberQuad_ = q.upload();

		driverEye_ = { 0.38f, hc - 0.03f, -0.3f };
		codriverEye_ = { -0.38f, hc - 0.05f, -0.32f };
		bonnetEye_ = { 0.0f, belt + 0.34f, cabFront + 0.02f };
		spin_[0] = spin_[1] = spin_[2] = spin_[3] = 0;
		built_ = true;
	}

	void CarRenderer::draw(World& world, const CarVisual& v, float dt, bool lampsOn, CarView view) {
		if (!built_) return;
		Matrix bodyM = MatrixMultiply(QuaternionToMatrix(v.rotation), MatrixTranslate(v.position.x, v.position.y, v.position.z));
		Material& mat = world.colorMaterial();
		Texture2D whiteTex = world.whiteTexture();
		mat.maps[MATERIAL_MAP_DIFFUSE].texture = whiteTex;

		// Dirt tints the whole body toward mud brown.
		float d = v.dirt * 0.65f;
		Color dirtTint = { (unsigned char)(255 * (1 - d) + 150 * d), (unsigned char)(255 * (1 - d) + 122 * d), (unsigned char)(255 * (1 - d) + 92 * d), 255 };
		mat.maps[MATERIAL_MAP_DIFFUSE].color = dirtTint;
		DrawMesh(body_, mat, bodyM);
		if (view == CarView::Interior) {
			// From inside, show the cabin shell's inner faces (roof, pillars).
			rlDrawRenderBatchActive();
			rlSetCullFace(RL_CULL_FACE_FRONT);
			mat.maps[MATERIAL_MAP_DIFFUSE].color = Color{ 150, 150, 155, 255 };
			world.setEmissive(0.35f);
			DrawMesh(cabin_, mat, bodyM);
			world.setEmissive(0.0f);
			rlSetCullFace(RL_CULL_FACE_BACK);
			mat.maps[MATERIAL_MAP_DIFFUSE].color = dirtTint;
		}
		else DrawMesh(cabin_, mat, bodyM);

		// Numbers on both doors. (Material copies share their maps array, so
		// swap the texture in place and put it back afterwards.)
		{
			mat.maps[MATERIAL_MAP_DIFFUSE].texture = numberRT_.texture;
			rlDisableBackfaceCulling();
			for (float sx : { 1.0f, -1.0f }) {
				Matrix m = MatrixMultiply(MatrixScale(1, 0.34f, sx > 0 ? -0.34f : 0.34f), MatrixTranslate(sx * (spec_.halfExtents.x + 0.012f), spec_.halfExtents.y - 0.1f, -0.25f));
				DrawMesh(numberQuad_, mat, MatrixMultiply(m, bodyM));
			}
			rlEnableBackfaceCulling();
			mat.maps[MATERIAL_MAP_DIFFUSE].texture = whiteTex;
		}

		// Wheels.
		mat.maps[MATERIAL_MAP_DIFFUSE].color = WHITE;
		for (int i = 0; i < 4; ++i) {
			spin_[i] = std::fmod(spin_[i] + v.wheelSpinRate[i] * dt, 2 * PI);
			bool front = i < 2, left = (i % 2) == 0;
			Vector3 mount = { left ? spec_.trackHalf : -spec_.trackHalf, spec_.mountY - v.wheelOffset[i], front ? spec_.frontAxleZ : spec_.rearAxleZ };
			Matrix m = MatrixRotateX(spin_[i]);
			if (front) m = MatrixMultiply(m, MatrixRotateY(-v.steerAngle));
			m = MatrixMultiply(m, MatrixTranslate(mount.x, mount.y, mount.z));
			DrawMesh(wheel_, mat, MatrixMultiply(m, bodyM));
		}

		// Lights: emissive when on; dark when broken.
		bool brakeOn = v.brake > 0.1f;
		world.setEmissive(1.0f);
		Color lit = lampsOn ? Color{ 255, 255, 255, 255 } : Color{ 190, 190, 180, 255 };
		Color broken = { 40, 40, 40, 255 };
		mat.maps[MATERIAL_MAP_DIFFUSE].color = (v.lightsBroken & 1) ? broken : lit;
		DrawMesh(headL_, mat, bodyM);
		mat.maps[MATERIAL_MAP_DIFFUSE].color = (v.lightsBroken & 2) ? broken : lit;
		DrawMesh(headR_, mat, bodyM);
		if (lampsOn) {
			mat.maps[MATERIAL_MAP_DIFFUSE].color = (v.lightsBroken & 4) ? broken : lit;
			DrawMesh(pod_, mat, bodyM);
		}
		mat.maps[MATERIAL_MAP_DIFFUSE].color = brakeOn ? Color{ 255, 255, 255, 255 } : (lampsOn ? Color{ 200, 120, 120, 255 } : Color{ 120, 60, 60, 255 });
		DrawMesh(tail_, mat, bodyM);
		world.setEmissive(0.0f);

		// Interior + crew.
		mat.maps[MATERIAL_MAP_DIFFUSE].color = WHITE;
		DrawMesh(interior_, mat, bodyM);
		DrawMesh(crew_, mat, bodyM);
		{
			Vector3 hub = { 0.38f, driverEye_.y - 0.3f, driverEye_.z + 0.55f };
			// +steer = right = clockwise for the driver; turns as far as the real wheel (1:1).
			float wheelAngle = v.steerInput * settings().wheelRotation * 0.5f * DEG2RAD;
			Matrix m = MatrixMultiply(MatrixRotateZ(wheelAngle), MatrixRotateX(-0.35f));
			m = MatrixMultiply(m, MatrixTranslate(hub.x, hub.y, hub.z));
			DrawMesh(steering_, mat, MatrixMultiply(m, bodyM));
		}

		// Glass last (translucent). Skipped from inside so the view is clear.
		if (view == CarView::Exterior) {
			mat.maps[MATERIAL_MAP_DIFFUSE].color = WHITE;
			DrawMesh(glass_, mat, bodyM);
		}
	}
}
