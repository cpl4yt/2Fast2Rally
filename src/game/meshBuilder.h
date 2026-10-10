#pragma once
#include <raylib.h>
#include <raymath.h>
#include <vector>
#include <cstring>

namespace rally {
	// Accumulates non-indexed triangles and uploads them as a raylib Mesh.
	// Non-indexed on purpose: raylib meshes use 16-bit indices, which
	// overflow on long road strips.
	struct MeshBuilder {
		std::vector<float> pos, nrm, uv;
		std::vector<unsigned char> col;

		size_t vertexCount() const { return pos.size() / 3; }
		bool empty() const { return pos.empty(); }
		void clear() { pos.clear(); nrm.clear(); uv.clear(); col.clear(); }

		void vertex(Vector3 p, Vector3 n, Vector2 t, Color c) {
			pos.insert(pos.end(), { p.x, p.y, p.z });
			nrm.insert(nrm.end(), { n.x, n.y, n.z });
			uv.insert(uv.end(), { t.x, t.y });
			col.insert(col.end(), { c.r, c.g, c.b, c.a });
		}

		// Triangle with a computed flat normal (counter-clockwise = front).
		void tri(Vector3 a, Vector3 b, Vector3 c, Color col, Vector2 ta = { 0, 0 }, Vector2 tb = { 1, 0 }, Vector2 tc = { 0, 1 }) {
			Vector3 n = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(b, a), Vector3Subtract(c, a)));
			vertex(a, n, ta, col); vertex(b, n, tb, col); vertex(c, n, tc, col);
		}
		// Quad a-b-c-d (counter-clockwise).
		void quad(Vector3 a, Vector3 b, Vector3 c, Vector3 d, Color col,
			Vector2 ta = { 0, 0 }, Vector2 tb = { 1, 0 }, Vector2 tc = { 1, 1 }, Vector2 td = { 0, 1 }) {
			tri(a, b, c, col, ta, tb, tc);
			tri(a, c, d, col, ta, tc, td);
		}

		// Oriented box: centre, half extents, rotation about Y.
		void box(Vector3 c, Vector3 h, Color col, float yaw = 0.0f) {
			Matrix m = MatrixMultiply(MatrixRotateY(yaw), MatrixTranslate(c.x, c.y, c.z));
			boxTransformed(h, m, col);
		}

		void boxTransformed(Vector3 h, const Matrix& m, Color col) {
			Vector3 v[8];
			for (int i = 0; i < 8; ++i) {
				Vector3 p = { (i & 1) ? h.x : -h.x, (i & 2) ? h.y : -h.y, (i & 4) ? h.z : -h.z };
				v[i] = Vector3Transform(p, m);
			}
			// faces: -x, +x, -y, +y, -z, +z
			quad(v[0], v[4], v[6], v[2], col);
			quad(v[1], v[3], v[7], v[5], col);
			quad(v[0], v[1], v[5], v[4], col);
			quad(v[2], v[6], v[7], v[3], col);
			quad(v[0], v[2], v[3], v[1], col);
			quad(v[4], v[5], v[7], v[6], col);
		}

		// Upright cylinder (or cone if r1 != r0) from base centre.
		void cylinder(Vector3 base, float r0, float r1, float height, int sides, Color col) {
			for (int i = 0; i < sides; ++i) {
				float a0 = 2 * PI * i / sides, a1 = 2 * PI * (i + 1) / sides;
				Vector3 b0 = { base.x + cosf(a0) * r0, base.y, base.z + sinf(a0) * r0 };
				Vector3 b1 = { base.x + cosf(a1) * r0, base.y, base.z + sinf(a1) * r0 };
				Vector3 t0 = { base.x + cosf(a0) * r1, base.y + height, base.z + sinf(a0) * r1 };
				Vector3 t1 = { base.x + cosf(a1) * r1, base.y + height, base.z + sinf(a1) * r1 };
				quad(b0, t0, t1, b1, col);
				Vector3 top = { base.x, base.y + height, base.z };
				if (r1 > 0.001f) tri(top, t1, t0, col);
			}
		}

		Mesh upload() const {
			Mesh m = { 0 };
			m.vertexCount = (int)vertexCount();
			m.triangleCount = m.vertexCount / 3;
			if (m.vertexCount == 0) return m;
			m.vertices = (float*)MemAlloc((unsigned)(pos.size() * sizeof(float)));
			m.normals = (float*)MemAlloc((unsigned)(nrm.size() * sizeof(float)));
			m.texcoords = (float*)MemAlloc((unsigned)(uv.size() * sizeof(float)));
			m.colors = (unsigned char*)MemAlloc((unsigned)col.size());
			std::memcpy(m.vertices, pos.data(), pos.size() * sizeof(float));
			std::memcpy(m.normals, nrm.data(), nrm.size() * sizeof(float));
			std::memcpy(m.texcoords, uv.data(), uv.size() * sizeof(float));
			std::memcpy(m.colors, col.data(), col.size());
			UploadMesh(&m, false);
			return m;
		}
	};
}
