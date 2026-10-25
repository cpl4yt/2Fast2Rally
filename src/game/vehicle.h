#pragma once
#include <raylib.h>
#include <raymath.h>
#include <cstdint>
#include "core/physicsEngine.h"
#include "core/input.h"
#include "game/carSpec.h"
#include "game/surface.h"

namespace rally {
	// What's needed to draw (and hear) a car. Produced by the local Vehicle
	// simulation every tick, or received over the network on the navigator's
	// machine - the renderer never looks at the physics directly.
	struct CarVisual {
		Vector3 position{ 0, 0, 0 };
		Quaternion rotation{ 0, 0, 0, 1 };
		Vector3 velocity{ 0, 0, 0 };
		float steerAngle = 0.0f;        // radians, front wheels
		float steerInput = 0.0f;        // -1..1 (for the steering wheel)
		float wheelOffset[4]{};         // wheel centre offset below its mount (m)
		float wheelSpinRate[4]{};       // rad/s
		float wheelSlip[4]{};           // 0..1 visual slip (smoke/spray)
		uint8_t wheelSurface[4]{};
		uint8_t wheelContact = 0;       // bitmask
		float rpm = 900.0f;
		int gear = 1;                   // -1 R, 0 N, 1..n
		float throttle = 0.0f, brake = 0.0f;
		bool handbrake = false;
		float boost = 0.0f;
		float speed = 0.0f;             // m/s forward
		float dirt = 0.0f;              // 0..1
		float damage = 0.0f;            // 0..1 overall
		uint8_t lightsBroken = 0;       // bit0 left, bit1 right, bit2 pod
		uint8_t wheelMissing = 0;       // bitmask: wheels not on the car (career)
	};

	// Wheel indices: 0 FL, 1 FR, 2 RL, 3 RR.
	struct Wheel {
		Vector3 mountLocal{};
		bool front = false, left = false;
		float compression = 0.0f, prevCompression = 0.0f;
		bool contact = false;
		Vector3 contactPoint{}, contactNormal{ 0, 1, 0 };
		float load = 0.0f;              // N
		Surface surface = Surface::Gravel;
		float slipAngle = 0.0f;
		float slipRatio = 0.0f;         // visual/audio: 0 grip .. 1 full slide/spin
		float spinRate = 0.0f;          // rad/s
		float longForce = 0.0f, latForce = 0.0f;
		bool locked = false, spinning = false;
	};

	struct VehicleTelemetry {
		float lateralG = 0, longG = 0;
		float driveForce = 0;
		int wheelsOnGround = 0;
		float frontSlip = 0, rearSlip = 0;
		// Accumulated work since the career/mechanics layer last read (and zeroed) it.
		float brakeWork[4]{};          // J dissipated by each brake
		float clutchWork = 0;          // J dissipated by a slipping clutch
		float tyreWork[4]{};           // J of sliding at each contact patch
		float suspensionWork[4]{};     // J through each damper
		float distance = 0;            // m travelled
		float limiterTime = 0;         // s spent on the rev limiter
		int grinds = 0;                // gear grinds
	};

	// Condition of the car's parts as it affects driving (career mode; the
	// defaults are a perfect car). Written by the mechanics layer each tick.
	struct MechState {
		float powerMul = 1.0f;         // engine output (wear, plugs, filter, turbo, heat...)
		float clutchMul = 1.0f;        // clutch capacity (worn clutch slips)
		float brakeMul[4] = { 1, 1, 1, 1 };
		float gripMul[4] = { 1, 1, 1, 1 };    // tyre tread / puncture
		float springMul[4] = { 1, 1, 1, 1 };
		float damperMul[4] = { 1, 1, 1, 1 };
		float steerPull = 0.0f;        // radians of toe/rack misalignment
		float steerWobble = 0.0f;      // radians of shake (loose wheel / bearing)
		float popOutChance = 0.0f;     // per second, a worn gearbox jumps to neutral
		float misfire = 0.0f;          // 0..1 chance per firing interval of a stumble
		uint8_t wheelOff = 0;          // bitmask: wheels that have fallen off / aren't fitted
		bool noDrive = false;          // clutch or gearbox missing
		bool noIgnition = false;       // dead battery / no plugs / no fuel: can't (re)start
		bool engineKill = false;       // seized / out of fuel / belt snapped: stops it now
	};

	class Vehicle {
	public:
		void init(PhysicsEngine& physics, const CarSpec& spec, Vector3 pos, float yaw);
		void destroy();
		// Swap spec in place (tuning hot-reload) keeping position/velocity.
		void applySpec(const CarSpec& spec);

