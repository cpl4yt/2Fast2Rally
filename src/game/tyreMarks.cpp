#include "game/tyreMarks.h"
#include <raymath.h>
#include <rlgl.h>
#include <algorithm>
#include <cmath>

namespace rally {
	namespace {
		constexpr size_t kMaxQuads = 6000;
		constexpr float kSegment = 0.4f;   // metres between mark points

		// Mark colour and opacity for a tyre on surface s with slip 0..1.
		void markStyle(Surface s, float slip, Color& c, float& alpha) {
			switch (s) {
			case Surface::Tarmac: c = { 18, 18, 20, 255 }; alpha = std::clamp((slip - 0.3f) * 1.4f, 0.0f, 0.8f); break;
			case Surface::Gravel: c = { 70, 58, 44, 255 }; alpha = 0.22f + slip * 0.45f; break;
			case Surface::Dirt:   c = { 66, 50, 36, 255 }; alpha = 0.25f + slip * 0.45f; break;
			case Surface::Mud:    c = { 38, 30, 22, 255 }; alpha = 0.4f + slip * 0.4f; break;
			case Surface::Snow:   c = { 150, 162, 182, 255 }; alpha = 0.35f + slip * 0.35f; break;
			case Surface::Grass:  c = { 52, 62, 30, 255 }; alpha = 0.18f + slip * 0.6f; break;
			default: c = BLACK; alpha = 0.0f; break;
			}
			alpha = std::clamp(alpha, 0.0f, 0.85f);
		}
	}

	void TyreMarks::clear() {
		quads_.clear();
		head_ = 0;
		for (Track& t : tracks_) t.active = false;
	}

	void TyreMarks::update(const CarVisual& v, const CarSpec& spec, const std::function<float(float, float)>& groundY) {
		Matrix rot = QuaternionToMatrix(v.rotation);
		Vector3 right = Vector3Transform({ 1, 0, 0 }, rot);
		right.y = 0;
		right = Vector3Normalize(right);
		const float half = spec.wheelWidth * 0.45f;
		for (int i = 0; i < 4; ++i) {
			Track& t = tracks_[i];
			bool front = i < 2, left = (i % 2) == 0;
			Vector3 local = { left ? spec.trackHalf : -spec.trackHalf, spec.mountY - v.wheelOffset[i] - spec.wheelRadius, front ? spec.frontAxleZ : spec.rearAxleZ };
			Vector3 wp = Vector3Add(v.position, Vector3Transform(local, rot));
			Color c; float alpha = 0;
			bool contact = (v.wheelContact & (1 << i)) != 0;
			if (contact) markStyle((Surface)v.wheelSurface[i], v.wheelSlip[i], c, alpha);
			if (!contact || alpha < 0.03f) { t.active = false; continue; }
			wp.y = groundY(wp.x, wp.z) + 0.025f;
			Vector3 l = Vector3Add(wp, Vector3Scale(right, half)), r = Vector3Subtract(wp, Vector3Scale(right, half));
			l.y = groundY(l.x, l.z) + 0.025f;
			r.y = groundY(r.x, r.z) + 0.025f;
			unsigned char a = (unsigned char)(alpha * 255.0f);
			if (!t.active) { t = { true, wp, l, r, a }; continue; }
			float d = Vector3Distance(wp, t.pos);
			if (d > 3.0f) { t = { true, wp, l, r, a }; continue; }   // reset / teleport
			if (d < kSegment) continue;
			Quad q{ { t.l, l, r, t.r }, c, t.alpha, a };
			if (quads_.size() < kMaxQuads) quads_.push_back(q);
			else { quads_[head_] = q; head_ = (head_ + 1) % kMaxQuads; }
			t.pos = wp; t.l = l; t.r = r; t.alpha = a;
		}
	}

	void TyreMarks::draw(const Camera3D& cam, float light, float maxDist) const {
		if (quads_.empty()) return;
		rlDrawRenderBatchActive();
		rlDisableDepthMask();
		rlDisableBackfaceCulling();
		rlBegin(RL_QUADS);
		float max2 = maxDist * maxDist;
		for (const Quad& q : quads_) {
			Vector3 mid = Vector3Scale(Vector3Add(q.p[0], q.p[2]), 0.5f);
			float d2 = Vector3DistanceSqr(mid, cam.position);
			if (d2 > max2) continue;
			float fade = 1.0f - std::sqrt(d2) / maxDist;
			unsigned char r = (unsigned char)(q.c.r * light), g = (unsigned char)(q.c.g * light), b = (unsigned char)(q.c.b * light);
			unsigned char a0 = (unsigned char)(q.a0 * fade), a1 = (unsigned char)(q.a1 * fade);
			rlColor4ub(r, g, b, a0); rlVertex3f(q.p[0].x, q.p[0].y, q.p[0].z);
			rlColor4ub(r, g, b, a1); rlVertex3f(q.p[1].x, q.p[1].y, q.p[1].z);
			rlColor4ub(r, g, b, a1); rlVertex3f(q.p[2].x, q.p[2].y, q.p[2].z);
			rlColor4ub(r, g, b, a0); rlVertex3f(q.p[3].x, q.p[3].y, q.p[3].z);
		}
		rlEnd();
		rlDrawRenderBatchActive();
		rlEnableBackfaceCulling();
		rlEnableDepthMask();
	}
}
