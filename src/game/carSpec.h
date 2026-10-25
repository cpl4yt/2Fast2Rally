#pragma once
#include <raylib.h>
#include <string>
#include <vector>

namespace rally {
	enum class Drivetrain { AWD, RWD, FWD };

	// Everything that defines how a car drives and looks. Loaded from
	// res/data/cars.json so it can be tuned and hot-reloaded (F5) in-game.
	struct CarSpec {
		std::string id = "car";
		std::string name = "Car";
		std::string carClass = "";
		std::string description = "";
		std::string soundPack = "";       // res/audio/cars/<soundPack>/ (defaults to id)
		std::string model = "";           // res/models/cars/<model>_body.obj etc; empty = procedural body
		Drivetrain drivetrain = Drivetrain::AWD;
		float frontTorqueShare = 0.45f;   // AWD only

		float mass = 1250.0f;
		Vector3 halfExtents = { 0.88f, 0.33f, 2.1f };  // physics box
		float rollInertiaScale = 1.6f;    // extra roll inertia for stability
		float yawInertiaScale = 1.0f;

		// Wheels (relative to body centre). Front axle +z.
		float frontAxleZ = 1.28f, rearAxleZ = -1.22f;
		float trackHalf = 0.78f;
		float mountY = -0.12f;
		float wheelRadius = 0.32f;
		float wheelWidth = 0.22f;

		// Suspension
		float restLength = 0.30f;
		float maxTravel = 0.24f;
		float springFront = 42000.0f, springRear = 40000.0f;
		float damperFront = 3600.0f, damperRear = 3400.0f;
		float antiRollFront = 9000.0f, antiRollRear = 6000.0f;

		// Tires
		float grip = 1.0f;                // multiplier on surface friction
		float peakSlipAngle = 0.15f;      // radians, where lateral force peaks
		float slideGrip = 0.78f;          // fraction of peak once fully sliding
		float rearGripBias = 1.0f;        // <1 = looser rear
		float tireForceHeight = 0.45f;    // 0 = contact patch, 1 = CoM (less roll)

		// Engine / gearbox
		float idleRpm = 950.0f, redline = 7400.0f;
		std::vector<Vector2> torqueCurve; // (rpm, Nm)
		int cylinders = 4;
		bool turbo = true;
		std::vector<float> gears = { 3.2f, 2.15f, 1.6f, 1.25f, 1.0f, 0.84f };
		float finalDrive = 4.2f;
		float reverseRatio = 3.3f;
		float drivetrainEfficiency = 0.85f;
		float shiftTime = 0.14f;
		float engineBraking = 0.12f;
		float engineInertia = 0.25f;      // kg m^2, flywheel + crank
		float stallRpm = 420.0f;
		float clutchTorqueScale = 1.6f;   // clutch capacity vs. peak engine torque

		// Brakes / steering
		float brakeForce = 11000.0f;      // total, N
		float brakeBias = 0.62f;          // front share
		float handbrakeForce = 6500.0f;
		float maxSteerDeg = 30.0f;
		float steerSpeedReduction = 0.45f;// fraction of lock left at high speed
		float steerRate = 4.0f;           // input smoothing (1/s)

		// Aero / misc
		float dragCoeff = 0.42f;          // F = c * v^2
		float rollingResistance = 14.0f;  // F = c * v
		float downforce = 0.25f;          // F = c * v^2

		// Look
		Color bodyColor = { 230, 230, 235, 255 };
		Color stripeColor = { 220, 40, 40, 255 };
		Color accentColor = { 30, 60, 160, 255 };
		int number = 7;
		int style = 0;                    // body: 0 hatch, 1 wedge, 2 supermini, 3 saloon, 4 Group B, 5 mini, 6 WRC, 7 70s saloon, 8 raid 4x4
		int livery = 0;                   // 0 stripes, 1 two-tone, 2 diagonal sweep, 3 racing stripes
		Color roofColor = { 0, 0, 0, 0 }; // alpha 0 = body colour
		Color rimColor = { 200, 200, 205, 255 };

		float torqueAt(float rpm) const;
		float maxTorque() const;
	};

	// Loads all cars from json; returns false (and keeps defaults) on failure.
	bool loadCarSpecs(const std::string& path, std::vector<CarSpec>& out);
}
