#include "game/particles.h"
#include <raymath.h>
#include <rlgl.h>
#include <cmath>
#include <algorithm>

namespace rally {
	void Particles::spawn(const Particle& p) {
		if (parts_.size() > 4000) return;
		parts_.push_back(p);
	}

	void Particles::emitForCar(const CarVisual& v, const CarSpec& spec, float dt, int quality, bool backfire) {
		if (quality <= 0) return;
		float density = quality == 1 ? 0.4f : 1.0f;
		float speed = Vector3Length(v.velocity);
		Matrix bodyM = MatrixMultiply(QuaternionToMatrix(v.rotation), MatrixTranslate(v.position.x, v.position.y, v.position.z));

		for (int i = 0; i < 4; ++i) {
			if (!(v.wheelContact & (1 << i))) continue;
			Surface surf = (Surface)v.wheelSurface[i];
			const SurfaceProps& sp = surfaceProps(surf);
			bool front = i < 2, left = (i % 2) == 0;
			Vector3 local = { left ? spec.trackHalf : -spec.trackHalf, spec.mountY - v.wheelOffset[i] - spec.wheelRadius + 0.1f, front ? spec.frontAxleZ : spec.rearAxleZ };
			Vector3 wp = Vector3Transform(local, bodyM);
			float slip = v.wheelSlip[i];
			float rate = 0.0f;
			if (sp.loose) rate = (speed * 0.9f + slip * 40.0f) * density;   // dust trail even when gripping
			else rate = slip > 0.35f ? (slip - 0.3f) * 30.0f * density : 0.0f; // tarmac: only smoke when sliding
			int n = (int)(rate * dt + frand());
			for (int k = 0; k < n; ++k) {
				Particle p{};
				p.pos = { wp.x + (frand() - 0.5f) * 0.3f, wp.y, wp.z + (frand() - 0.5f) * 0.3f };
				Vector3 back = Vector3Scale(v.velocity, -0.15f - frand() * 0.2f);
				if (sp.loose) {
					p.vel = { back.x + (frand() - 0.5f) * 3.0f, 1.0f + frand() * 2.5f + slip * 2.0f, back.z + (frand() - 0.5f) * 3.0f };
					p.color = sp.dust;
					p.color.a = surf == Surface::Snow ? 200 : 150;
					p.size = 0.6f + frand() * 0.5f;
					p.growth = surf == Surface::Snow ? 2.0f : 3.5f;
					p.maxLife = p.life = 0.9f + frand() * 1.6f;
					p.drag = 1.2f;
					p.gravity = surf == Surface::Snow ? 1.5f : 0.4f;
				}
				else {
					// Tarmac: thin tyre smoke when sliding.
					p.vel = { back.x * 0.3f + (frand() - 0.5f), 0.4f + frand() * 0.6f, back.z * 0.3f + (frand() - 0.5f) };
					p.color = { 200, 200, 205, 55 };
					p.size = 0.3f;
					p.growth = 1.4f;
					p.maxLife = p.life = 0.8f + frand() * 0.6f;
					p.drag = 1.5f;
					p.gravity = -0.2f;
				}
				spawn(p);
			}
			// Flying stones / clods on loose surfaces when spinning.
			if (sp.loose && slip > 0.4f && quality >= 2) {
				int stones = (int)(slip * 30.0f * dt + frand());
				for (int k = 0; k < stones; ++k) {
					Particle p{};
					p.pos = wp;
					Vector3 back = Vector3Scale(v.velocity, -0.3f);
					p.vel = { back.x + (frand() - 0.5f) * 4, 2.0f + frand() * 3, back.z + (frand() - 0.5f) * 4 };
					p.color = surf == Surface::Snow ? Color{ 250, 250, 255, 255 } : Color{ (unsigned char)(sp.dust.r * 0.6f), (unsigned char)(sp.dust.g * 0.6f), (unsigned char)(sp.dust.b * 0.6f), 255 };
					p.size = 0.08f; p.growth = 0;
					p.maxLife = p.life = 0.8f;
					p.drag = 0.2f; p.gravity = 9.8f;
					spawn(p);
				}
			}
		}

		// Exhaust (rear left).
		Vector3 ex = Vector3Transform({ -0.45f, -spec.halfExtents.y + 0.06f, -spec.halfExtents.z - 0.12f }, bodyM);
		Vector3 exDir = Vector3Transform({ 0, 0, -1 }, QuaternionToMatrix(v.rotation));
		if (frand() < (0.1f + v.throttle * 0.5f) * density) {
			Particle p{};
			p.pos = ex;
			p.vel = Vector3Add(Vector3Scale(exDir, 1.0f), Vector3Scale(v.velocity, 0.6f));
			p.color = { 120, 120, 125, 50 };
			p.size = 0.15f; p.growth = 1.2f;
			p.maxLife = p.life = 0.8f;
			p.drag = 2.0f; p.gravity = -0.5f;
			spawn(p);
		}
		if (backfire) {
			for (int k = 0; k < 6; ++k) {
				Particle p{};
				p.pos = ex;
				p.vel = Vector3Add(Vector3Scale(exDir, 4.0f + frand() * 3), Vector3Scale(v.velocity, 0.9f));
				p.color = { 255, (unsigned char)(140 + frand() * 80), 40, 255 };
				p.size = 0.25f + frand() * 0.15f; p.growth = -0.6f;
				p.maxLife = p.life = 0.12f + frand() * 0.08f;
				p.drag = 3.0f; p.gravity = 0;
				p.additive = true;
				spawn(p);
			}
		}
	}

