#pragma once
#include <raylib.h>
#include <cstdint>
#include <string>

namespace rally {
	enum class Surface : uint8_t { Tarmac = 0, Gravel, Snow, Mud, Grass, Dirt, Count };

	struct SurfaceProps {
		const char* name;
		float friction;        // peak friction coefficient (dry)
		float rolling;         // extra rolling resistance multiplier
		float slipScale;       // multiplies peak slip angle: loose surfaces are progressive
		float slideGrip;       // multiplies the car's slideGrip (how much grip remains sideways)
		float wetFactor;       // friction multiplier in rain
		bool loose;            // throws particles / gravel audio
		Color dust;
	};

	inline const SurfaceProps& surfaceProps(Surface s) {
		static const SurfaceProps table[] = {
			{ "Tarmac", 1.08f, 1.0f, 0.85f, 0.95f, 0.72f, false, { 120, 120, 120, 255 } },
			{ "Gravel", 0.80f, 1.6f, 1.45f, 1.08f, 0.90f, true,  { 168, 142, 110, 255 } },
			{ "Snow",   0.52f, 2.0f, 1.70f, 1.10f, 1.00f, true,  { 240, 244, 250, 255 } },
			{ "Mud",    0.55f, 3.5f, 1.60f, 1.05f, 0.85f, true,  { 92, 70, 48, 255 } },
			{ "Grass",  0.60f, 3.0f, 1.50f, 1.00f, 0.80f, true,  { 80, 110, 50, 255 } },
			{ "Dirt",   0.66f, 2.5f, 1.50f, 1.05f, 0.80f, true,  { 130, 100, 70, 255 } },
		};
		int i = (int)s;
		if (i < 0 || i >= (int)Surface::Count) i = 0;
		return table[i];
	}

	inline Surface surfaceFromName(const std::string& n) {
		if (n == "tarmac") return Surface::Tarmac;
		if (n == "gravel") return Surface::Gravel;
		if (n == "snow") return Surface::Snow;
		if (n == "mud") return Surface::Mud;
		if (n == "grass") return Surface::Grass;
		if (n == "dirt") return Surface::Dirt;
		return Surface::Gravel;
	}

	inline const char* surfaceKey(Surface s) {
		switch (s) {
		case Surface::Tarmac: return "tarmac";
		case Surface::Gravel: return "gravel";
		case Surface::Snow: return "snow";
		case Surface::Mud: return "mud";
		case Surface::Grass: return "grass";
		case Surface::Dirt: return "dirt";
		default: return "gravel";
		}
	}

	// What the vehicle needs to know about the world under it.
	class Ground {
	public:
		virtual ~Ground() = default;
		virtual float heightAt(float x, float z) const = 0;
		virtual Surface surfaceAt(float x, float z) const = 0;
		virtual float gripMultiplier(Surface s) const = 0;  // weather etc.
	};
}
