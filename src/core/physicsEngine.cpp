#include "physicsEngine.h"
#include <algorithm>
#include <cmath>

namespace rally {
	namespace {
		// --- Solver tuning ---
		const int   VELOCITY_ITERATIONS = 10;   // sequential-impulse passes per step
		const float BAUMGARTE = 0.2f;           // fraction of penetration corrected per step
		const float PENETRATION_SLOP = 0.005f;  // penetration allowed before correction kicks in
		const float RESTITUTION_THRESHOLD = 1.0f; // min approach speed before bounce is applied
		const float MAX_STEP_DT = 0.05f;        // clamp huge frames (alt-tab, breakpoints) so nothing explodes
		const int   MAX_SUBSTEPS = 4;
		const int   MAX_VELOCITY = 15;

		// Contacts touching the player use a much gentler correction than
		// the general solver default: rigid blocks want to snap apart
		// quickly (BAUMGARTE/MAX_VELOCITY above), but the same strength
		// applied to the player reads as violent shoving whenever they clip
		// into steep terrain or a placed object - walking uphill stutter-
		// pops instead of just... walking. Slower to resolve, but smooth is
		// what actually matters for a first-person body.
		const float PLAYER_BAUMGARTE = 0.06f;
		const float PLAYER_MAX_VELOCITY = 3.0f;

		Vector3 add(Vector3 a, Vector3 b) { return Vector3Add(a, b); }
		Vector3 sub(Vector3 a, Vector3 b) { return Vector3Subtract(a, b); }
		Vector3 scale(Vector3 v, float s) { return Vector3Scale(v, s); }
		float   dot(Vector3 a, Vector3 b) { return Vector3DotProduct(a, b); }
		Vector3 cross(Vector3 a, Vector3 b) { return Vector3CrossProduct(a, b); }

		// v transformed by a 3x3 matrix (rotation/inertia); translation ignored.
		Vector3 mat3Mul(const Matrix& m, Vector3 v) {
			return {
				m.m0 * v.x + m.m4 * v.y + m.m8 * v.z,
				m.m1 * v.x + m.m5 * v.y + m.m9 * v.z,
				m.m2 * v.x + m.m6 * v.y + m.m10 * v.z
			};
		}

		// The three world-space unit axes of an oriented box.
		void boxAxes(const Quaternion& q, Vector3 axes[3]) {
			Matrix r = QuaternionToMatrix(q);
			axes[0] = { r.m0, r.m1, r.m2 };
			axes[1] = { r.m4, r.m5, r.m6 };
			axes[2] = { r.m8, r.m9, r.m10 };
		}

		// The eight world-space corners of an oriented box.
		void boxVertices(Vector3 center, const Vector3 axes[3], Vector3 e, Vector3 out[8]) {
			for (int i = 0; i < 8; ++i) {
				float sx = (i & 1) ? 1.0f : -1.0f;
				float sy = (i & 2) ? 1.0f : -1.0f;
				float sz = (i & 4) ? 1.0f : -1.0f;
				Vector3 v = center;
				v = add(v, scale(axes[0], sx * e.x));
				v = add(v, scale(axes[1], sy * e.y));
				v = add(v, scale(axes[2], sz * e.z));
				out[i] = v;
			}
		}

		// Projection radius of a box onto a unit axis L.
		float projectRadius(const Vector3 axes[3], Vector3 e, Vector3 L) {
			return e.x * std::fabs(dot(axes[0], L)) +
			       e.y * std::fabs(dot(axes[1], L)) +
			       e.z * std::fabs(dot(axes[2], L));
		}

		// Clip a convex polygon against a half-space { x : dot(n, x) <= d }.
		// Sutherland-Hodgman on one plane. Keeps points on the inside.
		void clipToPlane(std::vector<Vector3>& poly, Vector3 n, float d) {
			if (poly.empty()) return;
			std::vector<Vector3> out;
			out.reserve(poly.size() + 1);
			for (size_t i = 0; i < poly.size(); ++i) {
				Vector3 cur = poly[i];
				Vector3 nxt = poly[(i + 1) % poly.size()];
				float dc = dot(n, cur) - d;
				float dn = dot(n, nxt) - d;
				bool curIn = dc <= 0.0f;
				bool nxtIn = dn <= 0.0f;
				if (curIn) out.push_back(cur);
				if (curIn != nxtIn) {
					float t = dc / (dc - dn);
					out.push_back(add(cur, scale(sub(nxt, cur), t)));
				}
			}
			poly.swap(out);
		}

