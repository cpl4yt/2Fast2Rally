#pragma once
#include <raylib.h>
#include <raymath.h>
#include <vector>
#include <functional>

namespace rally {
	// A small but real impulse-based rigid-body physics engine.
	//
	// This replaces the old hand-rolled resolveCollisions() push-out code
	// (which only ever moved the player and treated everything else as an
	// immovable AABB). Every collidable thing is now a box rigid body with
	// a full 6-DOF state (position + quaternion orientation, linear +
	// angular velocity), mass properties (inverse mass + inverse inertia
	// tensor), and a material (friction + restitution). Collisions are
	// found with a Separating-Axis-Theorem box/box test that produces a
	// real contact manifold, and resolved with a sequential-impulse solver
	// (Coulomb friction, restitution, and Baumgarte penetration
	// correction). Bodies also collide against the noise heightmap terrain
	// via a height callback.
	//
	// Design note - who is dynamic:
	//   The world holds thousands of trees/rocks/placed blocks, but almost
	//   all of them never move. Rather than make every one a persistent
	//   body (and pay broad-phase over all of them every frame), only
	//   genuinely moving things - the player, and any block the caller
	//   deliberately leaves un-"landlocked" so it can fall/topple - are
	//   persistent Bodies here. Immovable world geometry near the player is
	//   handed in per-step as a cheap list of StaticBox colliders (the
	//   caller already culls those by distance for rendering). This keeps
	//   the simulation cost tied to how much is actually moving, not to how
	//   large the world is.
	//
	//   `landlocked` is the caller-facing switch for that: a landlocked
	//   body is pinned - infinite mass, no gravity, no integration - so it
	//   can never fall or topple. Un-landlock a block and it becomes a full
	//   dynamic body that gravity, stacking, and impacts act on.
	class PhysicsEngine {
	public:
		enum BodyKind {
			KIND_PLAYER,
			KIND_BLOCK,
			KIND_VEHICLE
		};

		struct Body {
			// --- Shape (oriented box) ---
			Vector3 halfExtents{ 0.5f, 0.5f, 0.5f };

			// --- State ---
			Vector3 position{ 0.0f, 0.0f, 0.0f };  // center of mass, world space
			Quaternion orientation{ 0.0f, 0.0f, 0.0f, 1.0f };
			Vector3 linearVelocity{ 0.0f, 0.0f, 0.0f };
			Vector3 angularVelocity{ 0.0f, 0.0f, 0.0f };

			// --- Mass properties ---
			// invMass == 0 means infinite mass (immovable). Set via
			// setMass()/makeLandlocked() rather than by hand so the inertia
			// tensor stays consistent with the shape.
			float invMass = 1.0f;
			Matrix invInertiaLocal = MatrixIdentity(); // 0 matrix when rotation is locked/infinite

			// --- Material ---
			float friction = 0.6f;
			float restitution = 0.0f;

			// --- Flags ---
			// landlocked: pinned in place, cannot fall or topple (acts as a
			//   static collider). lockRotation: dynamic (can be moved) but
			//   never spins - used for the upright player capsule so it
			//   can't tip over. ghost: integrated but generates no contacts
			//   (used for creative-mode noclip). useGravity: whether the
			//   world's gravity accelerates it.
			bool landlocked = false;
			bool lockRotation = false;
			bool ghost = false;
			bool useGravity = true;

			// When false the body is skipped entirely for the step (no
			// gravity, no contacts, no integration) - it freezes in place.
			// Used to cheaply dormant-park enemies that are far from the
			// player so a big population never costs anything until it's
			// actually near the action.
			bool simulated = true;

			// Stand-still assist: while set AND the body is grounded this
			// substep, its horizontal velocity is zeroed right before
			// position integration. Without this, a velocity-driven
			// character standing on a slope drifts downhill forever: the
			// caller zeroes its velocity every frame, but the per-substep
			// gravity kick gets redirected sideways by the slope's contact
			// normal *within* the step, after the caller last touched it.
			// The caller (player movement code) sets this exactly when
			// there is no movement input, so it never fights walking - and
			// it only applies while grounded, so jumps/falls are untouched.
			bool holdStillOnGround = false;

			int kind = KIND_BLOCK;

			// Extra local-space points tested against the terrain on top of the
			// box's 8 corners (vehicle roof, panel middles) so a body rests on
			// its real outline instead of sinking until a corner touches.
			std::vector<Vector3> probes;

