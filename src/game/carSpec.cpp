#include "game/carSpec.h"
#include "core/log.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <algorithm>

namespace rally {
	float CarSpec::torqueAt(float rpm) const {
		if (torqueCurve.empty()) return 250.0f;
		if (rpm <= torqueCurve.front().x) return torqueCurve.front().y;
		for (size_t i = 1; i < torqueCurve.size(); ++i) {
			if (rpm <= torqueCurve[i].x) {
				const Vector2& a = torqueCurve[i - 1];
				const Vector2& b = torqueCurve[i];
				float t = (rpm - a.x) / std::max(1.0f, b.x - a.x);
				return a.y + (b.y - a.y) * t;
			}
		}
		return torqueCurve.back().y;
	}

	float CarSpec::maxTorque() const {
		float m = 0;
		for (auto& p : torqueCurve) m = std::max(m, p.y);
		return m > 0 ? m : 250.0f;
	}

	namespace {
		Color colorFrom(const nlohmann::json& j, Color def) {
			if (!j.is_array() || j.size() < 3) return def;
			return Color{ (unsigned char)j[0].get<int>(), (unsigned char)j[1].get<int>(), (unsigned char)j[2].get<int>(), 255 };
		}

		template <typename T>
		void get(const nlohmann::json& j, const char* key, T& v) {
			if (j.contains(key)) v = j[key].get<T>();
		}
	}

	bool loadCarSpecs(const std::string& path, std::vector<CarSpec>& out) {
		std::ifstream in(path);
		if (!in) { log::error("cars: cannot open %s", path.c_str()); return false; }
		try {
			nlohmann::json root = nlohmann::json::parse(in);
			std::vector<CarSpec> cars;
			for (const auto& j : root.at("cars")) {
				CarSpec c;
				get(j, "id", c.id); get(j, "name", c.name); get(j, "class", c.carClass); get(j, "description", c.description);
				std::string dt = j.value("drivetrain", "AWD");
				c.drivetrain = dt == "RWD" ? Drivetrain::RWD : dt == "FWD" ? Drivetrain::FWD : Drivetrain::AWD;
				get(j, "frontTorqueShare", c.frontTorqueShare);
				get(j, "mass", c.mass);
				if (j.contains("halfExtents")) {
					auto& h = j["halfExtents"];
					c.halfExtents = { h[0].get<float>(), h[1].get<float>(), h[2].get<float>() };
				}
				get(j, "rollInertiaScale", c.rollInertiaScale); get(j, "yawInertiaScale", c.yawInertiaScale);
				get(j, "frontAxleZ", c.frontAxleZ); get(j, "rearAxleZ", c.rearAxleZ);
				get(j, "trackHalf", c.trackHalf); get(j, "mountY", c.mountY);
				get(j, "wheelRadius", c.wheelRadius); get(j, "wheelWidth", c.wheelWidth);
				get(j, "restLength", c.restLength); get(j, "maxTravel", c.maxTravel);
				get(j, "springFront", c.springFront); get(j, "springRear", c.springRear);
				get(j, "damperFront", c.damperFront); get(j, "damperRear", c.damperRear);
				get(j, "antiRollFront", c.antiRollFront); get(j, "antiRollRear", c.antiRollRear);
				get(j, "grip", c.grip); get(j, "peakSlipAngle", c.peakSlipAngle); get(j, "slideGrip", c.slideGrip);
				get(j, "rearGripBias", c.rearGripBias); get(j, "tireForceHeight", c.tireForceHeight);
				get(j, "idleRpm", c.idleRpm); get(j, "redline", c.redline);
				if (j.contains("torqueCurve")) {
					c.torqueCurve.clear();
					for (auto& p : j["torqueCurve"]) c.torqueCurve.push_back({ p[0].get<float>(), p[1].get<float>() });
				}
				get(j, "cylinders", c.cylinders); get(j, "turbo", c.turbo);
				if (j.contains("gears")) c.gears = j["gears"].get<std::vector<float>>();
				get(j, "finalDrive", c.finalDrive); get(j, "reverseRatio", c.reverseRatio);
				get(j, "drivetrainEfficiency", c.drivetrainEfficiency); get(j, "shiftTime", c.shiftTime);
				get(j, "engineBraking", c.engineBraking); get(j, "engineInertia", c.engineInertia); get(j, "stallRpm", c.stallRpm); get(j, "clutchTorqueScale", c.clutchTorqueScale);
				get(j, "brakeForce", c.brakeForce); get(j, "brakeBias", c.brakeBias); get(j, "handbrakeForce", c.handbrakeForce);
				get(j, "maxSteerDeg", c.maxSteerDeg); get(j, "steerSpeedReduction", c.steerSpeedReduction); get(j, "steerRate", c.steerRate);
				get(j, "dragCoeff", c.dragCoeff); get(j, "rollingResistance", c.rollingResistance); get(j, "downforce", c.downforce);
				if (j.contains("bodyColor")) c.bodyColor = colorFrom(j["bodyColor"], c.bodyColor);
				if (j.contains("stripeColor")) c.stripeColor = colorFrom(j["stripeColor"], c.stripeColor);
				if (j.contains("accentColor")) c.accentColor = colorFrom(j["accentColor"], c.accentColor);
				get(j, "number", c.number); get(j, "style", c.style); get(j, "livery", c.livery);
				if (j.contains("roofColor")) { c.roofColor = colorFrom(j["roofColor"], c.roofColor); c.roofColor.a = 255; }
				if (j.contains("rimColor")) c.rimColor = colorFrom(j["rimColor"], c.rimColor);
				if (c.torqueCurve.empty()) c.torqueCurve = { {1000, 180}, {3000, 280}, {5000, 320}, {7000, 260} };
				cars.push_back(c);
			}
			if (cars.empty()) return false;
			out = std::move(cars);
			log::info("cars: loaded %d from %s", (int)out.size(), path.c_str());
			return true;
		}
		catch (const std::exception& e) {
			log::error("cars: parse error in %s: %s", path.c_str(), e.what());
			return false;
		}
	}
}