		float extentOnAxis(Vector3 e, int axis) {
			return axis == 0 ? e.x : axis == 1 ? e.y : e.z;
		}

		// The face of a box (4 vertices) whose outward normal is most
		// aligned with `dir`. Returns the outward normal too.
		void bestFace(Vector3 center, const Vector3 axes[3], Vector3 e, Vector3 dir,
		              Vector3 faceOut[4], Vector3& normalOut) {
			int bestAxis = 0;
			float bestDot = -1.0f;
			float sign = 1.0f;
			for (int i = 0; i < 3; ++i) {
				float d = dot(axes[i], dir);
				if (std::fabs(d) > bestDot) {
					bestDot = std::fabs(d);
					bestAxis = i;
					sign = (d >= 0.0f) ? 1.0f : -1.0f;
				}
			}
			normalOut = scale(axes[bestAxis], sign);
			int u = (bestAxis + 1) % 3;
			int v = (bestAxis + 2) % 3;
			Vector3 faceCenter = add(center, scale(normalOut, extentOnAxis(e, bestAxis)));
			Vector3 au = scale(axes[u], extentOnAxis(e, u));
			Vector3 av = scale(axes[v], extentOnAxis(e, v));
			faceOut[0] = add(add(faceCenter, au), av);
			faceOut[1] = sub(add(faceCenter, au), av);
			faceOut[2] = sub(sub(faceCenter, au), av);
			faceOut[3] = add(sub(faceCenter, au), av);
		}
	}

	PhysicsEngine::PhysicsEngine() {
		// Upfront capacity so early gameplay (player body, first few
		// dynamic blocks/enemies added) doesn't pay for several small
		// reallocate-and-copy growth steps - sized generously above
		// typical live body/contact counts (player + enemies + a modest
		// handful of toppled/falling blocks at once).
		bodies.reserve(128);
		freeSlots.reserve(32);
		colliders.reserve(192);
		contacts.reserve(256);
	}

	Matrix PhysicsEngine::worldInverseInertia(const Body& b) const {
		if (b.invMass == 0.0f || b.lockRotation || b.landlocked) {
			// No angular response: return the zero matrix so every angular
			// term in the solver drops out cleanly.
			Matrix z{};
			return z;
		}
		Matrix r = QuaternionToMatrix(b.orientation);
		Matrix rt = MatrixTranspose(r);
		// I_world^-1 = R * I_local^-1 * R^T
		return MatrixMultiply(MatrixMultiply(r, b.invInertiaLocal), rt);
	}

	int PhysicsEngine::addBody(const Body& body) {
		int id;
		if (!freeSlots.empty()) {
			id = freeSlots.back();
			freeSlots.pop_back();
			bodies[id] = body;
		}
		else {
			id = (int)bodies.size();
			bodies.push_back(body);
		}
		bodies[id].active = true;
		return id;
	}

	void PhysicsEngine::removeBody(int id) {
		if (!validId(id)) return;
		bodies[id].active = false;
		freeSlots.push_back(id);
	}

	PhysicsEngine::Body* PhysicsEngine::getBody(int id) {
		return validId(id) ? &bodies[id] : nullptr;
	}

	const PhysicsEngine::Body* PhysicsEngine::getBody(int id) const {
		return validId(id) ? &bodies[id] : nullptr;
	}

	void PhysicsEngine::clear() {
		bodies.clear();
		freeSlots.clear();
	}

