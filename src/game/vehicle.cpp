#include "game/vehicle.h"
#include <cmath>
#include <algorithm>

namespace rally {
	namespace {
		constexpr float kG = 9.81f;
		constexpr float kRadToRpm = 60.0f / (2.0f * PI);

		Vector3 add(Vector3 a, Vector3 b) { return Vector3Add(a, b); }
		Vector3 sub(Vector3 a, Vector3 b) { return Vector3Subtract(a, b); }
		Vector3 scale(Vector3 v, float s) { return Vector3Scale(v, s); }
		float dot(Vector3 a, Vector3 b) { return Vector3DotProduct(a, b); }
		Vector3 cross(Vector3 a, Vector3 b) { return Vector3CrossProduct(a, b); }

		float signf(float v) { return v < 0.0f ? -1.0f : 1.0f; }

		Vector3 groundNormal(const Ground& g, float x, float z) {
			const float e = 0.4f;
			float hL = g.heightAt(x - e, z), hR = g.heightAt(x + e, z);
			float hD = g.heightAt(x, z - e), hU = g.heightAt(x, z + e);
			return Vector3Normalize({ (hL - hR) / (2 * e), 1.0f, (hD - hU) / (2 * e) });
		}
	}

	void Vehicle::init(PhysicsEngine& physics, const CarSpec& spec, Vector3 pos, float yaw) {
		physics_ = &physics;
		PhysicsEngine::Body b;
		b.halfExtents = spec.halfExtents;
		b.position = pos;
		b.orientation = QuaternionFromAxisAngle({ 0, 1, 0 }, yaw);
		b.friction = 0.45f;
		b.restitution = 0.05f;
		b.kind = PhysicsEngine::KIND_VEHICLE;
		body_ = physics.addBody(b);
		applySpec(spec);
		resetTo(pos, yaw);
	}

	void Vehicle::destroy() {
		if (physics_ && body_ >= 0) physics_->removeBody(body_);
		body_ = -1;
	}

	void Vehicle::applySpec(const CarSpec& spec) {
		spec_ = spec;
		if (!physics_ || body_ < 0) return;
		PhysicsEngine::Body* b = physics_->getBody(body_);
		b->halfExtents = spec.halfExtents;
		physics_->setBoxMass(body_, spec.mass);
		b->invInertiaLocal.m5 /= std::max(0.1f, spec.yawInertiaScale);
		b->invInertiaLocal.m10 /= std::max(0.1f, spec.rollInertiaScale);

		// Wheel 0 FL, 1 FR, 2 RL, 3 RR. Local +x is the car's LEFT side
		// (y up, z forward, right-handed).
		for (int i = 0; i < 4; ++i) {
			Wheel& w = wheels_[i];
			w.front = i < 2;
			w.left = (i % 2) == 0;
			w.mountLocal = { w.left ? spec.trackHalf : -spec.trackHalf, spec.mountY, w.front ? spec.frontAxleZ : spec.rearAxleZ };
		}
		gear_ = std::clamp(gear_, -1, (int)spec.gears.size());
	}

	void Vehicle::resetTo(Vector3 pos, float yaw) {
		if (!physics_ || body_ < 0) return;
		PhysicsEngine::Body* b = physics_->getBody(body_);
		b->position = pos;
		b->orientation = QuaternionFromAxisAngle({ 0, 1, 0 }, yaw);
		b->linearVelocity = { 0, 0, 0 };
		b->angularVelocity = { 0, 0, 0 };
		for (Wheel& w : wheels_) { w.compression = w.prevCompression = 0.0f; w.spinRate = 0; w.contact = false; }
		gear_ = 1;
		rpm_ = spec_.idleRpm;
		shiftTimer_ = 0.0f;
		boost_ = 0.0f;
		steer_ = 0.0f;
		timeUpsideDown = 0.0f;
		prevVelocity_ = { 0, 0, 0 };
	}

	Vector3 Vehicle::position() const { auto* b = physics_->getBody(body_); return b ? b->position : Vector3{}; }
	Quaternion Vehicle::rotation() const { auto* b = physics_->getBody(body_); return b ? b->orientation : QuaternionIdentity(); }
	Vector3 Vehicle::velocity() const { auto* b = physics_->getBody(body_); return b ? b->linearVelocity : Vector3{}; }
	Vector3 Vehicle::forward() const { return rotateVec(rotation(), { 0, 0, 1 }); }
	Vector3 Vehicle::up() const { return rotateVec(rotation(), { 0, 1, 0 }); }
	Vector3 Vehicle::right() const { return rotateVec(rotation(), { -1, 0, 0 }); }
	float Vehicle::speed() const { return dot(velocity(), forward()); }
	bool Vehicle::upsideDown() const { return up().y < 0.15f; }

