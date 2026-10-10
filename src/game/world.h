#pragma once
#include <raylib.h>
#include <vector>
#include <string>
#include "game/stage.h"

namespace rally {
	// Lighting/fog/sky for a weather + time-of-day + environment combination.
	struct Environment {
		Color skyTop, skyHorizon;
		Vector3 sunDir;
		Vector3 sunColor;
		Vector3 ambient;
		Color fogColor;
		float fogDensity;
		float wetness;
		float lampIntensity;   // car lights
		bool precipitation;    // rain or snow falling
		bool snowfall;
	};
	Environment makeEnvironment(const std::string& env, Weather w, TimeOfDay t);

	// Owns everything needed to draw the world: the lit shader, textures,
	// prop models, and the per-stage banner textures.
	class World {
	public:
		bool init();
		void shutdown();

		// Builds per-stage render data (banners, far hills).
		void setStage(const Stage* stage);
		const Environment& env() const { return env_; }
		void setEnvironment(const Environment& e) { env_ = e; }

		// Uniforms that change per frame.
		void beginFrame(const Camera3D& cam, Vector3 lampPos, Vector3 lampDir, bool lampsOn, float lampScale = 1.0f);
		void drawSky(const Camera3D& cam) const;           // 2D pass, before BeginMode3D
		void drawStage(const Camera3D& cam, float drawDistance);
		void drawWater();

		// Generic lit material helpers for other renderers (car, gantries).
		Shader shader() const { return shader_; }
		Material& colorMaterial() { return colorMat_; }
		void setEmissive(float e);
		void setAlphaCut(float a);
		void setFogDensity(float d);

		Texture2D whiteTexture() const { return white_; }
		Texture2D particleTexture() const { return particle_; }

	private:
		void drawProps(const Camera3D& cam, float drawDistance);
		Model& propModel(PropKind k);

		Shader shader_{};
		int locViewPos = -1, locSunDir = -1, locSunColor = -1, locAmbient = -1, locFogColor = -1, locFogDensity = -1;
		int locLampPos = -1, locLampDir = -1, locLampIntensity = -1, locWetness = -1, locEmissive = -1, locAlphaCut = -1;

		Texture2D white_{}, grass_{}, gravel_{}, dirt_{}, snow_{}, tarmac_{}, snowRoad_{}, mud_{}, particle_{};
		Material terrainMat_{}, roadMat_[(int)Surface::Count]{}, shoulderMat_{}, plainMat_{}, colorMat_{};

		std::vector<Model> propModels_;
		std::vector<float> propScale_;     // normalisation so models are a sensible size
		std::vector<float> propYOffset_;

		const Stage* stage_ = nullptr;
		Environment env_{};
		std::vector<RenderTexture2D> bannerRT_;
		Mesh bannerQuad_{};
		Mesh gantryMesh_{};
		Model spectatorBody_{}, spectatorHead_{};
		Mesh hillsMesh_{};
		bool hillsBuilt_ = false;
		bool ready_ = false;
	};
}