	void PhysicsEngine::setBoxMass(int id, float mass) {
		if (!validId(id)) return;
		Body& b = bodies[id];
		if (mass <= 0.0f) {
			b.invMass = 0.0f;
			b.invInertiaLocal = Matrix{};
			return;
		}
		b.invMass = 1.0f / mass;

		// Solid box inertia about its center, using full side lengths.
		float w = 2.0f * b.halfExtents.x;
		float h = 2.0f * b.halfExtents.y;
		float d = 2.0f * b.halfExtents.z;
		float ix = (1.0f / 12.0f) * mass * (h * h + d * d);
		float iy = (1.0f / 12.0f) * mass * (w * w + d * d);
		float iz = (1.0f / 12.0f) * mass * (w * w + h * h);

		Matrix inv{};
		inv.m0 = 1.0f / ix;
		inv.m5 = 1.0f / iy;
		inv.m10 = 1.0f / iz;
		inv.m15 = 1.0f;
		b.invInertiaLocal = inv;
	}

	void PhysicsEngine::setLandlocked(int id, bool landlocked) {
		if (!validId(id)) return;
		Body& b = bodies[id];
		b.landlocked = landlocked;
		if (landlocked) {
			b.invMass = 0.0f;
			b.invInertiaLocal = Matrix{};
			b.linearVelocity = { 0, 0, 0 };
			b.angularVelocity = { 0, 0, 0 };
			b.useGravity = false;
		}
	}

	void PhysicsEngine::applyImpulseAtPoint(int id, Vector3 impulse, Vector3 worldPoint) {
		if (!validId(id)) return;
		Body& b = bodies[id];
		if (b.landlocked || b.invMass == 0.0f) return;
		b.linearVelocity = add(b.linearVelocity, scale(impulse, b.invMass));
		if (!b.lockRotation) {
			Vector3 r = sub(worldPoint, b.position);
			b.angularVelocity = add(b.angularVelocity, mat3Mul(worldInverseInertia(b), cross(r, impulse)));
		}
	}

	Matrix PhysicsEngine::inverseInertiaWorld(int id) const {
		if (!validId(id)) return Matrix{};
		return worldInverseInertia(bodies[id]);
	}

	Vector3 PhysicsEngine::terrainNormal(float x, float z) const {
		// Central differences of the height field give the surface slope;
		// the upward normal is (-dh/dx, 1, -dh/dz) normalized.
		const float eps = 0.5f;
		float hL = terrainHeight(x - eps, z);
		float hR = terrainHeight(x + eps, z);
		float hD = terrainHeight(x, z - eps);
		float hU = terrainHeight(x, z + eps);
		Vector3 n = { (hL - hR) / (2.0f * eps), 1.0f, (hD - hU) / (2.0f * eps) };
		return Vector3Normalize(n);
	}