	float Vehicle::wheelRpm() const {
		float sum = 0; int n = 0;
		for (int i = 0; i < 4; ++i) {
			const Wheel& w = wheels_[i];
			bool driven = spec_.drivetrain == Drivetrain::AWD || (spec_.drivetrain == Drivetrain::RWD ? !w.front : w.front);
			if (!driven) continue;
			sum += std::fabs(w.spinRate);
			++n;
		}
		return n ? sum / n : 0.0f;
	}

	void Vehicle::updateGearbox(float dt, const DriveInput& in, float fwdSpeed) {
		int nG = (int)spec_.gears.size();
		if (shiftTimer_ > 0.0f) shiftTimer_ -= dt;
		int before = gear_;

		if (manualGearbox) {
			if (in.shiftUp && gear_ < nG) ++gear_;
			if (in.shiftDown && gear_ > -1) --gear_;
		}
		else {
			if (gear_ >= 1) {
				if (engineEnabled && in.brake > 0.5f && in.throttle < 0.1f && fwdSpeed < 0.8f) {
					reverseHold_ += dt;
					if (reverseHold_ > 0.35f) { gear_ = -1; reverseHold_ = 0; }
				}
				else reverseHold_ = 0.0f;

				if (shiftTimer_ <= 0.0f && gear_ >= 1) {
					bool slipping = false;
					for (const Wheel& w : wheels_) if (w.spinning) slipping = true;
					if (rpm_ > spec_.redline * 0.95f && gear_ < nG && throttle_ > 0.2f && !slipping) ++gear_;
					else if (gear_ > 1) {
						float down = (throttle_ > 0.3f ? 0.56f : 0.40f) * spec_.redline;
						float rpmBelow = rpm_ * spec_.gears[gear_ - 2] / spec_.gears[gear_ - 1];
						if (rpm_ < down && rpmBelow < spec_.redline * 0.9f) --gear_;
					}
				}
			}
			else if (gear_ == -1) {
				if (in.throttle > 0.5f && in.brake < 0.1f && fwdSpeed > -0.8f) {
					reverseHold_ += dt;
					if (reverseHold_ > 0.2f) { gear_ = 1; reverseHold_ = 0; }
				}
				else reverseHold_ = 0.0f;
			}
			else gear_ = 1;
		}

		if (!engineEnabled && gear_ < 1 && !manualGearbox) gear_ = 1;
		if (gear_ != before) {
			shiftTimer_ = spec_.shiftTime;
			pendingShift = true;
			if (gear_ > before && boost_ > 0.5f) pendingBlowOff = true;
		}
	}