			// --- Per-step outputs (read by the caller after step()) ---
			// grounded: had a support contact whose normal pointed
			//   substantially upward this step. landingImpactSpeed: how fast
			//   the body was moving *into* the ground on the step it first
			//   became grounded (0 otherwise) - the caller turns this into
			//   fall damage.
			bool grounded = false;
			float landingImpactSpeed = 0.0f;

			bool active = true; // slot in use (false = free-listed, reusable)
		};

		struct StaticBox {
			Vector3 center;
			Vector3 halfExtents;
			float friction = 0.9f;
			float restitution = 0.0f;
		};

		PhysicsEngine();

		// Downward gravity acceleration magnitude (world units / s^2).
		void setGravity(float accel) { gravity = accel; }
		float getGravity() const { return gravity; }

		// Terrain height field the bodies rest on. `heightFn(x, z)` returns
		// the ground height at that world XZ; passing enabled=false makes
		// bodies ignore terrain entirely (flat void).
		void setTerrain(std::function<float(float, float)> heightFn, bool enabled) {
			terrainHeight = std::move(heightFn);
			terrainEnabled = enabled;
		}

		// Adds a body and returns its stable id (an index into the internal
		// store; reused slots keep ids compact). Configure mass via the
		// helpers below before/after adding.
		int addBody(const Body& body);
		void removeBody(int id);
		Body* getBody(int id);             // nullptr if id is invalid/freed
		const Body* getBody(int id) const;
		void clear();

		// Sets a dynamic box's mass and derives its local inverse inertia
		// tensor from its current half-extents. mass <= 0 makes it
		// infinite (immovable). Call again if the half-extents change.
		void setBoxMass(int id, float mass);

		// Pins/unpins a body. A landlocked body keeps its shape (still
		// collidable) but is treated as immovable: no gravity, no
		// integration, infinite mass.
		void setLandlocked(int id, bool landlocked);

		// Applies a world-space impulse (N*s) at a world-space point, changing
		// both linear and angular velocity. Used by the vehicle's suspension
		// and tire forces, which are computed outside the contact solver.
		void applyImpulseAtPoint(int id, Vector3 impulse, Vector3 worldPoint);
		// World-space inverse inertia of a body (zero if rotation is locked).
		Matrix inverseInertiaWorld(int id) const;

		// Advances the simulation by dt seconds. `nearbyStatics` are the
		// immovable world colliders close enough to matter this frame (the
		// caller supplies these; the engine does not store them). Terrain
		// is handled via the height callback.
		void step(float dt, const std::vector<StaticBox>& nearbyStatics);

	private:
		// One collision proxy per moving body + per supplied static, built
		// fresh each step. Solving happens on these; results are written
		// back into the persistent dynamic bodies at the end.
		struct Collider {
			Vector3 position;
			Quaternion orientation;
			Vector3 halfExtents;
			Vector3 linearVelocity;
			Vector3 angularVelocity;
			float invMass;
			Matrix invInertiaWorld;
			float friction;
			float restitution;
			int bodyId; // -1 for statics / terrain
		};

		struct Contact {
			int a, b;              // indices into the per-step collider list
			Vector3 point;
			Vector3 normal;        // unit, points from a toward b
			float penetration;

			// Precomputed at solve prep:
			Vector3 rA, rB;        // contact point relative to each center
			Vector3 tangent1, tangent2;
			float normalMass;
			float tangentMass1, tangentMass2;
			float velocityBias;
			float normalImpulse;   // accumulated (for friction clamp)
			bool terrain;          // support-from-terrain, for grounded flag
		};

		float gravity = 15.0f;
		std::function<float(float, float)> terrainHeight;
		bool terrainEnabled = false;

		std::vector<Body> bodies;
		std::vector<int> freeSlots;

		// Reused across steps to avoid per-frame heap churn.
		std::vector<Collider> colliders;
		std::vector<Contact> contacts;

		bool validId(int id) const { return id >= 0 && id < (int)bodies.size() && bodies[id].active; }

		Matrix worldInverseInertia(const Body& b) const;

		// Box/box manifold via SAT. Fills `out` with up to 8 contacts
		// (normal from a to b) and returns the count, 0 if separated.
		int collideBoxBox(const Collider& a, const Collider& b, Contact* out) const;

		// Box/terrain contacts for one dynamic collider; appends to
		// `contacts` with `terrainIndex` as the static side.
		void collideTerrain(int colliderIndex, int terrainIndex);

		Vector3 terrainNormal(float x, float z) const;
	};
}
