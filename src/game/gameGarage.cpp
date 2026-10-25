// Career garage: the car on the workshop floor, every part listed with what
// you know about its condition, and the tools to fix it - unbolt, remove,
// buy new or used, refit, bolt it back up, top up the fluids, then go rally.
#include "game/game.h"
#include "core/ui.h"
#include "core/log.h"
#include <raymath.h>
#include <rlgl.h>
#include <algorithm>
#include <cmath>

namespace rally {
	namespace {
		const char* kCareerPath = "saves/career.json";
		const char* kModelFiles[] = { "debris-tire", "debris-door", "debris-bumper", "debris-drivetrain", "debris-drivetrain-axle",
			"debris-plate-a", "debris-plate-small-a", "debris-spoiler-a", "box", "cone", "wheel-default" };
		enum GM { GTire, GDoor, GBumper, GDrivetrain, GAxle, GPlate, GPlateSmall, GSpoiler, GBox, GCone, GWheel, GCount };
		const float kModelSize[GCount] = { 0.66f, 1.1f, 1.6f, 0.7f, 0.9f, 1.0f, 0.35f, 0.9f, 0.6f, 0.5f, 0.6f };

		Model gModels[GCount];
		float gScale[GCount];
		Vector3 gOffset[GCount];
		bool gLoaded = false;
		Texture2D gFloor{}, gWall{}, gWood{};
		Mesh gTile{};

		// Kenney model for a part lying on the floor.
		GM modelFor(const PartDef& d) {
			if (d.cat == PartCat::Wheels) return GTire;
			if (d.id.rfind("door", 0) == 0) return GDoor;
			if (d.id.find("bumper") != std::string::npos) return GBumper;
			if (d.id == "bonnet" || d.id == "windscreen") return GPlate;
			if (d.cat == PartCat::Brakes || d.id.rfind("headlight", 0) == 0) return GPlateSmall;
			if (d.cat == PartCat::Suspension || d.id == "driveshafts" || d.id == "exhaust" || d.id == "steering_rack") return GAxle;
			if (d.cat == PartCat::Drivetrain || d.id == "engine_block" || d.id == "cylinder_head" || d.id == "turbo") return GDrivetrain;
			return GBox;
		}

		std::string money(float m) {
			char b[32];
			std::snprintf(b, sizeof b, "$%.0f", m);
			return b;
		}

		Color condColor(float c) {
			return c < 0.25f ? ui::kBad : c < 0.55f ? ui::kAccent : ui::kGood;
		}
	}

	int Game::carIndexById(const std::string& id) const {
		for (int i = 0; i < (int)cars_.size(); ++i) if (cars_[i].id == id) return i;
		return 0;
	}

	void Game::garageEnsureAssets() {
		if (gLoaded) return;
		gLoaded = true;
		for (int i = 0; i < GCount; ++i) {
			std::string path = std::string("res/models/garage/") + kModelFiles[i] + ".glb";
			gModels[i] = LoadModel(path.c_str());
			gScale[i] = 1.0f; gOffset[i] = {};
			if (gModels[i].meshCount == 0) { log::warn("garage: could not load %s", path.c_str()); continue; }
			BoundingBox bb = GetModelBoundingBox(gModels[i]);
			Vector3 sz = Vector3Subtract(bb.max, bb.min);
			float big = std::max(sz.x, std::max(sz.y, sz.z));
			gScale[i] = big > 1e-4f ? kModelSize[i] / big : 1.0f;
			gOffset[i] = { -(bb.min.x + bb.max.x) * 0.5f * gScale[i], -bb.min.y * gScale[i], -(bb.min.z + bb.max.z) * 0.5f * gScale[i] };
			for (int m = 0; m < gModels[i].materialCount; ++m) gModels[i].materials[m].shader = world_.shader();
		}
		auto tex = [](const char* p) {
			Texture2D t = LoadTexture(p);
			if (t.id) { GenTextureMipmaps(&t); SetTextureFilter(t, TEXTURE_FILTER_TRILINEAR); }
			return t;
		};
		gFloor = tex("res/textures/garage_floor.png");
		gWall = tex("res/textures/garage_wall.png");
		gWood = tex("res/textures/garage_wood.png");
		gTile = GenMeshPlane(2.0f, 2.0f, 1, 1);
	}