	void Vehicle::update(float dt, const DriveInput& in, const Ground& ground) {
		PhysicsEngine::Body* b = physics_->getBody(body_);
		if (!b) return;
		const Quaternion q = b->orientation;
		const Vector3 pos = b->position;
		const Vector3 fwd = rotateVec(q, { 0, 0, 1 });
		const Vector3 upV = rotateVec(q, { 0, 1, 0 });
		const float fwdSpeed = dot(b->linearVelocity, fwd);
		const float absSpeed = Vector3Length(b->linearVelocity);

		// --- Steering ---
		float maxStep = spec_.steerRate * dt;
		steer_ += std::clamp(in.steer - steer_, -maxStep, maxStep);
		float lockScale = 1.0f + (spec_.steerSpeedReduction - 1.0f) * std::clamp(std::fabs(fwdSpeed) / 35.0f, 0.0f, 1.0f);
		steerAngle_ = steer_ * spec_.maxSteerDeg * DEG2RAD * lockScale + steeringDamage * 0.06f;

		// --- Gearbox & pedals ---
		updateGearbox(dt, in, fwdSpeed);
		float driveThrottle = gear_ < 0 ? in.brake : in.throttle;
		float brakePedal = gear_ < 0 ? in.throttle : in.brake;
		handbrake_ = in.handbrake;
		brakeInput_ = brakePedal;

		// Backfire / anti-lag crackle when lifting off at high revs.
		if (prevThrottle_ > 0.7f && driveThrottle < 0.15f && rpm_ > spec_.redline * 0.55f) {
			pendingBackfires += 2 + (int)(rpm_ / spec_.redline * 3.0f);
			if (spec_.turbo && boost_ > 0.4f) pendingBlowOff = true;
		}
		prevThrottle_ = driveThrottle;
		throttle_ = driveThrottle;

		// --- Engine ---
		int nG = (int)spec_.gears.size();
		float ratio = gear_ > 0 ? spec_.gears[std::min(gear_, nG) - 1] : (gear_ < 0 ? spec_.reverseRatio : 0.0f);
		float totalRatio = ratio * spec_.finalDrive;
		bool shifting = shiftTimer_ > 0.0f;
		bool anySpin = false;
		for (const Wheel& w : wheels_) if (w.spinning) anySpin = true;

		float rpmWheels = wheelRpm() * totalRatio * kRadToRpm;
		float freeTarget = spec_.idleRpm + driveThrottle * (spec_.redline - spec_.idleRpm) * 1.04f;
		if (gear_ == 0 || shifting || !engineEnabled) {
			float rate = (freeTarget > rpm_) ? 7.0f : 3.0f;
			float target = shifting ? std::max(rpmWheels, spec_.idleRpm) : freeTarget;
			rpm_ += (target - rpm_) * std::min(1.0f, rate * dt);
		}
		else {
			// Auto clutch: slips at launch so the engine sits at a useful rpm.
			float launchFloor = (std::abs(gear_) == 1) ? spec_.idleRpm * 0.9f + driveThrottle * 2400.0f : spec_.idleRpm * 0.9f;
			float target = std::max(rpmWheels, launchFloor);
			if (anySpin) target = std::max(target, rpm_ + (freeTarget - rpm_) * 5.0f * dt);
			rpm_ += (target - rpm_) * std::min(1.0f, 16.0f * dt);
		}
		rpm_ = std::clamp(rpm_, spec_.idleRpm * 0.8f, spec_.redline * 1.03f);
		if (rpm_ >= spec_.redline) limiter_ = true;
		else if (rpm_ < spec_.redline * 0.96f) limiter_ = false;

		float boostTarget = spec_.turbo ? std::clamp((rpm_ - 2600.0f) / 2400.0f, 0.0f, 1.0f) * driveThrottle : 0.0f;
		boost_ += (boostTarget - boost_) * std::min(1.0f, dt * (boostTarget > boost_ ? 1.6f : 4.0f));

		float torque = 0.0f;
		if (driveThrottle > 0.05f) {
			torque = limiter_ ? 0.0f : spec_.torqueAt(rpm_) * driveThrottle;
			if (spec_.turbo) torque *= 0.62f + 0.38f * boost_;
			torque *= 1.0f - 0.45f * engineDamage;
		}
		else if (gear_ != 0) {
			torque = -spec_.engineBraking * spec_.torqueAt(rpm_) * (rpm_ / spec_.redline);
		}
		float driveForce = torque * totalRatio * spec_.drivetrainEfficiency / spec_.wheelRadius;
		if (gear_ < 0) driveForce = -driveForce;
		if (gear_ == 0 || shifting || !engineEnabled) driveForce = 0.0f;
		tele_.driveForce = driveForce;

		computeWheelForces(dt, in, ground, driveForce, brakePedal);

		// Driven wheels that are spinning are tied to the engine.
		for (int i = 0; i < 4; ++i) {
			Wheel& w = wheels_[i];
			bool driven = spec_.drivetrain == Drivetrain::AWD || (spec_.drivetrain == Drivetrain::RWD ? !w.front : w.front);
			if (driven && (w.spinning || !w.contact) && gear_ != 0 && !shifting && totalRatio > 0.0f && engineEnabled) {
				float engineRate = rpm_ / kRadToRpm / totalRatio * (gear_ < 0 ? -1.0f : 1.0f);
				if (!w.contact) w.spinRate += (engineRate - w.spinRate) * std::min(1.0f, 6.0f * dt);
				else w.spinRate = engineRate;
			}
			else if (!w.contact) {
				w.spinRate *= (1.0f - std::min(1.0f, 0.8f * dt));
			}
			if (handbrake_ && !w.front) w.spinRate = 0.0f;
		}

		// --- Aero & damping ---
		Vector3 v = b->linearVelocity;
		Vector3 drag = scale(v, -spec_.dragCoeff * absSpeed);
		Vector3 down = scale(upV, -spec_.downforce * fwdSpeed * fwdSpeed);
		physics_->applyImpulseAtPoint(body_, scale(add(drag, down), dt), pos);

		int grounded = 0;
		for (const Wheel& w : wheels_) if (w.contact) ++grounded;
		tele_.wheelsOnGround = grounded;
		float angDamp = grounded == 0 ? 0.5f : 0.12f;
		b->angularVelocity = scale(b->angularVelocity, 1.0f - std::min(1.0f, angDamp * dt));

		// Gentle self-righting when tipped on its side at low speed - keeps
		// the car from getting beached on a door after a slow-speed roll.
		if (antiFlip && grounded <= 1 && upV.y < 0.55f && absSpeed < 6.0f) {
			Vector3 axis = cross(upV, { 0, 1, 0 });
			b->angularVelocity = add(b->angularVelocity, scale(axis, 2.2f * dt));
		}

		timeUpsideDown = (upV.y < 0.15f && absSpeed < 3.0f) ? timeUpsideDown + dt : 0.0f;
		prevVelocity_ = b->linearVelocity;
	}

