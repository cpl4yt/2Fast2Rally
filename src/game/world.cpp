#include "game/world.h"
#include "game/meshBuilder.h"
#include "core/noise.h"
#include "core/ui.h"
#include "core/log.h"
#include <raymath.h>
#include <rlgl.h>
#include <cmath>
#include <algorithm>

namespace rally {
	namespace {
		const char* kVS = R"(#version 330
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
in vec4 vertexColor;
uniform mat4 mvp;
uniform mat4 matModel;
uniform mat4 matNormal;
out vec2 fragTexCoord;
out vec4 fragColor;
out vec3 fragPosition;
out vec3 fragNormal;
void main() {
    fragTexCoord = vertexTexCoord;
    fragColor = vertexColor;
    fragPosition = vec3(matModel * vec4(vertexPosition, 1.0));
    fragNormal = normalize(vec3(matNormal * vec4(vertexNormal, 0.0)));
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
)";

		const char* kFS = R"(#version 330
in vec2 fragTexCoord;
in vec4 fragColor;
in vec3 fragPosition;
in vec3 fragNormal;
uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform vec3 viewPos;
uniform vec3 sunDir;
uniform vec3 sunColor;
uniform vec3 ambient;
uniform vec3 fogColor;
uniform float fogDensity;
uniform vec3 lampPos;
uniform vec3 lampDir;
uniform float lampIntensity;
uniform float wetness;
uniform float emissive;
uniform float alphaCut;
out vec4 finalColor;
void main() {
    vec4 tex = texture(texture0, fragTexCoord);
    vec4 base = tex * fragColor * colDiffuse;
    if (base.a < alphaCut) discard;
    vec3 n = normalize(fragNormal);
    if (!gl_FrontFacing) n = -n;
    vec3 V = normalize(viewPos - fragPosition);
    float ndl = max(dot(n, sunDir), 0.0);
    float hemi = 0.55 + 0.45 * n.y;
    vec3 light = ambient * hemi + sunColor * ndl;
    vec3 toL = lampPos - fragPosition;
    float dist = length(toL);
    vec3 L = toL / max(dist, 0.001);
    float spot = smoothstep(0.80, 0.94, dot(-L, lampDir));
    float lamp = lampIntensity * spot * max(dot(n, L), 0.0) * 40.0 / (8.0 + dist * dist * 0.06);
    light += vec3(1.0, 0.95, 0.82) * lamp;
    vec3 col = base.rgb * light;
    float up = clamp(n.y, 0.0, 1.0);
    if (wetness > 0.0) {
        col *= 1.0 - 0.28 * wetness * up;
        vec3 R = reflect(-sunDir, n);
        float sp = pow(max(dot(R, V), 0.0), 20.0);
        col += sunColor * sp * 0.3 * wetness * up;
    }
    col = mix(col, base.rgb, emissive);
    float d = length(viewPos - fragPosition);
    float f = 1.0 - exp(-pow(d * fogDensity, 1.5));
    col = mix(col, fogColor, clamp(f, 0.0, 1.0));
    finalColor = vec4(col, base.a);
}
)";

		Vector3 colorVec(Color c, float k = 1.0f) { return { c.r / 255.0f * k, c.g / 255.0f * k, c.b / 255.0f * k }; }

		Texture2D makeNoiseTexture(int size, Color a, Color b, float scale, uint32_t seed, float grain) {
			Image img = GenImageColor(size, size, a);
			Color* px = (Color*)img.data;
			Noise n(seed);
			Rng rng(seed);
			for (int y = 0; y < size; ++y)
				for (int x = 0; x < size; ++x) {
					// Tileable: sample noise on a torus-ish domain via wrapped coords.
					float fx = (float)x / size, fy = (float)y / size;
					float v = 0.5f + 0.5f * n.fbm(std::cos(fx * 2 * PI) * scale + std::sin(fy * 2 * PI) * 0.37f * scale,
						std::sin(fx * 2 * PI) * scale + std::cos(fy * 2 * PI) * scale, 4);
					v = std::clamp(v + (rng.uniform() - 0.5f) * grain, 0.0f, 1.0f);
					px[y * size + x] = { (unsigned char)(a.r + (b.r - a.r) * v), (unsigned char)(a.g + (b.g - a.g) * v),
										 (unsigned char)(a.b + (b.b - a.b) * v), 255 };
				}
			Texture2D t = LoadTextureFromImage(img);
			UnloadImage(img);
			GenTextureMipmaps(&t);
			SetTextureFilter(t, TEXTURE_FILTER_TRILINEAR);
			SetTextureWrap(t, TEXTURE_WRAP_REPEAT);
			return t;
		}

