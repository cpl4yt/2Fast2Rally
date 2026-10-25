#pragma once
#include <raylib.h>
#include "game/carSpec.h"
#include "game/vehicle.h"

namespace rally {
	class World;

	enum class CarView { Exterior, Interior };

	// Procedurally modelled low-poly rally car (no asset files): lofted body,
	// glass, wheels, lights (which break), interior, and the two crew.
	class CarRenderer {
	public:
		void build(const CarSpec& spec);
		void unload();
		// dt advances wheel spin; lampsOn lights the headlights/pod.
		void draw(World& world, const CarVisual& v, float dt, bool lampsOn, CarView view);
		// Instrument cluster on the dashboard behind the steering wheel, showing
		// tex (a render texture of the gauges). Cockpit view only.
		void drawCluster(World& world, const CarVisual& v, Texture2D tex);

		// Local-space camera anchors.
		Vector3 driverEye() const { return driverEye_; }
		Vector3 codriverEye() const { return codriverEye_; }
		Vector3 bonnetEye() const { return bonnetEye_; }
		Vector3 headlightLocal() const { return { 0.0f, 0.05f, spec_.halfExtents.z + 0.1f }; }
		const CarSpec& spec() const { return spec_; }

	private:
		CarSpec spec_;
		// Optional imported body (res/models/cars): colours come from one palette texture.
		Model modelBody_{}, modelLower_{}, modelWheel_{};
		Texture2D atlas_{};
		bool modelMode_ = false;
		Matrix modelM_{};              // model space -> car space
		float modelWheelScale_ = 1.0f;
		float numberX_ = 0, numberY_ = 0, numberZ_ = 0;
		Mesh body_{}, cabin_{}, glass_{}, wheel_{}, interior_{}, crew_{}, steering_{}, headL_{}, headR_{}, pod_{}, tail_{}, numberQuad_{};
		RenderTexture2D numberRT_{};
		float spin_[4]{};
		Vector3 driverEye_{}, codriverEye_{}, bonnetEye_{};
		Vector3 hubLocal_{}, clusterLocal_{};
		Mesh clusterQuad_{};
		bool built_ = false;
	};
}