	void Vehicle::computeWheelForces(float dt, const DriveInput& in, const Ground& ground, float driveForceTotal, float brakePedal) {
		PhysicsEngine::Body* b = physics_->getBody(body_);
		const Quaternion q = b->orientation;
		const Vector3 pos = b->position;
		const Vector3 upV = rotateVec(q, { 0, 1, 0 });
		const Vector3 dir = scale(upV, -1.0f);
		const float L = spec_.restLength + spec_.wheelRadius;

		// Pass 1: raycasts & compression.
		float springForce[4]{};
		for (int i = 0; i < 4; ++i) {
			Wheel& w = wheels_[i];
			w.prevCompression = w.compression;
			Vector3 mount = add(pos, rotateVec(q, w.mountLocal));
			w.contact = false;
			if (dir.y < -0.3f) {
				float t = (mount.y - ground.heightAt(mount.x, mount.z)) / -dir.y;
				for (int it = 0; it < 3; ++it) {
					Vector3 p = add(mount, scale(dir, t));
					t = (mount.y - ground.heightAt(p.x, p.z)) / -dir.y;
				}
				if (t < L && t > -0.6f) {
					w.contact = true;
					w.compression = L - t;
					w.contactPoint = add(mount, scale(dir, t));
					w.contactNormal = groundNormal(ground, w.contactPoint.x, w.contactPoint.z);
					w.surface = ground.surfaceAt(w.contactPoint.x, w.contactPoint.z);
				}
			}
			if (!w.contact) {
				w.compression = 0.0f;
				w.contactPoint = add(mount, scale(dir, L));
			}
			float k = w.front ? spec_.springFront : spec_.springRear;
			float c = w.front ? spec_.damperFront : spec_.damperRear;
			k *= 1.0f - 0.25f * suspensionDamage;
			if (w.contact) {
				float comp = std::min(w.compression, spec_.maxTravel);
				float compVel = (w.compression - w.prevCompression) / dt;
				float f = k * comp + c * compVel;
				if (w.compression > spec_.maxTravel) f += k * 10.0f * (w.compression - spec_.maxTravel); // bump stop
				springForce[i] = std::max(0.0f, f);
			}
		}

		// Anti-roll bars.
		for (int axle = 0; axle < 2; ++axle) {
			int l = axle * 2, r = axle * 2 + 1;
			float arb = (axle == 0 ? spec_.antiRollFront : spec_.antiRollRear) * (wheels_[l].compression - wheels_[r].compression);
			if (wheels_[l].contact) springForce[l] = std::max(0.0f, springForce[l] + arb);
			if (wheels_[r].contact) springForce[r] = std::max(0.0f, springForce[r] - arb);
		}

		// Pass 2: tire forces.
		float frontSlip = 0, rearSlip = 0;
		for (int i = 0; i < 4; ++i) {
			Wheel& w = wheels_[i];
			w.longForce = w.latForce = 0.0f;
			w.spinning = w.locked = false;
			w.slipRatio = 0.0f;
			if (!w.contact) { w.load = 0.0f; w.slipAngle = 0.0f; continue; }

			float Fs = springForce[i];
			physics_->applyImpulseAtPoint(body_, scale(upV, Fs * dt), w.contactPoint);
			Vector3 n = w.contactNormal;
			float Fz = Fs * std::max(0.2f, dot(upV, n));
			w.load = Fz;

			// Wheel heading, projected onto the ground plane.
			float a = w.front ? -steerAngle_ : 0.0f; // +steer = right = toward -x
			Vector3 headLocal = { std::sin(a), 0.0f, std::cos(a) };
			Vector3 head = rotateVec(q, headLocal);
			Vector3 fwdG = Vector3Normalize(sub(head, scale(n, dot(head, n))));
			Vector3 sideG = cross(n, fwdG); // points to the car's left

			Vector3 r = sub(w.contactPoint, pos);
			Vector3 vPoint = add(b->linearVelocity, cross(b->angularVelocity, r));
			float vLong = dot(vPoint, fwdG);
			float vLat = dot(vPoint, sideG);

			const SurfaceProps& sp = surfaceProps(w.surface);
			float mu = sp.friction * spec_.grip * ground.gripMultiplier(w.surface) * (w.front ? 1.0f : spec_.rearGripBias);
			float maxF = mu * Fz;
			float massShare = Fz / kG;

			// --- Longitudinal ---
			bool driven = spec_.drivetrain == Drivetrain::AWD || (spec_.drivetrain == Drivetrain::RWD ? !w.front : w.front);
			float share = 0.0f;
			if (driven) {
				if (spec_.drivetrain == Drivetrain::AWD) share = (w.front ? spec_.frontTorqueShare : 1.0f - spec_.frontTorqueShare) * 0.5f;
				else share = 0.5f;
			}
			bool hb = handbrake_ && !w.front;
			float Fdrive = (hb && spec_.drivetrain != Drivetrain::FWD) ? 0.0f : driveForceTotal * share;
			if (tcsEnabled && Fdrive > 0.0f) {
				// Traction control: keep drive inside what the tyre can take,
				// leaving more for cornering the more the tyre is already sliding.
				float lat = std::min(1.0f, std::fabs(w.slipAngle) / (spec_.peakSlipAngle * sp.slipScale));
				float cap = maxF * std::clamp(0.97f - 0.45f * lat, 0.45f, 0.97f);
				Fdrive = std::min(Fdrive, cap);
			}

			float brakeMag = brakePedal * spec_.brakeForce * (w.front ? spec_.brakeBias : 1.0f - spec_.brakeBias) * 0.5f;
			if (hb) brakeMag += spec_.handbrakeForce * 0.5f;
			float Fbrake = 0.0f;
			if (brakeMag > 0.0f) {
				if (std::fabs(vLong) < 0.4f) Fbrake = std::clamp(-vLong * massShare / dt, -brakeMag, brakeMag);
				else Fbrake = -signf(vLong) * brakeMag;
			}
			float Froll = -vLong * spec_.rollingResistance * sp.rolling * 0.25f;

			float Fx = Fdrive + Fbrake + Froll;
			float latCap = 1.0f;
			if (std::fabs(Fbrake) > maxF * 0.95f && std::fabs(vLong) > 1.0f) {
				if (absEnabled && !hb) {
					Fx = Fdrive + Froll - signf(vLong) * maxF * 0.92f;
				}
				else {
					w.locked = true;
					Fx = -signf(vLong) * maxF * spec_.slideGrip * sp.slideGrip;
					latCap = 0.35f;
				}
			}
			else if (std::fabs(Fx) > maxF) {
				if (std::fabs(Fdrive) > std::fabs(Fbrake)) {
					w.spinning = true;
					latCap = 0.55f;
				}
				Fx = signf(Fx) * maxF * 0.94f;
			}
			if (hb) { latCap = std::min(latCap, 0.42f); w.locked = std::fabs(vLong) > 1.0f; }

			// --- Lateral ---
			float slipAngle = std::atan2(vLat, std::max(std::fabs(vLong), 3.0f));
			w.slipAngle = slipAngle;
			float peak = spec_.peakSlipAngle * sp.slipScale;
			float x = std::fabs(slipAngle) / peak;
			float slide = std::min(1.0f, spec_.slideGrip * sp.slideGrip);
			float curve = x < 1.0f ? std::sin(x * PI * 0.5f) : 1.0f - (1.0f - slide) * std::min(1.0f, (x - 1.0f) / 1.5f);
			float usage = std::min(1.0f, std::fabs(Fx) / std::max(1.0f, maxF));
			latCap = std::min(latCap, std::sqrt(std::max(0.08f, 1.0f - usage * usage)));
			float Fy = -signf(slipAngle) * curve * maxF * latCap;
			// Don't push past zero lateral velocity in one tick (standstill jitter).
			float stopLimit = std::fabs(vLat) * massShare / dt;
			Fy = std::clamp(Fy, -stopLimit, stopLimit);

			w.longForce = Fx;
			w.latForce = Fy;

			float h = dot(sub(pos, w.contactPoint), upV) * spec_.tireForceHeight;
			Vector3 applyAt = add(w.contactPoint, scale(upV, h));
			physics_->applyImpulseAtPoint(body_, scale(add(scale(fwdG, Fx), scale(sideG, Fy)), dt), applyAt);

			// Visual/audio slip measure.
			float s = std::max(0.0f, x - 0.7f) * 0.7f;
			if (w.spinning) s += 0.8f;
			if (w.locked) s += 0.8f;
			w.slipRatio = std::clamp(s, 0.0f, 1.0f);
			if (w.front) frontSlip = std::max(frontSlip, x); else rearSlip = std::max(rearSlip, x);

			if (!w.spinning) w.spinRate = w.locked ? 0.0f : vLong / spec_.wheelRadius;
		}
		tele_.frontSlip = frontSlip;
		tele_.rearSlip = rearSlip;
	}

