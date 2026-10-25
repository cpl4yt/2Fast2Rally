#include "game/career.h"
#include "core/log.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <fstream>

namespace rally {
	namespace {
		const char* kWheelTag[4] = { "fl", "fr", "rl", "rr" };
		const char* kWheelName[4] = { "front-left", "front-right", "rear-left", "rear-right" };

		std::vector<PartDef> buildDefs() {
			std::vector<PartDef> d;
			auto add = [&](std::string id, std::string name, PartCat cat, int bolts, float price, std::vector<std::string> blockers, Vector3 where, std::string info) {
				PartDef p; p.id = id; p.name = name; p.cat = cat; p.bolts = bolts; p.price = price; p.blockers = blockers; p.where = where; p.info = info;
				d.push_back(p);
				return (int)d.size() - 1;
			};
			// --- Engine bay ---
			add("battery", "Battery", PartCat::Engine, 2, 95, {}, { 0.5f, 0.35f, 1.5f }, "Starts the car. Flat or missing: the engine won't crank after a stall.");
			add("alternator", "Alternator", PartCat::Engine, 3, 160, {}, { -0.4f, 0.3f, 1.55f }, "Charges the battery while the engine runs. A dead one drains the battery.");
			add("spark_plugs", "Spark plugs (set)", PartCat::Engine, 4, 40, {}, { 0.0f, 0.45f, 1.35f }, "Worn plugs misfire and cost power. No plugs, no start.");
			add("air_filter", "Air filter", PartCat::Engine, 2, 25, {}, { 0.3f, 0.5f, 1.25f }, "Clogs up on dusty gravel. A dirty filter chokes the engine; none lets grit wear it out.");
			add("radiator", "Radiator", PartCat::Engine, 4, 190, { "front_bumper" }, { 0.0f, 0.2f, 1.95f }, "Keeps the engine cool. Holed in frontal hits: it leaks coolant and the engine overheats.");
			add("timing_belt", "Timing belt", PartCat::Engine, 4, 85, { "alternator" }, { -0.45f, 0.25f, 1.35f }, "Wears with distance. If it snaps the engine dies on the spot and bends valves.");
			add("cylinder_head", "Cylinder head", PartCat::Engine, 10, 650, { "spark_plugs", "timing_belt" }, { 0.0f, 0.45f, 1.35f }, "Cracks if the engine is cooked. Comes off to reach the head gasket and internals.");
			add("head_gasket", "Head gasket", PartCat::Engine, 0, 45, { "cylinder_head" }, { 0.0f, 0.38f, 1.35f }, "Blows when the engine overheats: burns coolant, ruins the oil, loses power.");
			add("engine_block", "Engine internals", PartCat::Engine, 0, 1400, { "cylinder_head" }, { 0.0f, 0.15f, 1.35f }, "Pistons and bearings. Wear with revs, die without oil or when cooked. Rebuild kit replaces them.");
			PartDef& tb = d[add("turbo", "Turbocharger", PartCat::Engine, 6, 900, { "exhaust" }, { 0.45f, 0.2f, 1.2f }, "Wears with boost. A tired turbo loses most of the punch.")];
			tb.turboOnly = true;
			add("exhaust", "Exhaust", PartCat::Engine, 4, 260, {}, { 0.2f, -0.3f, -1.6f }, "Hangs low: rocks and rear hits knock it about.");
			// --- Drivetrain ---
			add("driveshafts", "Driveshafts", PartCat::Drivetrain, 4, 280, {}, { 0.0f, -0.2f, 0.0f }, "Snap on hard landings with the throttle pinned. Broken: no drive.");
			add("gearbox", "Gearbox", PartCat::Drivetrain, 8, 1100, { "driveshafts" }, { 0.0f, 0.0f, 0.7f }, "Grinding gears chews it up. A worn box jumps out of gear under load.");
			add("clutch", "Clutch kit", PartCat::Drivetrain, 6, 240, { "gearbox" }, { 0.0f, 0.1f, 0.95f }, "Wears when slipped (launches, riding it). A worn clutch slips under power.");
			// --- Per wheel ---
			for (int w = 0; w < 4; ++w) {
				std::string t = kWheelTag[w], nm = kWheelName[w];
				Vector3 at = { (w % 2 == 0) ? 0.78f : -0.78f, -0.12f, w < 2 ? 1.25f : -1.22f };
				int i;
				i = add("wheel_" + t, "Wheel & tyre " + std::string(nm), PartCat::Wheels, 5, 160, {}, at, "Tread wears with sliding; sharp rocks puncture it. Five wheel nuts - leave them loose and it falls off.");
				d[i].wheel = w;
				i = add("pads_" + t, "Brake pads " + nm, PartCat::Brakes, 2, 45, { "wheel_" + t }, at, "Wear with every stop. Worn to the metal: weak brakes that chew the disc.");
				d[i].wheel = w;
				i = add("disc_" + t, "Brake disc " + nm, PartCat::Brakes, 2, 90, { "wheel_" + t, "pads_" + t }, at, "Lasts a long time unless the pads are gone.");
				d[i].wheel = w;
				i = add("strut_" + t, "Strut " + nm, PartCat::Suspension, 3, 210, { "wheel_" + t }, at, "Shock + spring. Big landings and kerbs wear it: the car floats, bounces and leans.");
				d[i].wheel = w;
				i = add("bearing_" + t, "Wheel bearing " + nm, PartCat::Suspension, 1, 70, { "wheel_" + t, "disc_" + t }, at, "A worn bearing makes the wheel wobble.");
				d[i].wheel = w;
			}
			add("steering_rack", "Steering rack", PartCat::Steering, 4, 380, {}, { 0.0f, -0.05f, 1.2f }, "Side hits bend it: the car pulls and the steering gets vague.");
			// --- Body ---
			add("front_bumper", "Front bumper", PartCat::Body, 4, 220, {}, { 0.0f, -0.1f, 2.1f }, "Takes the first hit, protecting the radiator and lights.");
			add("rear_bumper", "Rear bumper", PartCat::Body, 4, 200, {}, { 0.0f, -0.1f, -2.1f }, "Protects the exhaust.");
			add("bonnet", "Bonnet", PartCat::Body, 4, 300, {}, { 0.0f, 0.45f, 1.5f }, "Body panel.");
			add("door_l", "Door left", PartCat::Body, 3, 340, {}, { 0.9f, 0.3f, -0.2f }, "Body panel.");
			add("door_r", "Door right", PartCat::Body, 3, 340, {}, { -0.9f, 0.3f, -0.2f }, "Body panel.");
			add("headlight_l", "Headlight left", PartCat::Body, 2, 120, {}, { 0.55f, 0.1f, 2.05f }, "Smashed headlights make night stages very dark.");
			add("headlight_r", "Headlight right", PartCat::Body, 2, 120, {}, { -0.55f, 0.1f, 2.05f }, "Smashed headlights make night stages very dark.");
			add("windscreen", "Windscreen", PartCat::Body, 0, 260, {}, { 0.0f, 0.7f, 0.6f }, "Cracks from stones and hits.");
			return d;
		}