	int PhysicsEngine::collideBoxBox(const Collider& a, const Collider& b, Contact* out) const {
		Vector3 axA[3], axB[3];
		boxAxes(a.orientation, axA);
		boxAxes(b.orientation, axB);
		Vector3 T = sub(b.position, a.position);

		float bestOverlap = 1e30f;
		int bestType = -1;   // 0 = face of A, 1 = face of B, 2 = edge-edge
		int bestIndex = -1;  // which axis within its group
		Vector3 bestAxis = { 0, 1, 0 };

		auto testAxis = [&](Vector3 L, int type, int index) -> bool {
			float lenSq = dot(L, L);
			if (lenSq < 1e-8f) return true; // near-parallel edge cross: skip, not separating
			L = scale(L, 1.0f / std::sqrt(lenSq));
			float rA = projectRadius(axA, a.halfExtents, L);
			float rB = projectRadius(axB, b.halfExtents, L);
			float dist = std::fabs(dot(T, L));
			float overlap = rA + rB - dist;
			if (overlap < 0.0f) return false; // separating axis found
			if (overlap < bestOverlap) {
				bestOverlap = overlap;
				bestType = type;
				bestIndex = index;
				// Orient the axis to point from A toward B.
				bestAxis = (dot(T, L) < 0.0f) ? scale(L, -1.0f) : L;
			}
			return true;
		};

		for (int i = 0; i < 3; ++i) if (!testAxis(axA[i], 0, i)) return 0;
		for (int i = 0; i < 3; ++i) if (!testAxis(axB[i], 1, i)) return 0;
		for (int i = 0; i < 3; ++i)
			for (int j = 0; j < 3; ++j)
				if (!testAxis(cross(axA[i], axB[j]), 2, i * 3 + j)) return 0;

		if (bestType < 0) return 0;

		// --- Edge-edge: single contact at the closest points of the two edges ---
		if (bestType == 2) {
			int i = bestIndex / 3;
			int j = bestIndex % 3;
			Vector3 axisA = axA[i];
			Vector3 axisB = axB[j];

			// Pick the edge of each box on the side the normal points toward.
			Vector3 pA = a.position;
			for (int k = 0; k < 3; ++k) {
				if (k == i) continue;
				float s = dot(axA[k], bestAxis) >= 0.0f ? 1.0f : -1.0f;
				pA = add(pA, scale(axA[k], s * extentOnAxis(a.halfExtents, k)));
			}
			Vector3 pB = b.position;
			for (int k = 0; k < 3; ++k) {
				if (k == j) continue;
				float s = dot(axB[k], bestAxis) >= 0.0f ? -1.0f : 1.0f;
				pB = add(pB, scale(axB[k], s * extentOnAxis(b.halfExtents, k)));
			}

			// Closest points between infinite lines (pA,axisA) and (pB,axisB).
			Vector3 r = sub(pA, pB);
			float aa = dot(axisA, axisA);
			float e = dot(axisA, axisB);
			float f = dot(axisB, axisB);
			float c = dot(axisA, r);
			float bdot = dot(axisB, r);
			float denom = aa * f - e * e;
			float s = 0.0f, t = 0.0f;
			if (std::fabs(denom) > 1e-6f) s = (e * bdot - c * f) / denom;
			t = (e * s + bdot) / (f > 1e-6f ? f : 1.0f);
			float extA = extentOnAxis(a.halfExtents, i);
			float extB = extentOnAxis(b.halfExtents, j);
			s = std::max(-extA, std::min(extA, s));
			t = std::max(-extB, std::min(extB, t));
			Vector3 cA = add(pA, scale(axisA, s));
			Vector3 cB = add(pB, scale(axisB, t));

			out[0].point = scale(add(cA, cB), 0.5f);
			out[0].normal = bestAxis;
			out[0].penetration = bestOverlap;
			out[0].terrain = false;
			return 1;
		}

		// --- Face case: clip the incident face against the reference face ---
		const Collider* ref = (bestType == 0) ? &a : &b;
		const Collider* inc = (bestType == 0) ? &b : &a;
		const Vector3* refAxes = (bestType == 0) ? axA : axB;
		const Vector3* incAxes = (bestType == 0) ? axB : axA;
		// Reference normal points from ref outward toward inc.
		Vector3 refNormal = (bestType == 0) ? bestAxis : scale(bestAxis, -1.0f);

		int refAxisIdx = bestIndex;
		float refSign = dot(refAxes[refAxisIdx], refNormal) >= 0.0f ? 1.0f : -1.0f;
		Vector3 refFaceCenter = add(ref->position,
			scale(refAxes[refAxisIdx], refSign * extentOnAxis(ref->halfExtents, refAxisIdx)));

		Vector3 incFace[4]; Vector3 incNormal;
		bestFace(inc->position, incAxes, inc->halfExtents, scale(refNormal, -1.0f), incFace, incNormal);

		std::vector<Vector3> poly(incFace, incFace + 4);

		// Side planes of the reference face (the two axes orthogonal to the
		// reference normal), clipping the incident polygon to the face's
		// footprint.
		for (int k = 0; k < 3; ++k) {
			if (k == refAxisIdx) continue;
			float ext = extentOnAxis(ref->halfExtents, k);
			Vector3 sideAxis = refAxes[k];
			float centerProj = dot(sideAxis, refFaceCenter);
			clipToPlane(poly, sideAxis, centerProj + ext);
			clipToPlane(poly, scale(sideAxis, -1.0f), -(centerProj - ext));
		}

		// Keep clipped points that are below (inside) the reference face,
		// each becoming a contact with its own penetration depth.
		int count = 0;
		float refPlaneDist = dot(refNormal, refFaceCenter);
		for (const Vector3& p : poly) {
			float pen = refPlaneDist - dot(refNormal, p); // >0 means inside/behind ref face
			if (pen < -PENETRATION_SLOP) continue;
			if (count >= 8) break;
			// Normal must always point from a toward b, regardless of which
			// box supplied the reference face.
			out[count].point = p;
			out[count].normal = bestAxis;
			out[count].penetration = std::max(pen, 0.0f);
			out[count].terrain = false;
			++count;
		}

		// Numerical fallback: if clipping produced nothing, drop a single
		// contact at the incident face center so we never silently miss a
		// real overlap.
		if (count == 0) {
			out[0].point = scale(add(add(incFace[0], incFace[1]), add(incFace[2], incFace[3])), 0.25f);
			out[0].normal = bestAxis;
			out[0].penetration = bestOverlap;
			out[0].terrain = false;
			count = 1;
		}

		return count;
	}