	void Vehicle::postStep(float dt) {
		PhysicsEngine::Body* b = physics_->getBody(body_);
		if (!b) return;
		const Quaternion q = b->orientation;
		Vector3 v = b->linearVelocity;

		// --- Impacts: velocity change the contact solver applied this tick ---
		Vector3 dv = sub(v, prevVelocity_);
		dv.y += 9.81f * dt; // remove gravity
		float impact = Vector3Length(dv);
		if (impact > 2.8f) {
			lastImpact = impact;
			++impactCount;
			Vector3 local = Vector3RotateByQuaternion(dv, QuaternionInvert(q));
			float severity = (impact - 2.8f) * 0.035f;
			if (local.z < -1.5f) {          // pushed backwards: frontal hit
				engineDamage = std::min(1.0f, engineDamage + severity);
				if (impact > 5.0f) {
					lightsBroken |= (local.x > 0.5f) ? 1 : (local.x < -0.5f) ? 2 : 3;
					if (impact > 8.0f) lightsBroken |= 4;
				}
			}
			if (std::fabs(local.x) > 1.5f) steeringDamage = std::min(1.0f, steeringDamage + severity * 0.6f);
			suspensionDamage = std::min(1.0f, suspensionDamage + severity * 0.3f);
		}
		Vector3 accel = scale(sub(v, prevVelocity_), 1.0f / dt);
		Vector3 rightV = rotateVec(q, { -1, 0, 0 });
		Vector3 fwdV = rotateVec(q, { 0, 0, 1 });
		tele_.lateralG += (dot(accel, rightV) / 9.81f - tele_.lateralG) * 0.1f;
		tele_.longG += (dot(accel, fwdV) / 9.81f - tele_.longG) * 0.1f;

		// --- Dirt builds up on loose surfaces ---
		float spd = Vector3Length(v);
		int loose = 0;
		for (const Wheel& w : wheels_) if (w.contact && surfaceProps(w.surface).loose) ++loose;
		dirt = std::min(1.0f, dirt + dt * spd * 0.00045f * loose * 0.25f);

		// --- Visual state ---
		CarVisual& vis = visual_;
		vis.position = b->position;
		vis.rotation = q;
		vis.velocity = v;
		vis.steerAngle = steerAngle_;
		vis.steerInput = steer_;
		vis.wheelContact = 0;
		const float L = spec_.restLength + spec_.wheelRadius;
		for (int i = 0; i < 4; ++i) {
			const Wheel& w = wheels_[i];
			float t = w.contact ? (L - w.compression) : L;
			vis.wheelOffset[i] = std::clamp(t - spec_.wheelRadius, spec_.restLength - spec_.maxTravel, spec_.restLength);
			vis.wheelSpinRate[i] = w.spinRate;
			vis.wheelSlip[i] = w.slipRatio;
			vis.wheelSurface[i] = (uint8_t)w.surface;
			if (w.contact) vis.wheelContact |= (1 << i);
		}
		vis.rpm = rpm_;
		vis.gear = gear_;
		vis.throttle = throttle_;
		vis.brake = brakeInput_;
		vis.handbrake = handbrake_;
		vis.boost = boost_;
		vis.speed = dot(v, fwdV);
		vis.dirt = dirt;
		vis.damage = std::clamp((engineDamage + steeringDamage + suspensionDamage) / 2.0f, 0.0f, 1.0f);
		vis.lightsBroken = lightsBroken;
	}
}
