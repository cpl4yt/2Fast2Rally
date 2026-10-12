#pragma once
#include <raylib.h>
#include <functional>
#include <vector>
#include "game/vehicle.h"

namespace rally {
	// Tyre marks left on the ground: black skid marks on tarmac when sliding,
	// ruts in gravel, snow, mud and grass. A ring buffer of quads, so the
	// oldest marks (far behind the car) are recycled.
	class TyreMarks {
	public:
		void clear();
		// groundY(x, z): height of the surface the marks sit on.
		void update(const CarVisual& v, const CarSpec& spec, const std::function<float(float, float)>& groundY);
		// light: 0..1 scene brightness; maxDist: fade-out distance from the camera.
		void draw(const Camera3D& cam, float light, float maxDist = 160.0f) const;

	private:
		struct Quad { Vector3 p[4]; Color c; unsigned char a0, a1; };
		std::vector<Quad> quads_;
		size_t head_ = 0;
		struct Track { bool active = false; Vector3 pos{}, l{}, r{}; unsigned char alpha = 0; };
		Track tracks_[4];
	};
}
