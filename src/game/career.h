#pragma once
#include <raylib.h>
#include <string>
#include <vector>
#include "game/carSpec.h"
#include "game/vehicle.h"

namespace rally {
	// Career mode: you own cars that wear out and break, and fix them yourself
	// in the garage between stages - every part can be inspected, unbolted,
	// bought new or used, refitted and bolted back up (loose bolts work
	// themselves out on the stage; a wheel with no nuts comes off).

	enum class PartCat { Engine, Drivetrain, Brakes, Suspension, Wheels, Steering, Body, Count };
	const char* partCatName(PartCat c);

	struct PartDef {
		std::string id, name;
		PartCat cat;
		int bolts;                       // 0 = clips/seats in, nothing to tighten
		float price;                     // new
		std::vector<std::string> blockers; // must be off the car to remove or fit this
		Vector3 where;                   // rough position on the car (for the garage marker)
		std::string info;                // what it does / what failing looks like
		bool turboOnly = false;
		int wheel = -1;                  // 0 FL, 1 FR, 2 RL, 3 RR for per-wheel parts
	};
	const std::vector<PartDef>& partDefs();
	int partIndex(const std::string& id);

	struct PartState {
		bool installed = true;
		float condition = 1.0f;          // 0 broken .. 1 new
		int boltsTight = 0;              // of PartDef::bolts
		bool spare = false;              // a loose copy on the garage floor (removed or bought)
		float spareCondition = 0.0f;
		float seen = -1.0f;              // condition when last inspected (-1 never)
		int seenAt = 0;                  // stage counter when inspected
	};

	struct Fluids {
		float oil = 4.0f, oilQuality = 1.0f;   // litres of 4.0
		float coolant = 6.0f;                  // of 6.0
		float brakeFluid = 1.0f;               // of 1.0
		float fuel = 45.0f;                    // of 60
		float battery = 1.0f;                  // charge
		static constexpr float kOilCap = 4.0f, kCoolantCap = 6.0f, kBrakeCap = 1.0f, kFuelCap = 60.0f;
	};

	struct CarCondition {
		std::string carId;
		std::vector<PartState> parts;    // indexed like partDefs()
		Fluids fluids;
		float toe[4]{};                  // wheel alignment error (rad), from knocks
		float odometer = 0;              // km
		int stages = 0;
		float engineTemp = 20.0f;        // runtime, °C
		void resetNew(const CarSpec& spec);
		PartState& part(const std::string& id) { return parts[partIndex(id)]; }
		const PartState& part(const std::string& id) const { return parts[partIndex(id)]; }
		// Effective health 0..1 including loose bolts; 0 if not fitted.
		float health(const std::string& id) const;
	};

	struct CarOffer { std::string carId; float price; };

	class Career {
	public:
		bool active = false;             // a career stage is being driven
		float money = 2500.0f;
		std::vector<CarCondition> cars;  // owned
		int current = 0;
		int stageCounter = 0;            // for "inspected N stages ago"
		// Event in progress.
		int event = -1, eventStage = 0;
		double eventTime = 0;
		std::vector<std::string> history; // what happened (shown in the garage)
		int seenImpacts_ = 0;            // vehicle impact counter already processed

		bool load(const std::string& path, const std::vector<CarSpec>& specs);
		bool save(const std::string& path) const;
		void startNew(const std::vector<CarSpec>& specs, int carIndex);
		CarCondition* car() { return (current >= 0 && current < (int)cars.size()) ? &cars[current] : nullptr; }
		bool owns(const std::string& carId) const;
		static float carPrice(const CarSpec& spec);

		// Wear, failures and damage while driving; writes the car's MechState.
		// Returns messages for the HUD ("Front-left wheel came off!").
		std::vector<std::string> simulate(CarCondition& c, Vehicle& v, const CarSpec& spec, float dt, bool running, bool roughGround);
		// Effects only (garage, or before the stage starts).
		void applyEffects(const CarCondition& c, Vehicle& v, const CarSpec& spec) const;
		// Prize for a finished stage.
		float stageReward(float lengthM, double time, double penalties) const;

		// Garage actions; return false + reason when not possible.
		bool canRemove(const CarCondition& c, int part, std::string* why) const;
		bool canFit(const CarCondition& c, int part, std::string* why) const;
		void remove(CarCondition& c, int part);
		void fit(CarCondition& c, int part);              // spare -> car, bolts loose
		bool buy(CarCondition& c, int part, bool used);   // new / used spare
		void scrapSpare(CarCondition& c, int part);
		bool repair(CarCondition& c, int part);           // body panels: beat out dents
		void inspect(CarCondition& c, int part);
		bool refill(CarCondition& c, int what);           // 0 oil change, 1 top up oil, 2 coolant, 3 brake fluid, 4 fuel, 5 charge battery
		bool align(CarCondition& c);
		static float refillPrice(const CarCondition& c, int what);
		// Missing / broken things that will stop the car or matter on the stage.
		std::vector<std::string> warnings(const CarCondition& c, const CarSpec& spec) const;
	};
}
