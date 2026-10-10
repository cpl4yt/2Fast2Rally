#include "core/noise.h"
#include <cmath>

namespace rally {
	Noise::Noise(uint32_t seed) {
		Rng rng(seed * 2654435761u + 1);
		for (int i = 0; i < 256; ++i) perm[i] = (uint8_t)i;
		for (int i = 255; i > 0; --i) {
			int j = (int)(rng.next() % (uint32_t)(i + 1));
			uint8_t t = perm[i]; perm[i] = perm[j]; perm[j] = t;
		}
		for (int i = 0; i < 256; ++i) perm[256 + i] = perm[i];
	}

	static float fade(float t) { return t * t * t * (t * (t * 6 - 15) + 10); }
	static float lerpf(float a, float b, float t) { return a + (b - a) * t; }
	static float grad(int h, float x, float y) {
		switch (h & 7) {
		case 0: return x + y;  case 1: return -x + y;
		case 2: return x - y;  case 3: return -x - y;
		case 4: return x;      case 5: return -x;
		case 6: return y;      default: return -y;
		}
	}

	float Noise::perlin(float x, float y) const {
		int xi = (int)std::floor(x), yi = (int)std::floor(y);
		float xf = x - xi, yf = y - yi;
		xi &= 255; yi &= 255;
		float u = fade(xf), v = fade(yf);
		int aa = perm[perm[xi] + yi], ab = perm[perm[xi] + yi + 1];
		int ba = perm[perm[xi + 1] + yi], bb = perm[perm[xi + 1] + yi + 1];
		float x1 = lerpf(grad(aa, xf, yf), grad(ba, xf - 1, yf), u);
		float x2 = lerpf(grad(ab, xf, yf - 1), grad(bb, xf - 1, yf - 1), u);
		return lerpf(x1, x2, v) * 0.7071f;
	}

	float Noise::fbm(float x, float y, int octaves, float lacunarity, float gain) const {
		float sum = 0, amp = 1, norm = 0, f = 1;
		for (int i = 0; i < octaves; ++i) {
			sum += perlin(x * f, y * f) * amp;
			norm += amp;
			amp *= gain;
			f *= lacunarity;
		}
		return sum / norm;
	}

	float Noise::ridged(float x, float y, int octaves) const {
		float sum = 0, amp = 0.5f, f = 1, norm = 0;
		for (int i = 0; i < octaves; ++i) {
			float n = 1.0f - std::fabs(perlin(x * f, y * f));
			sum += n * n * amp;
			norm += amp;
			amp *= 0.5f;
			f *= 2.0f;
		}
		return sum / norm;
	}
}