	void Game::garageUnload() {
		if (!gLoaded) return;
		for (Model& m : gModels) if (m.meshCount) UnloadModel(m);
		for (Texture2D* t : { &gFloor, &gWall, &gWood }) if (t->id) UnloadTexture(*t);
		UnloadMesh(gTile);
		garageRender_.unload();
		gLoaded = false;
	}

	void Game::openCareer() {
		careerMode_ = false;
		if (career_.cars.empty()) career_.load(kCareerPath, cars_);
		garageSel_ = -1;
		garageTab_ = career_.cars.empty() ? 3 : 0;
		openScreen(Screen::Garage);
	}

	void Game::careerSave() { career_.save(kCareerPath); }

	void Game::garageMessage(const std::string& m, bool good) {
		garageMsg_ = m;
		garageMsgGood_ = good;
		garageMsgTimer_ = 3.5f;
	}

	// ------------------------------------------------------------------
	// 3D workshop
	// ------------------------------------------------------------------
	void Game::drawGarage3D(float dt) {
		garageEnsureAssets();
		CarCondition* cc = career_.car();
		const CarSpec* spec = cc ? &cars_[carIndexById(cc->carId)] : &cars_[0];
		if (garageBuiltFor_ != spec->id) { garageRender_.build(*spec); garageBuiltFor_ = spec->id; }

		Environment env = makeEnvironment("forest", Weather::Clear, TimeOfDay::Day);
		env.fogDensity = 0.0f;
		env.ambient = { 0.55f, 0.53f, 0.5f };
		env.sunColor = { 0.75f, 0.72f, 0.65f };
		env.sunDir = Vector3Normalize({ 0.3f, -1.0f, 0.4f });
		env.lampIntensity = 0.0f;
		world_.setEnvironment(env);

		// Orbit camera; right-drag or arrow-free mouse wheel to look around.
		if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
			Vector2 d = GetMouseDelta();
			garageYaw_ -= d.x * 0.006f;
			garagePitch_ = std::clamp(garagePitch_ + d.y * 0.004f, 0.08f, 1.1f);
		}
		else garageYaw_ += dt * 0.04f;
		garageDist_ = std::clamp(garageDist_ - GetMouseWheelMove() * 0.4f, 2.5f, 9.0f);

		Vector3 target = { 0, 0.6f, 0 };
		float dist = garageDist_;
		const auto& defs = partDefs();
		Vector3 marker{};
		bool showMarker = false;
		if (garageSel_ >= 0 && garageSel_ < (int)defs.size()) {
			Vector3 w = defs[garageSel_].where;
			marker = { w.x * spec->halfExtents.x / 0.88f, w.y + 0.45f, w.z * spec->halfExtents.z / 2.08f };
			target = Vector3Lerp(target, marker, 0.6f);
			dist = std::min(dist, 4.2f);
			showMarker = true;
		}
		garageTarget_ = Vector3Lerp(garageTarget_, target, std::min(1.0f, dt * 4.0f));
		garageCamDist_ += (dist - garageCamDist_) * std::min(1.0f, dt * 4.0f);

		Camera3D cam{};
		cam.target = garageTarget_;
		cam.position = Vector3Add(cam.target, { std::sin(garageYaw_) * std::cos(garagePitch_) * garageCamDist_, std::sin(garagePitch_) * garageCamDist_, std::cos(garageYaw_) * std::cos(garagePitch_) * garageCamDist_ });
		cam.position.y = std::max(cam.position.y, 0.3f);
		cam.up = { 0, 1, 0 };
		cam.fovy = 45;
		cam.projection = CAMERA_PERSPECTIVE;
		// Keep the car right of the UI panel.
		Vector3 fwd = Vector3Normalize(Vector3Subtract(cam.target, cam.position));
		Vector3 right = Vector3Normalize(Vector3CrossProduct(fwd, { 0, 1, 0 }));
		cam.position = Vector3Subtract(cam.position, Vector3Scale(right, 1.4f));
		cam.target = Vector3Subtract(cam.target, Vector3Scale(right, 1.4f));