	void Particles::emitImpact(Vector3 pos, Color c, int count) {
		for (int k = 0; k < count; ++k) {
			Particle p{};
			p.pos = pos;
			p.vel = { (frand() - 0.5f) * 6, frand() * 4, (frand() - 0.5f) * 6 };
			p.color = c;
			p.size = 0.4f + frand() * 0.4f; p.growth = 2.0f;
			p.maxLife = p.life = 0.8f + frand();
			p.drag = 1.5f; p.gravity = 0.5f;
			spawn(p);
		}
	}

	void Particles::update(float dt) {
		for (Particle& p : parts_) {
			p.life -= dt;
			p.vel.y -= p.gravity * dt;
			p.vel = Vector3Scale(p.vel, 1.0f - std::min(1.0f, p.drag * dt));
			p.pos = Vector3Add(p.pos, Vector3Scale(p.vel, dt));
			p.size = std::max(0.02f, p.size + p.growth * dt);
		}
		parts_.erase(std::remove_if(parts_.begin(), parts_.end(), [](const Particle& p) { return p.life <= 0; }), parts_.end());
	}

	void Particles::draw(const Camera3D& cam, Texture2D tex, float light) {
		if (parts_.empty()) return;
		rlDrawRenderBatchActive();
		rlDisableDepthMask();
		for (int pass = 0; pass < 2; ++pass) {
			if (pass == 1) BeginBlendMode(BLEND_ADDITIVE);
			for (const Particle& p : parts_) {
				if (p.additive != (pass == 1)) continue;
				float t = p.life / p.maxLife;
				Color c = p.color;
				c.a = (unsigned char)(c.a * std::min(1.0f, t * 1.5f));
				if (!p.additive) { c.r = (unsigned char)(c.r * light); c.g = (unsigned char)(c.g * light); c.b = (unsigned char)(c.b * light); }
				DrawBillboard(cam, tex, p.pos, p.size, c);
			}
			rlDrawRenderBatchActive();
			if (pass == 1) EndBlendMode();
		}
		rlEnableDepthMask();
	}

	void Particles::drawWeather(const Camera3D& cam, float dt, bool rain, bool snow, Vector3 carVel, float light) {
		if (!rain && !snow) { drops_.clear(); return; }
		const int count = snow ? 900 : 700;
		const float R = snow ? 22.0f : 16.0f;
		if ((int)drops_.size() != count) {
			drops_.resize(count);
			for (auto& d : drops_) d.p = { cam.position.x + (frand() - 0.5f) * 2 * R, cam.position.y + (frand() - 0.3f) * R, cam.position.z + (frand() - 0.5f) * 2 * R };
		}
		Vector3 fall = snow ? Vector3{ 0.4f, -1.6f, 0.2f } : Vector3{ 0.5f, -14.0f, 0.3f };
		accum_ += dt;
		rlDrawRenderBatchActive();
		rlDisableDepthMask();
		for (auto& d : drops_) {
			Vector3 v = fall;
			if (snow) {
				v.x += std::sin(accum_ * 1.3f + d.p.y) * 0.5f;
				v.z += std::cos(accum_ * 1.1f + d.p.x) * 0.5f;
			}
			d.p = Vector3Add(d.p, Vector3Scale(v, dt));
			// Wrap around the camera box.
			Vector3 rel = Vector3Subtract(d.p, cam.position);
			if (rel.x > R) d.p.x -= 2 * R; else if (rel.x < -R) d.p.x += 2 * R;
			if (rel.z > R) d.p.z -= 2 * R; else if (rel.z < -R) d.p.z += 2 * R;
			if (rel.y < -R * 0.4f) d.p.y += R * 1.2f; else if (rel.y > R * 0.8f) d.p.y -= R * 1.2f;
			if (snow) {
				DrawCube(d.p, 0.05f, 0.05f, 0.05f, Color{ (unsigned char)(255 * light), (unsigned char)(255 * light), (unsigned char)(255 * light), 220 });
			}
			else {
				Vector3 streak = Vector3Subtract(fall, Vector3Scale(carVel, 0.6f));
				Vector3 tail = Vector3Subtract(d.p, Vector3Scale(streak, 0.035f));
				DrawLine3D(d.p, tail, Color{ (unsigned char)(200 * light), (unsigned char)(210 * light), (unsigned char)(225 * light), 110 });
			}
		}
		rlDrawRenderBatchActive();
		rlEnableDepthMask();
	}
}