		Texture2D loadTex(const char* path, Color tint = WHITE) {
			Image img = LoadImage(path);
			if (img.data == nullptr) img = GenImageColor(4, 4, tint);
			else if (tint.r != 255 || tint.g != 255 || tint.b != 255) ImageColorTint(&img, tint);
			if (img.width > 512) ImageResize(&img, 512, 512);
			Texture2D t = LoadTextureFromImage(img);
			UnloadImage(img);
			GenTextureMipmaps(&t);
			SetTextureFilter(t, TEXTURE_FILTER_TRILINEAR);
			SetTextureWrap(t, TEXTURE_WRAP_REPEAT);
			return t;
		}

		Material makeMat(Shader sh, Texture2D tex, Color col = WHITE) {
			Material m = LoadMaterialDefault();
			m.shader = sh;
			m.maps[MATERIAL_MAP_DIFFUSE].texture = tex;
			m.maps[MATERIAL_MAP_DIFFUSE].color = col;
			return m;
		}

		struct PropFile { const char* path; float height; };
		const PropFile kPropFiles[] = {
			{ "res/models/pine1.glb", 13.0f }, { "res/models/pine2.glb", 11.0f },
			{ "res/models/birch1.glb", 11.0f }, { "res/models/birch2.glb", 10.0f },
			{ "res/models/oak1.glb", 9.5f }, { "res/models/oak2.glb", 8.5f },
			{ "res/models/deadTree2.glb", 8.0f },
			{ "res/models/rock1.glb", 1.3f }, { "res/models/rock2.glb", 1.2f },
			{ "res/models/rock3.glb", 1.1f }, { "res/models/rock4.glb", 1.4f },
			{ "res/models/stump.glb", 0.6f }, { "res/models/log.glb", 0.5f },
			{ "res/models/grassTall.glb", 0.8f },
		};
	}

	Environment makeEnvironment(const std::string& envName, Weather w, TimeOfDay t) {
		Environment e{};
		bool snow = envName == "snow";
		if (t == TimeOfDay::Day) {
			e.skyTop = { 70, 130, 210, 255 };
			e.skyHorizon = { 185, 210, 235, 255 };
			e.sunDir = Vector3Normalize({ 0.45f, 0.75f, 0.35f });
			e.sunColor = { 1.0f, 0.95f, 0.85f };
			e.ambient = { 0.48f, 0.52f, 0.62f };
			e.fogDensity = 0.0016f;
			e.lampIntensity = 0.0f;
		}
		else {
			e.skyTop = { 38, 46, 92, 255 };
			e.skyHorizon = { 235, 135, 80, 255 };
			e.sunDir = Vector3Normalize({ 0.85f, 0.14f, 0.2f });
			e.sunColor = { 1.0f, 0.55f, 0.32f };
			e.ambient = { 0.22f, 0.22f, 0.32f };
			e.fogDensity = 0.0019f;
			e.lampIntensity = 1.0f;
		}
		if (w == Weather::Overcast || w == Weather::Rain) {
			float dark = w == Weather::Rain ? 0.75f : 0.9f;
			auto gray = [&](Color c, float k) {
				int g = (int)((c.r + c.g + c.b) / 3.0f);
				return Color{ (unsigned char)((c.r * 0.3f + g * 0.7f) * k), (unsigned char)((c.g * 0.3f + g * 0.7f) * k), (unsigned char)((c.b * 0.3f + g * 0.7f + 6) * k), 255 };
			};
			e.skyTop = gray(e.skyTop, dark);
			e.skyHorizon = gray(e.skyHorizon, dark);
			e.sunColor = Vector3Scale(e.sunColor, w == Weather::Rain ? 0.25f : 0.4f);
			e.ambient = Vector3Scale(e.ambient, 1.25f);
			e.fogDensity *= w == Weather::Rain ? 2.4f : 1.6f;
			if (w == Weather::Rain) {
				e.precipitation = true;
				e.snowfall = snow;
				e.wetness = snow ? 0.0f : 1.0f;
				if (t == TimeOfDay::Day) e.lampIntensity = 0.5f;
			}
		}
		if (snow) e.ambient = Vector3Scale(e.ambient, 0.95f);
		e.fogColor = e.skyHorizon;
		return e;
	}

	bool World::init() {
		shader_ = LoadShaderFromMemory(kVS, kFS);
		if (shader_.id == 0) { log::error("world: shader failed"); return false; }
		locViewPos = GetShaderLocation(shader_, "viewPos");
		locSunDir = GetShaderLocation(shader_, "sunDir");
		locSunColor = GetShaderLocation(shader_, "sunColor");
		locAmbient = GetShaderLocation(shader_, "ambient");
		locFogColor = GetShaderLocation(shader_, "fogColor");
		locFogDensity = GetShaderLocation(shader_, "fogDensity");
		locLampPos = GetShaderLocation(shader_, "lampPos");
		locLampDir = GetShaderLocation(shader_, "lampDir");
		locLampIntensity = GetShaderLocation(shader_, "lampIntensity");
		locWetness = GetShaderLocation(shader_, "wetness");
		locEmissive = GetShaderLocation(shader_, "emissive");
		locAlphaCut = GetShaderLocation(shader_, "alphaCut");
		setEmissive(0); setAlphaCut(0.01f);

		Image wimg = GenImageColor(4, 4, WHITE);
		white_ = LoadTextureFromImage(wimg);
		UnloadImage(wimg);
		Image pimg = GenImageGradientRadial(64, 64, 0.0f, Color{ 255, 255, 255, 255 }, Color{ 255, 255, 255, 0 });
		particle_ = LoadTextureFromImage(pimg);
		UnloadImage(pimg);
		SetTextureFilter(particle_, TEXTURE_FILTER_BILINEAR);

		grass_ = loadTex("res/textures/grass.png");
		gravel_ = loadTex("res/textures/gravel.png", Color{ 255, 228, 195, 255 });
		dirt_ = loadTex("res/textures/dirt.png");
		mud_ = loadTex("res/textures/dirt.png", Color{ 150, 120, 95, 255 });
		snow_ = makeNoiseTexture(256, Color{ 225, 232, 245, 255 }, Color{ 255, 255, 255, 255 }, 3.0f, 11, 0.12f);
		snowRoad_ = makeNoiseTexture(256, Color{ 196, 202, 214, 255 }, Color{ 238, 242, 250, 255 }, 5.0f, 12, 0.25f);
		tarmac_ = makeNoiseTexture(256, Color{ 48, 48, 52, 255 }, Color{ 88, 88, 92, 255 }, 6.0f, 13, 0.6f);

		terrainMat_ = makeMat(shader_, grass_);
		Texture2D roadTex[(int)Surface::Count] = { tarmac_, gravel_, snowRoad_, mud_, grass_, dirt_ };
		for (int i = 0; i < (int)Surface::Count; ++i) roadMat_[i] = makeMat(shader_, roadTex[i]);
		shoulderMat_ = makeMat(shader_, gravel_);
		plainMat_ = makeMat(shader_, white_);
		colorMat_ = makeMat(shader_, white_);

		// Prop models (from Prairie), normalised to sensible real-world sizes.
		for (const PropFile& pf : kPropFiles) {
			Model m = LoadModel(pf.path);
			float s = 1.0f, yo = 0.0f;
			if (m.meshCount > 0) {
				BoundingBox bb = GetModelBoundingBox(m);
				float h = bb.max.y - bb.min.y;
				if (h > 0.001f) s = pf.height / h;
				yo = -bb.min.y * s;
				for (int i = 0; i < m.materialCount; ++i) m.materials[i].shader = shader_;
			}
			else log::warn("world: could not load %s", pf.path);
			propModels_.push_back(m);
			propScale_.push_back(s);
			propYOffset_.push_back(yo);
		}
		// Marker post: red/white bands.
		{
			MeshBuilder mb;
			for (int i = 0; i < 4; ++i)
				mb.cylinder({ 0, i * 0.3f, 0 }, 0.07f, 0.07f, 0.3f, 8, (i % 2) ? Color{ 245, 245, 245, 255 } : Color{ 210, 30, 30, 255 });
			Model m = LoadModelFromMesh(mb.upload());
			m.materials[0].shader = shader_;
			propModels_.push_back(m); propScale_.push_back(1.0f); propYOffset_.push_back(0.0f);
		}
		// Spectators: tinted body + untinted head.
		{
			MeshBuilder body;
			Color legs = { 55, 58, 70, 255 };
			body.box({ -0.12f, 0.45f, 0 }, { 0.09f, 0.45f, 0.1f }, legs);
			body.box({ 0.12f, 0.45f, 0 }, { 0.09f, 0.45f, 0.1f }, legs);
			body.box({ 0, 1.2f, 0 }, { 0.26f, 0.33f, 0.15f }, WHITE);
			body.box({ -0.33f, 1.18f, 0 }, { 0.07f, 0.3f, 0.08f }, WHITE);
			body.box({ 0.33f, 1.18f, 0 }, { 0.07f, 0.3f, 0.08f }, WHITE);
			spectatorBody_ = LoadModelFromMesh(body.upload());
			spectatorBody_.materials[0].shader = shader_;
			Model head = LoadModelFromMesh(GenMeshSphere(0.13f, 8, 8));
			head.materials[0].shader = shader_;
			head.materials[0].maps[MATERIAL_MAP_DIFFUSE].color = { 225, 185, 160, 255 };
			spectatorHead_ = head;
			propModels_.push_back(spectatorBody_); propScale_.push_back(1.0f); propYOffset_.push_back(0.0f);
		}
		ready_ = true;
		return true;
	}

	void World::shutdown() {
		if (!ready_) return;
		setStage(nullptr);
		for (size_t i = 0; i < propModels_.size(); ++i) UnloadModel(propModels_[i]);
		propModels_.clear();
		UnloadModel(spectatorHead_);
		for (Texture2D t : { white_, grass_, gravel_, dirt_, snow_, tarmac_, snowRoad_, mud_, particle_ }) UnloadTexture(t);
		UnloadShader(shader_);
		ready_ = false;
	}

	Model& World::propModel(PropKind k) {
		int i = (int)k;
		if (i >= (int)propModels_.size()) i = 0;
		return propModels_[i];
	}

	void World::setEmissive(float e) { SetShaderValue(shader_, locEmissive, &e, SHADER_UNIFORM_FLOAT); }
	void World::setAlphaCut(float a) { SetShaderValue(shader_, locAlphaCut, &a, SHADER_UNIFORM_FLOAT); }
	void World::setFogDensity(float d) { SetShaderValue(shader_, locFogDensity, &d, SHADER_UNIFORM_FLOAT); }

	void World::setStage(const Stage* stage) {
		for (auto& rt : bannerRT_) UnloadRenderTexture(rt);
		bannerRT_.clear();
		if (gantryMesh_.vertexCount) UnloadMesh(gantryMesh_);
		if (bannerQuad_.vertexCount) UnloadMesh(bannerQuad_);
		if (hillsMesh_.vertexCount) UnloadMesh(hillsMesh_);
		gantryMesh_ = bannerQuad_ = hillsMesh_ = Mesh{};
		stage_ = stage;
		if (!stage) return;

		env_ = makeEnvironment(stage->def.environment, stage->weather, stage->timeOfDay);
		terrainMat_.maps[MATERIAL_MAP_DIFFUSE].texture = stage->def.environment == "snow" ? snow_ : grass_;
		shoulderMat_.maps[MATERIAL_MAP_DIFFUSE].texture = stage->def.environment == "snow" ? snowRoad_ : gravel_;

		// Banner textures (text rendered once).
		for (const Banner& b : stage->banners) {
			RenderTexture2D rt = LoadRenderTexture(512, 128);
			BeginTextureMode(rt);
			ClearBackground(b.color);
			DrawRectangle(0, 0, 512, 10, WHITE);
			DrawRectangle(0, 118, 512, 10, WHITE);
			Vector2 sz = ui::measure(b.text, 84, true);
			DrawTextEx(ui::titleFont(), b.text.c_str(), { 256 - sz.x * 0.5f, 64 - sz.y * 0.5f }, 84, 1.5f, WHITE);
			EndTextureMode();
			SetTextureFilter(rt.texture, TEXTURE_FILTER_BILINEAR);
			bannerRT_.push_back(rt);
		}
		MeshBuilder q;
		// Unit quad facing -z/+z, v flipped for render textures.
		q.quad({ -0.5f, -0.5f, 0 }, { 0.5f, -0.5f, 0 }, { 0.5f, 0.5f, 0 }, { -0.5f, 0.5f, 0 }, WHITE, { 1, 0 }, { 0, 0 }, { 0, 1 }, { 1, 1 });
		bannerQuad_ = q.upload();

		MeshBuilder g;
		Color post = { 70, 70, 78, 255 };
		for (const Banner& b : stage->banners) {
			Matrix m = MatrixMultiply(MatrixRotateY(b.yaw), MatrixTranslate(b.pos.x, b.pos.y, b.pos.z));
			auto P = [&](Vector3 l) { return Vector3Transform(l, m); };
			if (b.gantry) {
				float hw = b.width * 0.5f;
				for (float sx : { -hw, hw }) {
					Vector3 c = P({ sx, 2.8f, 0 });
					g.box(c, { 0.18f, 2.8f, 0.18f }, post, b.yaw);
				}
				g.box(P({ 0, 5.75f, 0 }), { hw + 0.2f, 0.18f, 0.2f }, post, b.yaw);
			}
			else {
				float off = stage->halfWidth() + 2.2f;
				g.box(P({ -off, 1.2f, 0 }), { 0.07f, 1.2f, 0.07f }, post, b.yaw);
			}
		}
		gantryMesh_ = g.upload();

		// Far hills ring, hazy silhouettes beyond the playable terrain.
		{
			float minX = 1e9f, minZ = 1e9f, maxX = -1e9f, maxZ = -1e9f;
			for (auto& s : stage->samples) {
				minX = std::min(minX, s.pos.x); maxX = std::max(maxX, s.pos.x);
				minZ = std::min(minZ, s.pos.z); maxZ = std::max(maxZ, s.pos.z);
			}
			Vector2 c = { (minX + maxX) * 0.5f, (minZ + maxZ) * 0.5f };
			float R = std::max(maxX - minX, maxZ - minZ) * 0.5f + 1100.0f;
			float baseY = stage->samples.empty() ? 0 : stage->samples[0].pos.y - 40.0f;
			const std::string& e = stage->def.environment;
			Color col = e == "snow" ? Color{ 215, 222, 236, 255 } : e == "mountain" ? Color{ 105, 115, 120, 255 } : Color{ 62, 88, 66, 255 };
			Noise n(stage->def.seed + 5);
			MeshBuilder h;
			int segs = 160;
			for (int i = 0; i < segs; ++i) {
				float a0 = 2 * PI * i / segs, a1 = 2 * PI * (i + 1) / segs;
				float amp = e == "mountain" ? 420.0f : 170.0f;
				float h0 = 40 + amp * (0.5f + 0.5f * n.fbm(std::cos(a0) * 3, std::sin(a0) * 3, 4));
				float h1 = 40 + amp * (0.5f + 0.5f * n.fbm(std::cos(a1) * 3, std::sin(a1) * 3, 4));
				Vector3 b0 = { c.x + std::cos(a0) * R, baseY, c.y + std::sin(a0) * R };
				Vector3 b1 = { c.x + std::cos(a1) * R, baseY, c.y + std::sin(a1) * R };
				Vector3 t0 = { b0.x, baseY + h0, b0.z }, t1 = { b1.x, baseY + h1, b1.z };
				// Face inward (toward the stage).
				h.quad(b0, b1, t1, t0, col);
			}
			hillsMesh_ = h.upload();
		}
	}

	void World::beginFrame(const Camera3D& cam, Vector3 lampPos, Vector3 lampDir, bool lampsOn, float lampScale) {
		SetShaderValue(shader_, locViewPos, &cam.position, SHADER_UNIFORM_VEC3);
		SetShaderValue(shader_, locSunDir, &env_.sunDir, SHADER_UNIFORM_VEC3);
		SetShaderValue(shader_, locSunColor, &env_.sunColor, SHADER_UNIFORM_VEC3);
		SetShaderValue(shader_, locAmbient, &env_.ambient, SHADER_UNIFORM_VEC3);
		Vector3 fog = colorVec(env_.fogColor);
		SetShaderValue(shader_, locFogColor, &fog, SHADER_UNIFORM_VEC3);
		SetShaderValue(shader_, locFogDensity, &env_.fogDensity, SHADER_UNIFORM_FLOAT);
		SetShaderValue(shader_, locLampPos, &lampPos, SHADER_UNIFORM_VEC3);
		SetShaderValue(shader_, locLampDir, &lampDir, SHADER_UNIFORM_VEC3);
		float li = lampsOn ? env_.lampIntensity * lampScale : 0.0f;
		SetShaderValue(shader_, locLampIntensity, &li, SHADER_UNIFORM_FLOAT);
		SetShaderValue(shader_, locWetness, &env_.wetness, SHADER_UNIFORM_FLOAT);
		setEmissive(0.0f);
		setAlphaCut(0.01f);
	}

	void World::drawSky(const Camera3D& cam) const {
		int w = GetScreenWidth(), h = GetScreenHeight();
		Vector3 f = Vector3Subtract(cam.target, cam.position);
		f.y = 0;
		f = Vector3Normalize(f);
		Vector2 hz = GetWorldToScreen(Vector3Add(cam.position, Vector3Scale(f, 2000.0f)), cam);
		int hy = std::clamp((int)hz.y, 0, h);
		DrawRectangleGradientV(0, 0, w, std::max(1, hy), env_.skyTop, env_.skyHorizon);
		DrawRectangle(0, hy, w, h - hy, env_.skyHorizon);
		// Sun glow.
		Vector3 sunW = Vector3Add(cam.position, Vector3Scale(env_.sunDir, 2000.0f));
		Vector3 toSun = Vector3Subtract(sunW, cam.position);
		Vector3 fwd = Vector3Normalize(Vector3Subtract(cam.target, cam.position));
		if (Vector3DotProduct(toSun, fwd) > 0) {
			Vector2 sp = GetWorldToScreen(sunW, cam);
			float k = env_.sunColor.x;
			Color glow = { 255, (unsigned char)(220 * std::min(1.0f, env_.sunColor.y / std::max(0.01f, k))), 170, (unsigned char)(120 * std::min(1.0f, k)) };
			DrawCircleGradient(sp, h * 0.18f, glow, Color{ 255, 255, 255, 0 });
			DrawCircleV(sp, h * 0.022f, Color{ 255, 250, 230, (unsigned char)(255 * std::min(1.0f, k * 1.2f)) });
		}
	}

	void World::drawStage(const Camera3D& cam, float drawDistance) {
		if (!stage_ || !stage_->meshesBuilt) return;
		Vector3 fwd = Vector3Normalize(Vector3Subtract(cam.target, cam.position));
		Matrix I = MatrixIdentity();

		// Far hills: much thinner fog so they read as distant silhouettes.
		if (hillsMesh_.vertexCount) {
			setFogDensity(env_.fogDensity * 0.18f);
			setEmissive(0.55f);
			rlDisableBackfaceCulling();
			DrawMesh(hillsMesh_, plainMat_, I);
			rlEnableBackfaceCulling();
			setEmissive(0.0f);
			setFogDensity(env_.fogDensity);
		}

		for (const TerrainChunk& c : stage_->chunks) {
			Vector3 center = Vector3Scale(Vector3Add(c.bounds.min, c.bounds.max), 0.5f);
			float radius = Vector3Distance(c.bounds.min, c.bounds.max) * 0.5f;
			Vector3 to = Vector3Subtract(center, cam.position);
			float dist = Vector3Length(to);
			if (dist - radius > drawDistance) continue;
			if (Vector3DotProduct(to, fwd) < -radius) continue;
			DrawMesh(c.mesh, terrainMat_, I);
		}
		for (int i = 0; i < (int)Surface::Count; ++i)
			if (stage_->roadMesh[i].vertexCount) DrawMesh(stage_->roadMesh[i], roadMat_[i], I);
		if (stage_->shoulderMesh.vertexCount) DrawMesh(stage_->shoulderMesh, shoulderMat_, I);
		if (stage_->markingsMesh.vertexCount) DrawMesh(stage_->markingsMesh, plainMat_, I);
		if (stage_->bridgeMesh.vertexCount) DrawMesh(stage_->bridgeMesh, plainMat_, I);
		if (gantryMesh_.vertexCount) DrawMesh(gantryMesh_, plainMat_, I);

		// Banners.
		rlDisableBackfaceCulling();
		// Material copies share the maps array: swap the texture in place
		// and restore it afterwards.
		Material& bm = plainMat_;
		setEmissive(0.35f);
		for (size_t i = 0; i < stage_->banners.size() && i < bannerRT_.size(); ++i) {
			const Banner& b = stage_->banners[i];
			bm.maps[MATERIAL_MAP_DIFFUSE].texture = bannerRT_[i].texture;
			Matrix m;
			if (b.gantry) {
				m = MatrixMultiply(MatrixScale(b.width - 0.6f, 1.3f, 1.0f), MatrixTranslate(0, 4.9f, 0));
			}
			else {
				float off = stage_->halfWidth() + 2.2f;
				m = MatrixMultiply(MatrixScale(2.6f, 0.65f, 1.0f), MatrixTranslate(-off, 2.5f, -0.05f));
			}
			m = MatrixMultiply(m, MatrixMultiply(MatrixRotateY(b.yaw), MatrixTranslate(b.pos.x, b.pos.y, b.pos.z)));
			DrawMesh(bannerQuad_, bm, m);
		}
		bm.maps[MATERIAL_MAP_DIFFUSE].texture = white_;
		setEmissive(0.0f);
		rlEnableBackfaceCulling();

		drawProps(cam, drawDistance);
	}

	void World::drawProps(const Camera3D& cam, float drawDistance) {
		Vector3 fwd = Vector3Normalize(Vector3Subtract(cam.target, cam.position));
		float dd2 = drawDistance * drawDistance;
		float grassD2 = std::min(dd2, 90.0f * 90.0f);
		rlDisableBackfaceCulling();
		setAlphaCut(0.5f);
		double t = GetTime();
		for (const Prop& p : stage_->props) {
			Vector3 to = Vector3Subtract(p.pos, cam.position);
			float d2 = to.x * to.x + to.z * to.z;
			if (d2 > dd2) continue;
			if (p.kind == PropKind::GrassTall && d2 > grassD2) continue;
			if (Vector3DotProduct(to, fwd) < -16.0f) continue;
			int k = (int)p.kind;
			if (p.kind == PropKind::Spectator) {
				float bob = (float)std::fabs(std::sin(t * 3.0 + p.pos.x)) * 0.05f;
				Vector3 pos = { p.pos.x, p.pos.y + bob, p.pos.z };
				DrawModelEx(spectatorBody_, pos, { 0, 1, 0 }, p.yaw * RAD2DEG, { p.scale, p.scale, p.scale }, p.tint);
				DrawModelEx(spectatorHead_, { pos.x, pos.y + 1.68f * p.scale, pos.z }, { 0, 1, 0 }, 0, { p.scale, p.scale, p.scale }, WHITE);
				continue;
			}
			Model& m = propModel(p.kind);
			if (m.meshCount == 0) continue;
			float s = propScale_[k] * p.scale;
			Vector3 pos = { p.pos.x, p.pos.y + propYOffset_[k] * p.scale, p.pos.z };
			DrawModelEx(m, pos, { 0, 1, 0 }, p.yaw * RAD2DEG, { s, s, s }, p.tint);
		}
		setAlphaCut(0.01f);
		rlEnableBackfaceCulling();
	}

	void World::drawWater() {
		if (!stage_ || !stage_->waterMesh.vertexCount) return;
		rlDisableBackfaceCulling();
		DrawMesh(stage_->waterMesh, plainMat_, MatrixIdentity());
		rlEnableBackfaceCulling();
	}
}