		world_.beginFrame(cam, { 0, 2.6f, 0 }, { 0, -1, 0 }, false, 0.0f);
		BeginMode3D(cam);
		Material& mat = world_.colorMaterial();
		Texture2D white = world_.whiteTexture();
		mat.maps[MATERIAL_MAP_DIFFUSE].color = WHITE;
		// Floor 14 x 12, walls 3 m high on three sides.
		mat.maps[MATERIAL_MAP_DIFFUSE].texture = gFloor.id ? gFloor : white;
		for (int x = -3; x <= 3; ++x)
			for (int z = -3; z <= 2; ++z) DrawMesh(gTile, mat, MatrixTranslate(x * 2.0f, 0.0f, z * 2.0f));
		mat.maps[MATERIAL_MAP_DIFFUSE].texture = gWall.id ? gWall : white;
		mat.maps[MATERIAL_MAP_DIFFUSE].color = Color{ 200, 200, 200, 255 };
		for (int i = -3; i <= 3; ++i)
			for (int y = 0; y < 2; ++y) {
				// Back wall (-z), facing +z.
				DrawMesh(gTile, mat, MatrixMultiply(MatrixRotateX(PI * 0.5f), MatrixTranslate(i * 2.0f, y * 2.0f + 1.0f, -7.0f)));
				if (std::abs(i) <= 2) {
					DrawMesh(gTile, mat, MatrixMultiply(MatrixMultiply(MatrixRotateX(PI * 0.5f), MatrixRotateY(PI * 0.5f)), MatrixTranslate(-7.0f, y * 2.0f + 1.0f, i * 2.0f - 1.0f)));
					DrawMesh(gTile, mat, MatrixMultiply(MatrixMultiply(MatrixRotateX(PI * 0.5f), MatrixRotateY(-PI * 0.5f)), MatrixTranslate(7.0f, y * 2.0f + 1.0f, i * 2.0f - 1.0f)));
				}
			}
		// Workbench along the back wall.
		mat.maps[MATERIAL_MAP_DIFFUSE].texture = gWood.id ? gWood : white;
		mat.maps[MATERIAL_MAP_DIFFUSE].color = WHITE;
		{
			static Mesh bench{};
			if (bench.vertexCount == 0) bench = GenMeshCube(4.0f, 0.08f, 1.0f);
			DrawMesh(bench, mat, MatrixTranslate(-2.0f, 0.95f, -6.4f));
			static Mesh leg{};
			if (leg.vertexCount == 0) leg = GenMeshCube(0.08f, 0.95f, 0.08f);
			for (float lx : { -3.9f, -0.1f }) for (float lz : { -6.0f, -6.8f }) DrawMesh(leg, mat, MatrixTranslate(lx, 0.475f, lz));
		}
		mat.maps[MATERIAL_MAP_DIFFUSE].texture = white;

		auto place = [&](GM g, Vector3 at, float yaw) {
			if (!gModels[g].meshCount) return;
			Matrix m = MatrixMultiply(MatrixScale(gScale[g], gScale[g], gScale[g]), MatrixRotateY(yaw));
			Vector3 o = Vector3Transform(gOffset[g], MatrixRotateY(yaw));
			m = MatrixMultiply(m, MatrixTranslate(at.x + o.x, at.y + o.y, at.z + o.z));
			gModels[g].transform = m;
			DrawModel(gModels[g], { 0, 0, 0 }, 1.0f, WHITE);
		};
		// Clutter: boxes and cones.
		place(GBox, { -5.6f, 0, -5.8f }, 0.3f);
		place(GBox, { -5.0f, 0, -6.1f }, 1.2f);
		place(GBox, { -5.4f, 0.6f, -5.9f }, 0.7f);
		place(GCone, { 5.5f, 0, 4.0f }, 0);
		place(GCone, { 6.0f, 0, 3.2f }, 0);

		// Spare / removed parts laid out on the floor and the bench.
		if (cc) {
			int slot = 0;
			for (size_t i = 0; i < defs.size(); ++i) {
				if (!cc->parts[i].spare) continue;
				GM g = modelFor(defs[i]);
				Vector3 at;
				if (g == GPlateSmall || g == GDrivetrain || g == GAxle || g == GBox) at = { -3.6f + (slot % 6) * 0.65f, 0.99f, -6.4f + (slot / 6 % 2) * 0.3f };
				else at = { 4.2f + (slot % 3) * 0.95f, 0.0f, -4.5f + (slot / 3) * 1.4f };
				place(g, at, 0.4f * slot);
				if ((int)i == garageSel_) DrawCubeWires(Vector3Add(at, { 0, 0.3f, 0 }), 0.9f, 0.6f, 0.9f, ui::kAccent);
				++slot;
			}
		}

