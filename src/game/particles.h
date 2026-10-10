#pragma once
#include <raylib.h>
#include <vector>
#include "game/surface.h"
#include "game/vehicle.h"

namespace rally {
	struct Particle {
		Vector3 pos, vel;
		float life, maxLife;
		float size, growth;
		Color color;
		float drag, gravity;
		bool additive;
	};

	class Particles {
	public:
		void clear() { parts_.clear(); }
		// Spawns dust/spray/smoke/exhaust/flames for a car. quality: 0 off, 1 low, 2 high.
		void emitForCar(const CarVisual& v, const CarSpec& spec, float dt, int quality, bool backfire);
		void emitImpact(Vector3 pos, Color c, int count);
		void update(float dt);
		void draw(const Camera3D& cam, Texture2D tex, float light = 1.0f);

		// Weather (camera-relative volume).
		void drawWeather(const Camera3D& cam, float dt, bool rain, bool snow, Vector3 carVel, float light = 1.0f);

	private:
		void spawn(const Particle& p);
		std::vector<Particle> parts_;
		struct Drop { Vector3 p; };
		std::vector<Drop> drops_;
		unsigned rng_ = 12345;
		float frand() { rng_ = rng_ * 1664525u + 1013904223u; return (rng_ >> 8) / float(1 << 24); }
		float accum_ = 0;
	};
}
