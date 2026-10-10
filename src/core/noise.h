#pragma once
#include <cstdint>

namespace rally {
	// Seeded value/gradient noise for terrain and stage generation.
	class Noise {
	public:
		explicit Noise(uint32_t seed = 1);
		// Smooth 2D gradient noise in roughly [-1, 1].
		float perlin(float x, float y) const;
		// Fractal sum of `octaves` perlin layers, result roughly [-1, 1].
		float fbm(float x, float y, int octaves, float lacunarity = 2.0f, float gain = 0.5f) const;
		// 1 - |perlin|, sharpened: good for ridged mountains.
		float ridged(float x, float y, int octaves) const;
	private:
		uint8_t perm[512];
	};

	// Small deterministic RNG (xorshift) so stage props are identical on both
	// players' machines given the same seed.
	struct Rng {
		uint32_t s;
		explicit Rng(uint32_t seed) : s(seed ? seed : 0x9E3779B9u) {}
		uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
		float uniform() { return (next() & 0xFFFFFF) / float(0x1000000); }        // [0,1)
		float range(float a, float b) { return a + (b - a) * uniform(); }
		int irange(int a, int b) { return a + int(next() % uint32_t(b - a + 1)); } // [a,b]
	};
}