		// The car.
		CarVisual vis{};
		vis.position = { 0, spec->wheelRadius + spec->restLength - spec->mountY - 0.06f, 0 };
		vis.rotation = QuaternionIdentity();
		for (int i = 0; i < 4; ++i) vis.wheelOffset[i] = spec->restLength - 0.06f;
		if (cc) {
			for (int w = 0; w < 4; ++w) {
				const char* tg[4] = { "wheel_fl", "wheel_fr", "wheel_rl", "wheel_rr" };
				if (!cc->part(tg[w]).installed) vis.wheelMissing |= (uint8_t)(1 << w);
			}
			if (cc->health("headlight_l") < 0.3f) vis.lightsBroken |= 1;
			if (cc->health("headlight_r") < 0.3f) vis.lightsBroken |= 2;
			float body = (cc->health("front_bumper") + cc->health("rear_bumper") + cc->health("bonnet") + cc->health("door_l") + cc->health("door_r")) / 5.0f;
			vis.damage = 1.0f - body;
			vis.dirt = 0.1f;
		}
		garageRender_.draw(world_, vis, 0.0f, false, CarView::Exterior);
		// Jack stands under a car with a wheel off.
		if (vis.wheelMissing) {
			for (int w = 0; w < 4; ++w) if (vis.wheelMissing & (1 << w)) {
				Vector3 at = { (w % 2 == 0) ? spec->trackHalf : -spec->trackHalf, 0.0f, w < 2 ? spec->frontAxleZ : spec->rearAxleZ };
				DrawCylinder(at, 0.06f, 0.12f, vis.position.y + spec->mountY, 6, Color{ 180, 40, 30, 255 });
			}
		}
		if (showMarker) {
			float pulse = 0.08f + 0.03f * std::sin((float)GetTime() * 6.0f);
			rlDisableDepthTest();
			DrawSphereWires(marker, pulse * 2.2f, 6, 8, ui::kAccent);
			DrawSphere(marker, pulse, ui::kAccent);
			rlEnableDepthTest();
		}
		EndMode3D();
		int W = GetScreenWidth(), H = GetScreenHeight();
		DrawRectangleGradientH(0, 0, (int)(W * 0.5f), H, Color{ 10, 11, 14, 230 }, Color{ 10, 11, 14, 0 });
	}

	// ------------------------------------------------------------------
	// Garage UI
	// ------------------------------------------------------------------
	void Game::screenGarage(float dt) {
		ClearBackground(Color{ 18, 18, 20, 255 });
		drawGarage3D(dt);
		float s = ui::s();
		int W = GetScreenWidth(), H = GetScreenHeight();
		float x = 40 * s, y = 26 * s, w = 640 * s, h = 40 * s, g = 6 * s;
		Settings& cfg = settings();
		auto sound = [&](const char* n) { sfx_.play(n, cfg.effectsVolume, 0.04f); };

		// --- New career: pick a first car ---
		if (career_.cars.empty()) {
			ui::text("CAREER", x, y, 64 * s, ui::kAccent, ui::Left, true);
			y += 80 * s;
			ui::text("Pick your first car. It's used - check it over before you go rallying.", x, y, 24 * s, ui::kText);
			y += 44 * s;
			std::vector<int> order;
			for (int i = 0; i < (int)cars_.size(); ++i) order.push_back(i);
			std::sort(order.begin(), order.end(), [&](int a, int b) { return Career::carPrice(cars_[a]) < Career::carPrice(cars_[b]); });
			for (int k = 0; k < 3 && k < (int)order.size(); ++k) {
				const CarSpec& cs = cars_[order[k]];
				if (ui::button({ x, y, w, 52 * s }, cs.name + "  -  " + (cs.description.empty() ? cs.carClass : cs.description).substr(0, 40))) {
					career_.startNew(cars_, order[k]);
					careerSave();
					garageTab_ = 0;
					sound("coins");
				}
				y += 60 * s;
			}
			y += 20 * s;
			if (ui::button({ x, y, 260 * s, 52 * s }, "Back") || ui::back()) openScreen(Screen::MainMenu);
			return;
		}

		CarCondition& cc = *career_.car();
		const CarSpec& spec = cars_[carIndexById(cc.carId)];
		const auto& defs = partDefs();

		// Header.
		ui::text("GARAGE", x, y, 54 * s, ui::kAccent, ui::Left, true);
		char hb[160];
		std::snprintf(hb, sizeof hb, "%s   |   %.0f km   |   %d stages", spec.name.c_str(), cc.odometer, cc.stages);
		ui::text(hb, x, y + 62 * s, 22 * s, ui::kTextDim);
		ui::text(money(career_.money), W - 40 * s, y, 48 * s, ui::kGood, ui::Right, true);
		y += 100 * s;

		const char* tabs[] = { "Parts", "Fluids", "Rally", "Cars" };
		float tw = (w - 3 * g) / 4;
		for (int i = 0; i < 4; ++i) {
			Rectangle r = { x + i * (tw + g), y, tw, h };
			if (ui::button(r, tabs[i])) { garageTab_ = i; garageSel_ = -1; }
			if (garageTab_ == i) DrawRectangle((int)r.x, (int)(r.y + r.height - 4 * s), (int)r.width, (int)(4 * s), ui::kAccent);
		}
		y += h + 14 * s;

		// Bolt work happens one bolt at a time, like a real ratchet.
		if (garageBolt_ != 0 && garageSel_ >= 0) {
			garageBoltTimer_ -= dt;
			if (garageBoltTimer_ <= 0.0f) {
				PartState& p = cc.parts[garageSel_];
				int total = defs[garageSel_].bolts;
				if (garageBolt_ > 0 && p.installed && p.boltsTight < total) { ++p.boltsTight; sound("drill"); }
				else if (garageBolt_ < 0 && p.installed && p.boltsTight > 0) { --p.boltsTight; sound("drill"); }
				bool done = !p.installed || (garageBolt_ > 0 ? p.boltsTight >= total : p.boltsTight <= 0);
				if (done || !garageBoltAll_) garageBolt_ = 0;
				garageBoltTimer_ = 0.45f;
			}
		}

		if (garageTab_ == 0) {
			// --- Parts ---
			std::vector<std::string> cats;
			for (int c = 0; c < (int)PartCat::Count; ++c) cats.push_back(partCatName((PartCat)c));
			if (ui::choice({ x, y, w, h }, "Category", garageCat_, cats)) garageSel_ = -1;
			y += h + g * 2;
			for (size_t i = 0; i < defs.size(); ++i) {
				const PartDef& d = defs[i];
				if ((int)d.cat != garageCat_ || (d.turboOnly && !spec.turbo)) continue;
				const PartState& p = cc.parts[i];
				std::string label = d.name;
				Rectangle r = { x, y, w, h };
				if (ui::button(r, "")) { garageSel_ = (int)i; garageBolt_ = 0; }
				if (garageSel_ == (int)i) DrawRectangleLinesEx(r, 2.0f, ui::kAccent);
				ui::text(label, r.x + 14 * s, r.y + 9 * s, 21 * s, p.installed ? ui::kText : ui::kTextDim);
				std::string st;
				Color sc = ui::kTextDim;
				bool looseBolts = false;
				if (!p.installed) { st = p.spare ? "OFF (on bench)" : "MISSING"; sc = p.spare ? ui::kAccent : ui::kBad; }
				else {
					if (d.bolts > 0 && p.boltsTight < d.bolts) { st = std::to_string(p.boltsTight) + "/" + std::to_string(d.bolts) + " bolts  "; sc = ui::kBad; looseBolts = true; }
					if (p.seen >= 0) {
						char b[32]; std::snprintf(b, sizeof b, "%d%%", (int)std::round(p.seen * 100));
						st += b;
						if (!looseBolts) sc = condColor(p.seen);
						if (career_.stageCounter > p.seenAt) st += "*";
					}
					else st += "?";
				}
				ui::text(st, r.x + r.width - 14 * s, r.y + 9 * s, 21 * s, sc, ui::Right);
				y += h + g;
			}
			ui::text("? = not inspected   * = inspected before the last stage", x, y + 4 * s, 18 * s, ui::kTextDim);

			// Detail panel.
			if (garageSel_ >= 0) {
				const PartDef& d = defs[garageSel_];
				PartState& p = cc.parts[garageSel_];
				Rectangle pr = { W - 640 * s, H - 440 * s, 600 * s, 400 * s };
				ui::panel(pr, ui::kBg);
				float py = pr.y + 16 * s, px = pr.x + 20 * s, pw = pr.width - 40 * s;
				ui::text(d.name, px, py, 30 * s, ui::kAccent, ui::Left, true); py += 44 * s;
				// Word-wrap the description.
				{
					std::string line, word;
					std::string txt = d.info + " ";
					for (char ch : txt) {
						if (ch == ' ') {
							std::string tryLine = line.empty() ? word : line + " " + word;
							if (ui::measure(tryLine, 19 * s).x > pw) { ui::text(line, px, py, 19 * s, ui::kTextDim); py += 24 * s; line = word; }
							else line = tryLine;
							word.clear();
						}
						else word += ch;
					}
					if (!line.empty()) { ui::text(line, px, py, 19 * s, ui::kTextDim); py += 24 * s; }
				}
				py += 6 * s;
				char b[128];
				if (p.installed) {
					if (p.seen >= 0) std::snprintf(b, sizeof b, "Condition: %d%%%s", (int)std::round(p.seen * 100), career_.stageCounter > p.seenAt ? " (checked before the last stage)" : "");
					else std::snprintf(b, sizeof b, "Condition: unknown - inspect it");
					ui::text(b, px, py, 21 * s, p.seen >= 0 ? condColor(p.seen) : ui::kText); py += 28 * s;
					if (d.bolts > 0) {
						std::snprintf(b, sizeof b, "Bolts: %d of %d tight", p.boltsTight, d.bolts);
						ui::text(b, px, py, 21 * s, p.boltsTight < d.bolts ? ui::kBad : ui::kGood); py += 28 * s;
					}
				}
				else ui::text("Not on the car.", px, py, 21 * s, ui::kBad), py += 28 * s;
				if (p.spare) {
					std::snprintf(b, sizeof b, "On the bench: %d%% condition", (int)std::round(p.spareCondition * 100));
					ui::text(b, px, py, 21 * s, condColor(p.spareCondition)); py += 28 * s;
				}

				// Actions.
				float bw = (pw - 2 * g) / 3, bh = 42 * s;
				float by = pr.y + pr.height - 2 * bh - 26 * s;
				int col = 0;
				auto act = [&](const std::string& label, bool enabled) {
					Rectangle r = { px + (col % 3) * (bw + g), by + (col / 3) * (bh + g), bw, bh };
					++col;
					return ui::button(r, label, enabled);
				};
				std::string why;
				if (act("Inspect", true)) { career_.inspect(cc, garageSel_); sound("clank"); }
				if (p.installed && d.bolts > 0) {
					if (act(p.boltsTight < d.bolts ? "Tighten all" : "Loosen all", true)) {
						garageBolt_ = p.boltsTight < d.bolts ? 1 : -1; garageBoltAll_ = true; garageBoltTimer_ = 0;
					}
				}
				if (p.installed) {
					bool can = career_.canRemove(cc, garageSel_, &why) && p.boltsTight == 0;
					if (act("Remove", can)) { career_.remove(cc, garageSel_); sound("panel"); careerSave(); }
					else if (!can && p.boltsTight > 0 && why.empty()) why = "Loosen the bolts first.";
				}
				else {
					bool can = career_.canFit(cc, garageSel_, &why);
					if (act("Fit", can)) { career_.fit(cc, garageSel_); sound("panel"); careerSave(); if (d.bolts > 0) garageMessage("Fitted - now tighten the bolts!", false); }
				}
				if (act("Buy new " + money(d.price), career_.money >= d.price)) { if (career_.buy(cc, garageSel_, false)) { sound("coins"); careerSave(); garageMessage(d.name + " bought - it's on the bench.", true); } }
				float used = std::round(d.price * 0.45f);
				if (act("Buy used " + money(used), career_.money >= used)) { if (career_.buy(cc, garageSel_, true)) { sound("coins"); careerSave(); garageMessage("Used " + d.name + " on the bench - inspect it.", true); } }
				if (p.spare && act("Scrap spare", true)) { career_.scrapSpare(cc, garageSel_); sound("clank"); careerSave(); }
				if (d.cat == PartCat::Body && p.installed && act("Beat out dents $30", career_.money >= 30 && p.condition < 0.8f)) {
					if (career_.repair(cc, garageSel_)) { sound("hammer"); careerSave(); }
				}
				if (!why.empty()) ui::text(why, px, by - 28 * s, 19 * s, ui::kAccent);
			}
		}
		else if (garageTab_ == 1) {
			// --- Fluids & service ---
			Fluids& f = cc.fluids;
			auto gauge = [&](const char* label, float v, float cap, const char* unit) {
				ui::text(label, x, y, 22 * s, ui::kText);
				Rectangle bar = { x + 230 * s, y + 6 * s, 260 * s, 16 * s };
				DrawRectangleRec(bar, Color{ 50, 54, 62, 255 });
				float t = std::clamp(v / cap, 0.0f, 1.0f);
				DrawRectangleRec({ bar.x, bar.y, bar.width * t, bar.height }, condColor(t));
				char b[48]; std::snprintf(b, sizeof b, "%.1f %s", v, unit);
				ui::text(b, x + w, y, 22 * s, ui::kTextDim, ui::Right);
				y += 34 * s;
			};
			gauge("Oil level", f.oil, Fluids::kOilCap, "L");
			gauge("Oil condition", f.oilQuality * 100, 100, "%");
			gauge("Coolant", f.coolant, Fluids::kCoolantCap, "L");
			gauge("Brake fluid", f.brakeFluid, Fluids::kBrakeCap, "L");
			gauge("Fuel", f.fuel, Fluids::kFuelCap, "L");
			gauge("Battery charge", f.battery * 100, 100, "%");
			float toeDeg = 0;
			for (float t : cc.toe) toeDeg = std::max(toeDeg, std::fabs(t) * RAD2DEG);
			char ab[64]; std::snprintf(ab, sizeof ab, "Wheel alignment: %.1f deg out", toeDeg);
			ui::text(ab, x, y, 22 * s, toeDeg > 0.5f ? ui::kBad : ui::kGood); y += 44 * s;
			const char* names[] = { "Change oil + filter", "Top up oil", "Top up coolant", "Top up brake fluid", "Fill the tank", "Charge battery" };
			for (int k = 0; k < 6; ++k) {
				float price = Career::refillPrice(cc, k);
				bool need = k == 0 || price > 0.0f;
				if (ui::button({ x, y, w, h }, std::string(names[k]) + "   " + money(price), need && career_.money >= price)) {
					if (career_.refill(cc, k)) { sound(k == 4 ? "coins" : "clank"); careerSave(); }
				}
				y += h + g;
			}
			if (ui::button({ x, y, w, h }, "Wheel alignment   " + money(80), career_.money >= 80 && toeDeg > 0.05f)) {
				if (career_.align(cc)) { sound("drill"); careerSave(); }
			}
			y += h + g;
		}
		else if (garageTab_ == 2) {
			// --- Rally ---
			if (career_.event >= 0 && career_.event < (int)eventNames_.size()) {
				const auto& st = eventStages_[career_.event];
				ui::text(eventNames_[career_.event], x, y, 30 * s, ui::kAccent); y += 40 * s;
				char b[128];
				std::snprintf(b, sizeof b, "Stage %d of %d   -   total so far %s", career_.eventStage + 1, (int)st.size(), ui::formatTime(career_.eventTime).c_str());
				ui::text(b, x, y, 22 * s, ui::kText); y += 36 * s;
				if (career_.eventStage < (int)st.size()) ui::text("Next: " + stageDefs_[st[career_.eventStage]].name, x, y, 22 * s, ui::kTextDim);
				y += 40 * s;
				if (ui::button({ x, y, w, 56 * s }, "Start the next stage")) { careerStartStage(); return; }
				y += 64 * s;
				if (ui::button({ x, y, w, h }, "Retire from this rally")) { career_.history.push_back("Retired from " + eventNames_[career_.event]); career_.event = -1; careerSave(); return; }
				y += h + 20 * s;
			}
			else {
				ui::text("Enter a rally (entry fee per stage). Prize money depends on your times.", x, y, 21 * s, ui::kTextDim); y += 36 * s;
				for (int e = 0; e < (int)eventNames_.size(); ++e) {
					float fee = 100.0f * eventStages_[e].size();
					if (ui::button({ x, y, w, h }, eventNames_[e] + "  (" + std::to_string(eventStages_[e].size()) + " stages)   entry " + money(fee), career_.money >= fee)) {
						career_.money -= fee;
						career_.event = e; career_.eventStage = 0; career_.eventTime = 0;
						sound("coins");
						careerSave();
					}
					y += h + g;
				}
				y += 14 * s;
			}
			// Pre-stage checklist.
			auto warn = career_.warnings(cc, spec);
			ui::text(warn.empty() ? "Car looks ready." : "Before you go:", x, y, 22 * s, warn.empty() ? ui::kGood : ui::kBad); y += 30 * s;
			for (size_t k = 0; k < warn.size() && k < 10; ++k) { ui::text("- " + warn[k], x + 10 * s, y, 19 * s, ui::kAccent); y += 24 * s; }
			if (warn.size() > 10) ui::text("...and " + std::to_string(warn.size() - 10) + " more", x + 10 * s, y, 19 * s, ui::kTextDim);
		}
		else {
			// --- Cars ---
			ui::text("Your cars and the classifieds.", x, y, 21 * s, ui::kTextDim); y += 34 * s;
			for (int i = 0; i < (int)cars_.size(); ++i) {
				const CarSpec& cs = cars_[i];
				int owned = -1;
				for (int k = 0; k < (int)career_.cars.size(); ++k) if (career_.cars[k].carId == cs.id) owned = k;
				float price = Career::carPrice(cs);
				std::string label = cs.name + (owned >= 0 ? (owned == career_.current ? "   (driving)" : "   (owned)") : "   " + money(price));
				bool enabled = owned >= 0 ? owned != career_.current : career_.money >= price;
				if (ui::button({ x, y, w, h }, label, enabled) && career_.event < 0) {
					if (owned >= 0) career_.current = owned;
					else {
						career_.money -= price;
						CarCondition c;
						c.resetNew(cs);
						for (auto& p : c.parts) if (p.installed) p.condition = 0.6f + 0.35f * GetRandomValue(0, 100) / 100.0f;
						c.odometer = (float)GetRandomValue(20000, 120000);
						career_.cars.push_back(c);
						career_.current = (int)career_.cars.size() - 1;
						career_.history.push_back("Bought a used " + cs.name);
						sound("coins");
					}
					garageSel_ = -1;
					careerSave();
				}
				y += h + g;
			}
			if (career_.event >= 0) ui::text("Finish or retire from the current rally to change cars.", x, y + 6 * s, 19 * s, ui::kAccent);
		}

		// Recent history.
		{
			float hy = H - 40 * s - std::min<size_t>(4, career_.history.size()) * 24 * s;
			if (garageTab_ != 0 || garageSel_ < 0) {
				for (size_t k = career_.history.size() > 4 ? career_.history.size() - 4 : 0; k < career_.history.size(); ++k) {
					ui::text(career_.history[k], W - 40 * s, hy, 19 * s, ui::kTextDim, ui::Right);
					hy += 24 * s;
				}
			}
		}
		if (garageMsgTimer_ > 0.0f) {
			garageMsgTimer_ -= dt;
			ui::text(garageMsg_, W * 0.62f, 150 * s, 26 * s, garageMsgGood_ ? ui::kGood : ui::kAccent, ui::Center);
		}
		ui::text("Right-drag to look around, wheel to zoom", W * 0.62f, H - 30 * s, 18 * s, ui::kTextDim, ui::Center);
		if (ui::button({ x, H - 70 * s, 220 * s, 50 * s }, "Main Menu") || ui::back()) { careerSave(); openScreen(Screen::MainMenu); }
	}

	void Game::careerStartStage() {
		CarCondition* cc = career_.car();
		if (!cc || career_.event < 0) return;
		const auto& st = eventStages_[career_.event];
		if (career_.eventStage >= (int)st.size()) return;
		careerMode_ = true;
		rally_ = RallyState{};
		careerSave();
		startStage(st[career_.eventStage], carIndexById(cc->carId), Weather::Clear, TimeOfDay::Day, Role::Driver, false);
	}

	void Game::careerStageDone(bool finished) {
		CarCondition* cc = career_.car();
		if (!cc) return;
		cc->stages++;
		career_.stageCounter++;
		cc->engineTemp = 20.0f;
		if (finished) {
			careerReward_ = career_.stageReward(stage_.finishS - stage_.startS, run_.stageTime, run_.penalties);
			career_.money += careerReward_;
			career_.eventTime += run_.finishTime;
			career_.history.push_back(stage_.def.name + ": " + ui::formatTime(run_.finishTime) + "  +" + money(careerReward_));
			if (career_.event >= 0) {
				career_.eventStage++;
				if (career_.eventStage >= (int)eventStages_[career_.event].size()) {
					float bonus = 400.0f * eventStages_[career_.event].size();
					career_.money += bonus;
					careerReward_ += bonus;
					career_.history.push_back(eventNames_[career_.event] + " finished in " + ui::formatTime(career_.eventTime) + " - bonus " + money(bonus));
					career_.event = -1;
				}
			}
		}
		else {
			career_.history.push_back("Retired on " + stage_.def.name + " - the car needs work.");
			career_.event = -1;
		}
		careerSave();
	}
}