	void PhysicsEngine::collideTerrain(int colliderIndex, int terrainIndex) {
		const Collider& c = colliders[colliderIndex];
		Vector3 axes[3];
		boxAxes(c.orientation, axes);
		Vector3 verts[8];
		boxVertices(c.position, axes, c.halfExtents, verts);

		for (int i = 0; i < 8; ++i) {
			float h = terrainHeight(verts[i].x, verts[i].z);
			if (verts[i].y < h) {
				Contact ct{};
				ct.a = colliderIndex;      // dynamic body
				ct.b = terrainIndex;       // static terrain proxy
				ct.point = verts[i];
				ct.normal = terrainNormal(verts[i].x, verts[i].z); // points up, out of ground
				ct.penetration = h - verts[i].y;
				ct.terrain = true;
				// Normal convention is "from a to b"; here b is the ground,
				// so the outward (up) normal must point from the body toward
				// the ground, i.e. downward. Flip it, and the solver's
				// grounded test looks for an upward normal on the body side.
				ct.normal = scale(ct.normal, -1.0f);
				contacts.push_back(ct);
			}
		}
	}

	void PhysicsEngine::step(float dt, const std::vector<StaticBox>& nearbyStatics) {
		if (dt <= 0.0f) return;

		// Break big frames into fixed-ish substeps so fast bodies can't
		// tunnel and the solver stays stable.
		int substeps = 1;
		if (dt > MAX_STEP_DT) {
			substeps = std::min(MAX_SUBSTEPS, (int)std::ceil(dt / MAX_STEP_DT));
		}
		float h = dt / substeps;

		for (int s = 0; s < substeps; ++s) {
			// Clear per-step outputs; grounded/landing are recomputed below.
			for (Body& b : bodies) {
				if (!b.active) continue;
				b.grounded = false;
				b.landingImpactSpeed = 0.0f;
			}

			// --- Integrate velocities (gravity) ---
			for (Body& b : bodies) {
				if (!b.active || !b.simulated || b.landlocked || b.invMass == 0.0f) continue;
				if (b.useGravity && !b.ghost) {
					b.linearVelocity.y -= gravity * h;
				}
			}

			// --- Build collider proxies ---
			colliders.clear();
			colliders.reserve(bodies.size() + nearbyStatics.size() + 1);

			for (int i = 0; i < (int)bodies.size(); ++i) {
				Body& b = bodies[i];
				if (!b.active || b.ghost || !b.simulated) continue;
				Collider col;
				col.position = b.position;
				col.orientation = b.orientation;
				col.halfExtents = b.halfExtents;
				col.linearVelocity = b.linearVelocity;
				col.angularVelocity = b.angularVelocity;
				col.invMass = b.landlocked ? 0.0f : b.invMass;
				col.invInertiaWorld = worldInverseInertia(b);
				col.friction = b.friction;
				col.restitution = b.restitution;
				col.bodyId = i;
				colliders.push_back(col);
			}

			int firstStaticIndex = (int)colliders.size();
			for (const StaticBox& sb : nearbyStatics) {
				Collider col{};
				col.position = sb.center;
				col.orientation = QuaternionIdentity();
				col.halfExtents = sb.halfExtents;
				col.linearVelocity = { 0, 0, 0 };
				col.angularVelocity = { 0, 0, 0 };
				col.invMass = 0.0f;
				col.invInertiaWorld = Matrix{};
				col.friction = sb.friction;
				col.restitution = sb.restitution;
				col.bodyId = -1;
				colliders.push_back(col);
			}

			// Terrain pseudo-collider (immovable). Its material only matters
			// via friction combine below; restitution 0 so nothing bounces
			// off the ground.
			int terrainIndex = (int)colliders.size();
			{
				Collider col{};
				col.position = { 0, 0, 0 };
				col.orientation = QuaternionIdentity();
				col.halfExtents = { 0, 0, 0 };
				col.invMass = 0.0f;
				col.invInertiaWorld = Matrix{};
				col.friction = 1.0f;
				col.restitution = 0.0f;
				col.bodyId = -1;
				colliders.push_back(col);
			}

			// --- Broad + narrow phase ---
			contacts.clear();
			int dynamicCount = firstStaticIndex;
			Contact manifold[8];

			for (int i = 0; i < dynamicCount; ++i) {
				// Only depends on i, not j - hoisted out of the inner loop
				// below so this O(N) amount of work isn't redone O(N) times
				// per i (an O(N^2) sqrt call for what's really O(N) worth
				// of values).
				float rA = Vector3Length(colliders[i].halfExtents);
				// Dynamic vs everything after it (other dynamics + statics).
				for (int j = i + 1; j < terrainIndex; ++j) {
					// Cheap sphere reject before the full SAT.
					float rB = Vector3Length(colliders[j].halfExtents);
					float d2 = Vector3LengthSqr(sub(colliders[j].position, colliders[i].position));
					float rr = rA + rB;
					if (d2 > rr * rr) continue;

					int n = collideBoxBox(colliders[i], colliders[j], manifold);
					for (int k = 0; k < n; ++k) {
						Contact ct = manifold[k];
						ct.a = i;
						ct.b = j;

						// Step-offset filter (ghost-collision fix): walking
						// across a floor tiled from flush blocks, the player
						// box drifts a hair into the NEXT block (penetration
						// slop) and its side face emits a horizontal contact
						// - an invisible wall at every seam, even though the
						// two tops are perfectly coplanar. For the PLAYER
						// only: drop near-horizontal contacts against any
						// box whose top is at or barely above the feet.
						// There's nothing there tall enough to genuinely
						// block a capsule that's already standing at that
						// height - the support contacts keep holding it up,
						// and real walls (tops well above the feet) still
						// collide normally.
						const float stepOffset = 0.25f;
						int playerSide = -1;
						if (colliders[i].bodyId >= 0 && bodies[colliders[i].bodyId].kind == KIND_PLAYER) playerSide = 0;
						else if (colliders[j].bodyId >= 0 && bodies[colliders[j].bodyId].kind == KIND_PLAYER) playerSide = 1;
						if (playerSide >= 0 && std::fabs(ct.normal.y) < 0.3f) {
							const Collider& player = (playerSide == 0) ? colliders[i] : colliders[j];
							const Collider& other = (playerSide == 0) ? colliders[j] : colliders[i];
							float playerFeet = player.position.y - player.halfExtents.y;
							// Conservative world-space top of the (possibly
							// rotated) other box.
							Matrix om = QuaternionToMatrix(other.orientation);
							float otherTopExtent =
								std::fabs(om.m1) * other.halfExtents.x +
								std::fabs(om.m5) * other.halfExtents.y +
								std::fabs(om.m9) * other.halfExtents.z;
							float otherTop = other.position.y + otherTopExtent;
							if (otherTop <= playerFeet + stepOffset) continue;
						}

						contacts.push_back(ct);
					}
				}

				// Dynamic vs terrain.
				if (terrainEnabled && terrainHeight) {
					collideTerrain(i, terrainIndex);
				}
			}

			// --- Prepare contact constraints ---
			for (Contact& ct : contacts) {
				Collider& A = colliders[ct.a];
				Collider& B = colliders[ct.b];
				ct.rA = sub(ct.point, A.position);
				ct.rB = sub(ct.point, B.position);
				ct.normalImpulse = 0.0f;

				// Effective mass along the normal.
				Vector3 rnA = cross(ct.rA, ct.normal);
				Vector3 rnB = cross(ct.rB, ct.normal);
				float invMassSum = A.invMass + B.invMass
					+ dot(ct.normal, cross(mat3Mul(A.invInertiaWorld, rnA), ct.rA))
					+ dot(ct.normal, cross(mat3Mul(B.invInertiaWorld, rnB), ct.rB));
				ct.normalMass = invMassSum > 0.0f ? 1.0f / invMassSum : 0.0f;

				// Two tangent directions orthogonal to the normal.
				Vector3 n = ct.normal;
				Vector3 t1 = std::fabs(n.x) > 0.57f ? Vector3{ n.y, -n.x, 0.0f } : Vector3{ 0.0f, n.z, -n.y };
				t1 = Vector3Normalize(t1);
				Vector3 t2 = cross(n, t1);
				ct.tangent1 = t1;
				ct.tangent2 = t2;

				auto tangentMass = [&](Vector3 tdir) {
					Vector3 rtA = cross(ct.rA, tdir);
					Vector3 rtB = cross(ct.rB, tdir);
					float s = A.invMass + B.invMass
						+ dot(tdir, cross(mat3Mul(A.invInertiaWorld, rtA), ct.rA))
						+ dot(tdir, cross(mat3Mul(B.invInertiaWorld, rtB), ct.rB));
					return s > 0.0f ? 1.0f / s : 0.0f;
				};
				ct.tangentMass1 = tangentMass(t1);
				ct.tangentMass2 = tangentMass(t2);

				// Relative normal velocity at the contact (for restitution).
				Vector3 relVel = sub(
					add(B.linearVelocity, cross(B.angularVelocity, ct.rB)),
					add(A.linearVelocity, cross(A.angularVelocity, ct.rA)));
				float vn = dot(relVel, ct.normal);

				float restitution = std::max(A.restitution, B.restitution);
				float restitutionBias = 0.0f;
				if (vn < -RESTITUTION_THRESHOLD) restitutionBias = -restitution * vn;

				bool involvesPlayer = (A.bodyId >= 0 && bodies[A.bodyId].kind == KIND_PLAYER)
					|| (B.bodyId >= 0 && bodies[B.bodyId].kind == KIND_PLAYER);
				float baumgarte = involvesPlayer ? PLAYER_BAUMGARTE : BAUMGARTE;
				float maxCorrection = involvesPlayer ? PLAYER_MAX_VELOCITY : (float)MAX_VELOCITY;

				float positionBias = (baumgarte / h) * std::max(ct.penetration - PENETRATION_SLOP, 0.0f);
				// Cap the correction speed: with a small substep h, even modest
				// penetration (e.g. clipping into a steep slope) can otherwise
				// produce a huge bias that the solver applies as real velocity,
				// launching the body instead of gently resolving the overlap.
				positionBias = std::min(positionBias, maxCorrection);
				ct.velocityBias = restitutionBias + positionBias;

				// Grounded / landing detection: a contact whose normal has a
				// strong upward component on the *body* side means the body
				// is being supported from below. ct.normal points from a to
				// b; for terrain (b = ground) it points downward, so the
				// support direction on body A is -normal.
				if (ct.a < dynamicCount || ct.b < dynamicCount) {
					Vector3 supportOnBody = ct.terrain ? scale(ct.normal, -1.0f) : ct.normal;
					// Determine which side is the dynamic body.
					int dynSide = (ct.a < dynamicCount) ? ct.a : ct.b;
					// If b is the dynamic body, the upward-support normal is
					// +normal (points from a into b = up into the body).
					if (dynSide == ct.b && !ct.terrain) supportOnBody = ct.normal;
					if (dynSide == ct.a && !ct.terrain) supportOnBody = scale(ct.normal, -1.0f);
					if (supportOnBody.y > 0.5f) {
						Body& body = bodies[colliders[dynSide].bodyId];
						if (!body.grounded) {
							body.grounded = true;
							// Downward speed of the body at the moment of support.
							float down = -colliders[dynSide].linearVelocity.y;
							body.landingImpactSpeed = std::max(body.landingImpactSpeed, down);
						}
					}
				}
			}

			// --- Solve velocity constraints ---
			for (int iter = 0; iter < VELOCITY_ITERATIONS; ++iter) {
				for (Contact& ct : contacts) {
					Collider& A = colliders[ct.a];
					Collider& B = colliders[ct.b];

					// Friction (applied against the *current* normal impulse).
					auto solveFriction = [&](Vector3 tdir, float tmass) {
						Vector3 relVel = sub(
							add(B.linearVelocity, cross(B.angularVelocity, ct.rB)),
							add(A.linearVelocity, cross(A.angularVelocity, ct.rA)));
						float vt = dot(relVel, tdir);
						float jt = -vt * tmass;
						float mu = std::sqrt(A.friction * B.friction);
						float maxF = mu * ct.normalImpulse;
						jt = std::max(-maxF, std::min(maxF, jt));
						Vector3 impulse = scale(tdir, jt);
						A.linearVelocity = sub(A.linearVelocity, scale(impulse, A.invMass));
						A.angularVelocity = sub(A.angularVelocity, mat3Mul(A.invInertiaWorld, cross(ct.rA, impulse)));
						B.linearVelocity = add(B.linearVelocity, scale(impulse, B.invMass));
						B.angularVelocity = add(B.angularVelocity, mat3Mul(B.invInertiaWorld, cross(ct.rB, impulse)));
					};

					// Normal impulse first.
					Vector3 relVel = sub(
						add(B.linearVelocity, cross(B.angularVelocity, ct.rB)),
						add(A.linearVelocity, cross(A.angularVelocity, ct.rA)));
					float vn = dot(relVel, ct.normal);
					float jn = (ct.velocityBias - vn) * ct.normalMass;
					float newImpulse = std::max(ct.normalImpulse + jn, 0.0f);
					jn = newImpulse - ct.normalImpulse;
					ct.normalImpulse = newImpulse;

					Vector3 nImpulse = scale(ct.normal, jn);
					A.linearVelocity = sub(A.linearVelocity, scale(nImpulse, A.invMass));
					A.angularVelocity = sub(A.angularVelocity, mat3Mul(A.invInertiaWorld, cross(ct.rA, nImpulse)));
					B.linearVelocity = add(B.linearVelocity, scale(nImpulse, B.invMass));
					B.angularVelocity = add(B.angularVelocity, mat3Mul(B.invInertiaWorld, cross(ct.rB, nImpulse)));

					solveFriction(ct.tangent1, ct.tangentMass1);
					solveFriction(ct.tangent2, ct.tangentMass2);
				}
			}

			// --- Write solved velocities back, integrate positions ---
			for (Collider& col : colliders) {
				if (col.bodyId < 0) continue;
				Body& b = bodies[col.bodyId];
				if (b.landlocked || b.invMass == 0.0f) continue;

				b.linearVelocity = col.linearVelocity;
				b.angularVelocity = b.lockRotation ? Vector3{ 0, 0, 0 } : col.angularVelocity;

				// Stand-still assist (see the flag's comment in the header):
				// zeroing horizontal velocity here - after the solve, before
				// position integration - is the only place that fully stops
				// slope drift, because this substep's gravity has already
				// been bent sideways by the contact normal by now.
				if (b.holdStillOnGround && b.grounded) {
					b.linearVelocity.x = 0.0f;
					b.linearVelocity.z = 0.0f;
				}

				b.position = add(b.position, scale(b.linearVelocity, h));

				if (!b.lockRotation) {
					// Quaternion integration: q += 0.5 * w * q * dt.
					Quaternion wq = { b.angularVelocity.x, b.angularVelocity.y, b.angularVelocity.z, 0.0f };
					Quaternion dq = QuaternionMultiply(wq, b.orientation);
					b.orientation.x += 0.5f * dq.x * h;
					b.orientation.y += 0.5f * dq.y * h;
					b.orientation.z += 0.5f * dq.z * h;
					b.orientation.w += 0.5f * dq.w * h;
					b.orientation = QuaternionNormalize(b.orientation);
				}
			}

			// --- Integrate ghost (noclip) bodies: no contacts, just move ---
			for (Body& b : bodies) {
				if (!b.active || !b.ghost || b.landlocked || !b.simulated) continue;
				b.position = add(b.position, scale(b.linearVelocity, h));
			}
		}
	}
}