		float clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }
		unsigned rngState = 0x9e3779b9u;
		float rnd() { rngState = rngState * 1664525u + 1013904223u; return (rngState >> 8) / float(1 << 24); }
	}

	const char* partCatName(PartCat c) {
		switch (c) {
		case PartCat::Engine: return "Engine";
		case PartCat::Drivetrain: return "Drivetrain";
		case PartCat::Brakes: return "Brakes";
		case PartCat::Suspension: return "Suspension";
		case PartCat::Wheels: return "Wheels";
		case PartCat::Steering: return "Steering";
		case PartCat::Body: return "Body";
		default: return "?";
		}
	}

	const std::vector<PartDef>& partDefs() {
		static std::vector<PartDef> defs = buildDefs();
		return defs;
	}

	int partIndex(const std::string& id) {
		const auto& d = partDefs();
		for (int i = 0; i < (int)d.size(); ++i) if (d[i].id == id) return i;
		log::error("career: unknown part %s", id.c_str());
		return 0;
	}

	void CarCondition::resetNew(const CarSpec& spec) {
		carId = spec.id;
		parts.assign(partDefs().size(), PartState{});
		for (size_t i = 0; i < parts.size(); ++i) {
			parts[i].boltsTight = partDefs()[i].bolts;
			if (partDefs()[i].turboOnly && !spec.turbo) { parts[i].installed = false; parts[i].condition = 0; }
		}
		fluids = Fluids{};
		for (float& t : toe) t = 0;
		odometer = 0; stages = 0; engineTemp = 20.0f;
	}

	float CarCondition::health(const std::string& id) const {
		int i = partIndex(id);
		const PartState& p = parts[i];
		if (!p.installed) return 0.0f;
		int b = partDefs()[i].bolts;
		float attach = b > 0 ? 0.55f + 0.45f * (float)p.boltsTight / b : 1.0f;
		return p.condition * attach;
	}

	float Career::carPrice(const CarSpec& spec) {
		float p = 2500.0f + spec.maxTorque() * 14.0f;
		if (spec.drivetrain == Drivetrain::AWD) p += 3500.0f;
		if (spec.turbo) p += 2000.0f;
		return std::round(p / 50.0f) * 50.0f;
	}

	bool Career::owns(const std::string& carId) const {
		for (const auto& c : cars) if (c.carId == carId) return true;
		return false;
	}

	void Career::startNew(const std::vector<CarSpec>& specs, int carIndex) {
		*this = Career{};
		CarCondition c;
		c.resetNew(specs[std::clamp(carIndex, 0, (int)specs.size() - 1)]);
		// A used car: everything a bit tired, so there's work from day one.
		for (size_t i = 0; i < c.parts.size(); ++i) if (c.parts[i].installed) c.parts[i].condition = 0.55f + 0.4f * rnd();
		c.fluids.oilQuality = 0.5f;
		c.fluids.oil = 3.3f;
		c.odometer = 60000.0f + 90000.0f * rnd();
		cars.push_back(c);
		current = 0;
		history.push_back("Bought a used " + c.carId + ". Check it over before the first stage.");
	}

	// ------------------------------------------------------------------
	// Driving: wear, failures, damage
	// ------------------------------------------------------------------
	std::vector<std::string> Career::simulate(CarCondition& c, Vehicle& v, const CarSpec& spec, float dt, bool running, bool rough) {
		std::vector<std::string> msgs;
		VehicleTelemetry& t = v.telemetryMut();
		Fluids& f = c.fluids;
		auto P = [&](const char* id) -> PartState& { return c.part(id); };
		auto wear = [&](const char* id, float amount) {
			PartState& p = P(id);
			if (p.installed) p.condition = clamp01(p.condition - amount);
		};

		float d = t.distance;            // m since last tick
		float km = d / 1000.0f;
		c.odometer += km;
		float rpm = v.rpm();
		float rpmN = clamp01(rpm / spec.redline);
		float thr = v.visual().throttle;
		float speed = std::fabs(v.speed());
		bool engineOn = v.engineRunning() && running;

		// --- Fuel ---
		if (engineOn) f.fuel = std::max(0.0f, f.fuel - (0.00006f * d + 0.0003f * thr * d + 0.0002f * dt * rpmN));
		// --- Lubrication & engine internals ---
		float oilFrac = f.oil / Fluids::kOilCap;
		float lube = clamp01(oilFrac / 0.55f) * (0.35f + 0.65f * f.oilQuality);
		if (engineOn) {
			f.oil = std::max(0.0f, f.oil - km * (0.0008f + (1.0f - c.parts[partIndex("engine_block")].condition) * 0.006f));
			f.oilQuality = clamp01(f.oilQuality - km * (1.0f / 400.0f) * (0.5f + rpmN));
			float block = rpmN * rpmN * 0.000004f * (1.0f + 10.0f * (1.0f - lube)) * dt;
			if (!P("air_filter").installed) block *= 3.0f;
			if (oilFrac < 0.15f) block += 0.01f * dt;            // running dry
			block += t.limiterTime * 0.002f;
			wear("engine_block", block);
			wear("spark_plugs", km / 260.0f * (0.5f + rpmN));
			wear("air_filter", km / 90.0f * (rough ? 2.0f : 0.5f));
			wear("timing_belt", km / 400.0f);
			if (spec.turbo) wear("turbo", km / 450.0f * (0.3f + v.boost()));
		}
		// --- Cooling ---
		{
			float heat = engineOn ? 0.3f + thr * rpmN * 2.0f : 0.0f;
			float rad = c.health("radiator");
			float cool = std::max(0.02f, rad * clamp01(f.coolant / Fluids::kCoolantCap) * (0.6f + std::min(speed / 20.0f, 1.0f) * 0.8f) * 1.6f);
			float target = engineOn ? 80.0f + 25.0f * heat / cool : 20.0f;
			c.engineTemp += (target - c.engineTemp) * std::min(1.0f, dt * 0.08f);
			if (rad < 0.45f && engineOn) f.coolant = std::max(0.0f, f.coolant - (0.45f - rad) * 0.01f * dt);
			if (c.part("head_gasket").installed && c.part("head_gasket").condition < 0.3f && engineOn) {
				f.coolant = std::max(0.0f, f.coolant - 0.002f * dt);
				f.oilQuality = clamp01(f.oilQuality - 0.002f * dt);
			}
			if (c.engineTemp > 112.0f && engineOn) {
				float over = c.engineTemp - 112.0f;
				wear("head_gasket", over * 0.0006f * dt);
				wear("engine_block", over * 0.0001f * dt);
				if (c.engineTemp > 125.0f) wear("cylinder_head", over * 0.0002f * dt);
				static float warnT = 0;
				warnT -= dt;
				if (warnT <= 0) { msgs.push_back("ENGINE OVERHEATING"); warnT = 8.0f; }
			}
			if (c.engineTemp > 140.0f && P("engine_block").condition > 0.0f) {
				P("engine_block").condition = 0.0f;
				msgs.push_back("The engine has seized!");
			}
		}
		// --- Timing belt snap ---
		if (engineOn && P("timing_belt").installed && P("timing_belt").condition <= 0.0f) {
			P("timing_belt").installed = false;
			wear("cylinder_head", 0.5f);
			wear("engine_block", 0.25f);
			msgs.push_back("Timing belt snapped!");
		}
		// --- Electrics ---
		{
			float alt = c.health("alternator");
			if (engineOn && alt > 0.15f) f.battery = clamp01(f.battery + 0.004f * dt * alt);
			else f.battery = clamp01(f.battery - 0.0004f * dt);
			wear("alternator", km / 900.0f);
			wear("battery", km / 2000.0f);
		}
		// --- Clutch & gearbox ---
		wear("clutch", t.clutchWork / 9.0e6f);
		if (t.grinds > 0) wear("gearbox", 0.012f * t.grinds);
		wear("gearbox", km / 1500.0f);
		wear("driveshafts", km / 1200.0f);
		// --- Brakes ---
		for (int w = 0; w < 4; ++w) {
			std::string tg = kWheelTag[w];
			PartState& pads = c.part("pads_" + tg);
			float work = t.brakeWork[w];
			if (pads.installed && pads.condition > 0.02f) {
				pads.condition = clamp01(pads.condition - work / 3.5e7f);
				wear(("disc_" + tg).c_str(), work / 4.0e8f);
			}
			else wear(("disc_" + tg).c_str(), work / 2.5e7f);   // metal on metal
			f.brakeFluid = std::max(0.0f, f.brakeFluid - work / 4.0e9f);
			// Tyres and suspension.
			wear(("wheel_" + tg).c_str(), t.tyreWork[w] / 2.2e7f + km / 300.0f);
			wear(("strut_" + tg).c_str(), t.suspensionWork[w] / 4.0e7f);
			wear(("bearing_" + tg).c_str(), km / 700.0f);
		}

		// --- Impacts: where the car was hit decides what breaks ---
		if (v.impactCount != seenImpacts_) {
			seenImpacts_ = v.impactCount;
			Vector3 L = v.lastImpactLocal;
			float hit = std::sqrt(L.x * L.x + L.z * L.z);
			float sev = std::max(0.0f, hit - 4.0f) * 0.035f;
			auto loosen = [&](const char* id, float chance) {
				PartState& p = P(id);
				int i = partIndex(id);
				if (p.installed && partDefs()[i].bolts > 0 && p.boltsTight > 0 && rnd() < chance) --p.boltsTight;
			};
			if (sev > 0.0f) {
				if (L.z < -2.5f) {        // pushed back: hit something head-on
					PartState& bump = P("front_bumper");
					float through = bump.installed ? std::max(0.0f, sev - bump.condition * 0.12f) : sev;
					wear("front_bumper", sev * 1.5f);
					wear("bonnet", sev * 0.8f);
					wear("radiator", through * 1.6f);
					if (sev > 0.08f) { wear("headlight_l", sev * (L.x > 0 ? 2.0f : 0.8f)); wear("headlight_r", sev * (L.x < 0 ? 2.0f : 0.8f)); }
					if (sev > 0.15f) wear("windscreen", sev * 0.6f);
					loosen("front_bumper", 0.3f + sev);
					loosen("radiator", sev);
				}
				if (L.z > 2.5f) {
					wear("rear_bumper", sev * 1.5f);
					wear("exhaust", sev * 1.2f);
					loosen("exhaust", 0.3f + sev);
				}
				if (std::fabs(L.x) > 3.0f) {
					// dv to -x means the hit came from the car's left (+x).
					bool fromLeft = L.x < 0.0f;
					wear(fromLeft ? "door_l" : "door_r", sev * 1.5f);
					int w0 = fromLeft ? 0 : 1, w1 = fromLeft ? 2 : 3;
					for (int w : { w0, w1 }) {
						std::string tg = kWheelTag[w];
						c.toe[w] += (rnd() - 0.5f) * sev * 0.2f;
						wear(("strut_" + tg).c_str(), sev * 0.8f);
						loosen(("wheel_" + tg).c_str(), sev * 0.8f);
						if (sev > 0.12f && rnd() < sev) {
							PartState& tyre = P(("wheel_" + tg).c_str());
							if (tyre.installed && tyre.condition > 0.0f) { tyre.condition = 0.0f; msgs.push_back(std::string("Puncture! ") + kWheelName[w] + " tyre"); }
						}
					}
					wear("steering_rack", sev * 0.7f);
				}
			}
			// Landings: struts, driveshafts, exhaust and the sump.
			if (std::fabs(L.y) > 8.0f) {
				float land = (std::fabs(L.y) - 8.0f) * 0.02f;
				for (int w = 0; w < 4; ++w) wear(("strut_" + std::string(kWheelTag[w])).c_str(), land);
				if (thr > 0.6f) wear("driveshafts", land * 1.5f);
				wear("exhaust", land * 0.5f);
				for (const auto& def : partDefs()) if (def.bolts > 0) loosen(def.id.c_str(), land * 0.15f);
			}
		}

		// --- Loose bolts work themselves out; a part with none left falls off ---
		{
			float shake = std::min(1.0f, speed / 25.0f) * (rough ? 1.5f : 0.6f);
			const auto& defs = partDefs();
			for (size_t i = 0; i < defs.size(); ++i) {
				PartState& p = c.parts[i];
				if (!p.installed || defs[i].bolts == 0) continue;
				if (p.boltsTight < defs[i].bolts && rnd() < 0.05f * shake * dt * (1.0f - (float)p.boltsTight / defs[i].bolts) * 6.0f) {
					if (p.boltsTight > 0) --p.boltsTight;
					else {
						p.installed = false;          // gone - left on the stage
						p.condition = 0.0f;
						msgs.push_back(defs[i].name + " fell off!");
						history.push_back("Lost the " + defs[i].name + " on a stage (bolts left loose).");
					}
				}
			}
		}

		// Clear the work counters and the vehicle's own damage model (career
		// damage lives in the parts instead).
		t.distance = 0; t.clutchWork = 0; t.limiterTime = 0; t.grinds = 0;
		for (int w = 0; w < 4; ++w) { t.brakeWork[w] = 0; t.tyreWork[w] = 0; t.suspensionWork[w] = 0; }
		v.engineDamage = v.steeringDamage = v.suspensionDamage = 0.0f;

		applyEffects(c, v, spec);
		return msgs;
	}

	void Career::applyEffects(const CarCondition& c, Vehicle& v, const CarSpec& spec) const {
		MechState m;
		const Fluids& f = c.fluids;
		auto H = [&](const char* id) { return c.health(id); };
		auto in = [&](const char* id) { return c.part(id).installed; };

		bool engineParts = in("cylinder_head") && in("head_gasket") && in("engine_block") && in("timing_belt");
		float block = H("engine_block");
		m.powerMul = (0.4f + 0.6f * block)
			* (in("air_filter") ? 0.8f + 0.2f * H("air_filter") : 1.0f)
			* (0.75f + 0.25f * H("spark_plugs"))
			* (H("head_gasket") < 0.3f ? 0.8f : 1.0f)
			* (0.85f + 0.15f * H("cylinder_head"))
			* (c.engineTemp > 120.0f ? 0.7f : 1.0f);
		if (spec.turbo) m.powerMul *= in("turbo") ? 0.55f + 0.45f * H("turbo") : 0.55f;
		if (!in("exhaust")) m.powerMul *= 0.95f;
		float plugs = H("spark_plugs");
		m.misfire = plugs < 0.5f ? (0.5f - plugs) * 0.8f : 0.0f;
		if (f.fuel < 1.0f) m.misfire = std::max(m.misfire, 1.0f - f.fuel);   // fuel starvation
		m.engineKill = !engineParts || block <= 0.01f || f.fuel <= 0.0f || !in("spark_plugs");
		m.noIgnition = m.engineKill || !in("battery") || f.battery * H("battery") < 0.12f;

		m.clutchMul = in("clutch") ? 0.4f + 0.6f * std::min(1.0f, H("clutch") * 1.6f) : 0.0f;
		m.noDrive = !in("clutch") || !in("gearbox") || !in("driveshafts") || H("driveshafts") <= 0.01f;
		float gb = H("gearbox");
		m.popOutChance = gb < 0.4f ? (0.4f - gb) * 0.6f : 0.0f;

		float fluid = f.brakeFluid < 0.3f ? 0.5f : 1.0f;
		float wobble = 0.0f;
		for (int w = 0; w < 4; ++w) {
			std::string tg = kWheelTag[w];
			auto h = [&](const std::string& id) { return c.health(id); };
			bool wheelOn = c.part("wheel_" + tg).installed && c.part("strut_" + tg).installed && c.part("bearing_" + tg).installed;
			if (!wheelOn) m.wheelOff |= (uint8_t)(1 << w);
			float pads = c.part("pads_" + tg).installed ? h("pads_" + tg) : 0.0f;
			float disc = c.part("disc_" + tg).installed ? h("disc_" + tg) : 0.0f;
			m.brakeMul[w] = disc <= 0.0f ? 0.05f : (0.3f + 0.7f * std::min(1.0f, pads * 3.0f)) * (0.7f + 0.3f * disc) * fluid;
			float tyre = c.part("wheel_" + tg).condition;
			m.gripMul[w] = tyre <= 0.0f ? 0.4f : 0.74f + 0.26f * std::min(1.0f, tyre * 1.6f);
			float strut = h("strut_" + tg);
			m.springMul[w] = 0.75f + 0.25f * strut;
			m.damperMul[w] = 0.3f + 0.7f * strut;
			const PartState& wp = c.part("wheel_" + tg);
			if (wp.installed && wp.boltsTight < 5) wobble += (5 - wp.boltsTight) * 0.004f;
			float br = h("bearing_" + tg);
			if (br < 0.5f) wobble += (0.5f - br) * 0.01f;
		}
		float rack = in("steering_rack") ? H("steering_rack") : 0.0f;
		m.steerPull = (c.toe[0] + c.toe[1]) * 0.5f + (rack < 0.6f ? (0.6f - rack) * 0.04f : 0.0f);
		m.steerWobble = wobble + (rack <= 0.0f ? 0.2f : 0.0f);
		v.mech = m;
		uint8_t lights = 0;
		if (H("headlight_l") < 0.3f) lights |= 1;
		if (H("headlight_r") < 0.3f) lights |= 2;
		v.lightsBroken = lights | (v.lightsBroken & 4);
	}

	float Career::stageReward(float lengthM, double time, double penalties) const {
		double par = lengthM / 21.0;                  // ~75 km/h average
		double ratio = std::clamp(par / std::max(1.0, time), 0.4, 1.6);
		float r = (float)(lengthM / 1000.0 * 320.0 * ratio * ratio);
		r *= penalties > 0.0 ? 0.85f : 1.0f;
		return std::round(r / 5.0f) * 5.0f;
	}

	// ------------------------------------------------------------------
	// Garage actions
	// ------------------------------------------------------------------
	bool Career::canRemove(const CarCondition& c, int part, std::string* why) const {
		const PartDef& d = partDefs()[part];
		if (!c.parts[part].installed) { if (why) *why = "Not fitted."; return false; }
		for (const auto& b : d.blockers) if (c.part(b).installed) { if (why) *why = "Remove the " + partDefs()[partIndex(b)].name + " first."; return false; }
		// Anything this part blocks has to come off too? No - blockers are on top; this is underneath.
		return true;
	}

	bool Career::canFit(const CarCondition& c, int part, std::string* why) const {
		const PartDef& d = partDefs()[part];
		const PartState& p = c.parts[part];
		if (p.installed) { if (why) *why = "Already fitted."; return false; }
		if (!p.spare) { if (why) *why = "No part to fit - buy one."; return false; }
		for (const auto& b : d.blockers) if (c.part(b).installed) { if (why) *why = "The " + partDefs()[partIndex(b)].name + " is in the way."; return false; }
		return true;
	}

	void Career::remove(CarCondition& c, int part) {
		PartState& p = c.parts[part];
		if (!canRemove(c, part, nullptr)) return;
		if (p.spare) scrapSpare(c, part);         // only room for one on the bench
		p.spare = true;
		p.spareCondition = p.condition;
		p.installed = false;
		p.boltsTight = 0;
		p.seen = p.condition;                     // you can see it now it's off
		p.seenAt = stageCounter;
	}

	void Career::fit(CarCondition& c, int part) {
		PartState& p = c.parts[part];
		if (!canFit(c, part, nullptr)) return;
		p.installed = true;
		p.condition = p.spareCondition;
		p.spare = false;
		p.boltsTight = 0;                         // hand-tight: tighten them!
		p.seen = p.condition;
		p.seenAt = stageCounter;
	}

	bool Career::buy(CarCondition& c, int part, bool used) {
		const PartDef& d = partDefs()[part];
		float price = used ? std::round(d.price * 0.45f) : d.price;
		if (money < price) return false;
		PartState& p = c.parts[part];
		if (p.spare) scrapSpare(c, part);
		money -= price;
		p.spare = true;
		p.spareCondition = used ? 0.45f + 0.35f * rnd() : 1.0f;
		return true;
	}

	void Career::scrapSpare(CarCondition& c, int part) {
		PartState& p = c.parts[part];
		if (!p.spare) return;
		money += std::round(partDefs()[part].price * 0.12f * p.spareCondition);
		p.spare = false;
	}

	bool Career::repair(CarCondition& c, int part) {
		const PartDef& d = partDefs()[part];
		PartState& p = c.parts[part];
		if (d.cat != PartCat::Body || d.id.rfind("headlight", 0) == 0 || d.id == "windscreen") return false;
		if (!p.installed || p.condition >= 0.8f || money < 30.0f) return false;
		money -= 30.0f;
		p.condition = std::min(0.8f, p.condition + 0.25f);
		p.seen = p.condition; p.seenAt = stageCounter;
		return true;
	}

	void Career::inspect(CarCondition& c, int part) {
		PartState& p = c.parts[part];
		p.seen = p.installed ? p.condition : p.spareCondition;
		p.seenAt = stageCounter;
	}

	float Career::refillPrice(const CarCondition& c, int what) {
		const Fluids& f = c.fluids;
		switch (what) {
		case 0: return 60.0f;                                               // oil + filter change
		case 1: return std::ceil((Fluids::kOilCap - f.oil) * 9.0f);
		case 2: return std::ceil((Fluids::kCoolantCap - f.coolant) * 4.0f);
		case 3: return std::ceil((Fluids::kBrakeCap - f.brakeFluid) * 20.0f);
		case 4: return std::ceil((Fluids::kFuelCap - f.fuel) * 1.9f);
		case 5: return f.battery < 0.99f ? 15.0f : 0.0f;
		default: return 0;
		}
	}

	bool Career::refill(CarCondition& c, int what) {
		float price = refillPrice(c, what);
		if (money < price) return false;
		Fluids& f = c.fluids;
		money -= price;
		switch (what) {
		case 0: f.oil = Fluids::kOilCap; f.oilQuality = 1.0f; break;
		case 1: f.oil = Fluids::kOilCap; break;
		case 2: f.coolant = Fluids::kCoolantCap; break;
		case 3: f.brakeFluid = Fluids::kBrakeCap; break;
		case 4: f.fuel = Fluids::kFuelCap; break;
		case 5: f.battery = 1.0f; break;
		}
		return true;
	}

	bool Career::align(CarCondition& c) {
		if (money < 80.0f) return false;
		money -= 80.0f;
		for (float& t : c.toe) t = 0.0f;
		return true;
	}

	std::vector<std::string> Career::warnings(const CarCondition& c, const CarSpec& spec) const {
		std::vector<std::string> w;
		const auto& defs = partDefs();
		for (size_t i = 0; i < defs.size(); ++i) {
			const PartState& p = c.parts[i];
			if (defs[i].turboOnly && !spec.turbo) continue;
			if (!p.installed) w.push_back(defs[i].name + " is not fitted");
			else if (defs[i].bolts > 0 && p.boltsTight < defs[i].bolts) w.push_back(defs[i].name + ": " + std::to_string(defs[i].bolts - p.boltsTight) + " loose bolt(s)");
		}
		const Fluids& f = c.fluids;
		if (f.fuel < 8.0f) w.push_back("Low fuel");
		if (f.oil < 2.5f) w.push_back("Oil level low");
		if (f.coolant < 4.0f) w.push_back("Coolant low");
		if (f.brakeFluid < 0.5f) w.push_back("Brake fluid low");
		if (f.battery < 0.3f) w.push_back("Battery flat");
		return w;
	}

	// ------------------------------------------------------------------
	// Save / load
	// ------------------------------------------------------------------
	bool Career::save(const std::string& path) const {
		nlohmann::json j;
		j["money"] = money; j["current"] = current; j["stageCounter"] = stageCounter;
		j["event"] = event; j["eventStage"] = eventStage; j["eventTime"] = eventTime;
		j["history"] = history;
		for (const auto& c : cars) {
			nlohmann::json cj;
			cj["car"] = c.carId; cj["odometer"] = c.odometer; cj["stages"] = c.stages;
			cj["toe"] = { c.toe[0], c.toe[1], c.toe[2], c.toe[3] };
			cj["fluids"] = { {"oil", c.fluids.oil}, {"oilQuality", c.fluids.oilQuality}, {"coolant", c.fluids.coolant},
				{"brakeFluid", c.fluids.brakeFluid}, {"fuel", c.fluids.fuel}, {"battery", c.fluids.battery} };
			for (size_t i = 0; i < c.parts.size(); ++i) {
				const PartState& p = c.parts[i];
				cj["parts"][partDefs()[i].id] = { {"on", p.installed}, {"cond", p.condition}, {"bolts", p.boltsTight},
					{"spare", p.spare}, {"spareCond", p.spareCondition}, {"seen", p.seen}, {"seenAt", p.seenAt} };
			}
			j["cars"].push_back(cj);
		}
		std::ofstream out(path);
		if (!out) return false;
		out << j.dump(1);
		return true;
	}

	bool Career::load(const std::string& path, const std::vector<CarSpec>& specs) {
		std::ifstream in(path);
		if (!in) return false;
		try {
			nlohmann::json j = nlohmann::json::parse(in);
			Career c;
			c.money = j.value("money", 0.0f); c.current = j.value("current", 0); c.stageCounter = j.value("stageCounter", 0);
			c.event = j.value("event", -1); c.eventStage = j.value("eventStage", 0); c.eventTime = j.value("eventTime", 0.0);
			if (j.contains("history")) c.history = j["history"].get<std::vector<std::string>>();
			for (const auto& cj : j["cars"]) {
				std::string id = cj.value("car", "");
				auto it = std::find_if(specs.begin(), specs.end(), [&](const CarSpec& s) { return s.id == id; });
				if (it == specs.end()) continue;
				CarCondition cc;
				cc.resetNew(*it);
				cc.odometer = cj.value("odometer", 0.0f); cc.stages = cj.value("stages", 0);
				if (cj.contains("toe")) for (int w = 0; w < 4; ++w) cc.toe[w] = cj["toe"][w];
				if (cj.contains("fluids")) {
					const auto& f = cj["fluids"];
					cc.fluids.oil = f.value("oil", 4.0f); cc.fluids.oilQuality = f.value("oilQuality", 1.0f); cc.fluids.coolant = f.value("coolant", 6.0f);
					cc.fluids.brakeFluid = f.value("brakeFluid", 1.0f); cc.fluids.fuel = f.value("fuel", 40.0f); cc.fluids.battery = f.value("battery", 1.0f);
				}
				if (cj.contains("parts")) {
					for (size_t i = 0; i < cc.parts.size(); ++i) {
						const std::string& pid = partDefs()[i].id;
						if (!cj["parts"].contains(pid)) continue;
						const auto& pj = cj["parts"][pid];
						PartState& p = cc.parts[i];
						p.installed = pj.value("on", true); p.condition = pj.value("cond", 1.0f); p.boltsTight = pj.value("bolts", partDefs()[i].bolts);
						p.spare = pj.value("spare", false); p.spareCondition = pj.value("spareCond", 0.0f);
						p.seen = pj.value("seen", -1.0f); p.seenAt = pj.value("seenAt", 0);
					}
				}
				c.cars.push_back(cc);
			}
			if (c.cars.empty()) return false;
			c.current = std::clamp(c.current, 0, (int)c.cars.size() - 1);
			*this = c;
			return true;
		}
		catch (const std::exception& e) {
			log::warn("career: failed to load %s: %s", path.c_str(), e.what());
			return false;
		}
	}
}