		// One fixed simulation tick. Applies suspension/tire/engine forces as
		// impulses on the chassis body; call before PhysicsEngine::step().
		void update(float dt, const DriveInput& in, const Ground& ground);
		// After PhysicsEngine::step(): impact detection, damage, visual state.
		void postStep(float dt);

		void resetTo(Vector3 pos, float yaw);

		// --- State ---
		const CarSpec& spec() const { return spec_; }
		Vector3 position() const;
		Quaternion rotation() const;
		Vector3 velocity() const;
		Vector3 forward() const;
		Vector3 up() const;
		Vector3 right() const;
		float speed() const;            // signed forward speed m/s
		float rpm() const { return rpm_; }
		int gear() const { return gear_; }
		float boost() const { return boost_; }
		bool engineCut() const { return limiter_; }
		const Wheel& wheel(int i) const { return wheels_[i]; }
		const VehicleTelemetry& telemetry() const { return tele_; }
		const CarVisual& visual() const { return visual_; }

		// Damage & dirt
		float engineDamage = 0.0f;      // 0..1, reduces power
		float steeringDamage = 0.0f;    // 0..1, adds pull
		float suspensionDamage = 0.0f;
		float dirt = 0.0f;
		uint8_t lightsBroken = 0;
		float lastImpact = 0.0f;        // m/s of the last significant hit (consumed by game)
		Vector3 lastImpactLocal{};      // its velocity change in car space (+z forward, +x left)
		int impactCount = 0;
		MechState mech;                 // part condition effects (career)
		VehicleTelemetry& telemetryMut() { return tele_; }

		bool upsideDown() const;
		float timeUpsideDown = 0.0f;
		int transmission = 0;           // 0 automatic, 1 sequential, 2 H-pattern + clutch
		bool engineRunning() const { return engineRunning_; }
		float clutchEngagement() const { return clutchEngage_; }
		float clutchPedal() const { return clutchPedal_; }
		bool absEnabled = true;
		bool tcsEnabled = true;
		bool antiFlip = true;
		bool engineEnabled = true;      // false during countdown hold

		// Events for audio (consumed by the game each frame).
		int pendingBackfires = 0;
		bool pendingShift = false;
		bool pendingBlowOff = false;
		bool pendingGrind = false;
		bool pendingStall = false;

	private:
		void computeWheelForces(float dt, const DriveInput& in, const Ground& ground, float driveForceTotal, float brakeIn);
		void updateGearbox(float dt, const DriveInput& in, float fwdSpeed);
		float wheelRpm() const;
		float drivenSpin() const;       // signed average driven-wheel spin (rad/s)
		float gearRatioSigned() const;  // total ratio incl. final drive, negative in reverse

		PhysicsEngine* physics_ = nullptr;
		int body_ = -1;
		CarSpec spec_;
		Wheel wheels_[4];
		float steer_ = 0.0f;            // smoothed input -1..1
		float steerAngle_ = 0.0f;
		float rpm_ = 900.0f;
		int gear_ = 1;
		float shiftTimer_ = 0.0f;
		float reverseHold_ = 0.0f;
		bool limiter_ = false;
		float boost_ = 0.0f;
		float prevThrottle_ = 0.0f;
		float throttle_ = 0.0f;
		float clutchSlip_ = 0.0f;
		Vector3 prevVelocity_{};
		VehicleTelemetry tele_;
		CarVisual visual_;
		float spinAngle_[4]{};
		bool handbrake_ = false;
		float brakeInput_ = 0.0f;
		float engineOmega_ = 0.0f;      // rad/s
		bool engineRunning_ = true;
		float clutchEngage_ = 1.0f;     // 0 open .. 1 closed
		float clutchPedal_ = 0.0f;
		float driveUsed_ = 0.0f;        // N actually delivered to the ground last tick
		float grindCooldown_ = 0.0f;
		float misfireTimer_ = 0.0f;
		unsigned rng_ = 0x2468ace1u;
		float rand01() { rng_ = rng_ * 1664525u + 1013904223u; return (rng_ >> 8) / float(1 << 24); }
	};

	// Quaternion helpers used across game code.
	inline Vector3 rotateVec(Quaternion q, Vector3 v) { return Vector3RotateByQuaternion(v, q); }
	inline float yawOf(Quaternion q) {
		Vector3 f = rotateVec(q, { 0, 0, 1 });
		return atan2f(f.x, f.z);
	}
}
