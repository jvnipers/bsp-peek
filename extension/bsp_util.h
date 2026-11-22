#ifndef _INCLUDE_BSPPEEK_BSP_UTIL_H_
#define _INCLUDE_BSPPEEK_BSP_UTIL_H_

#include "smsdk_ext.h"
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace BSPUtil
{

	constexpr float kNoHit = -1.0e30f;

	struct Vec3
	{
		float x, y, z;
	};

	inline int ReadI32(const uint8_t *p, int off)
	{
		int v;
		std::memcpy(&v, p + off, 4);
		return v;
	}

	inline float ReadF32(const uint8_t *p, int off)
	{
		float v;
		std::memcpy(&v, p + off, 4);
		return v;
	}

	inline void *ReadPtr(const uint8_t *p, int off)
	{
		uintptr_t v;
		std::memcpy(&v, p + off, sizeof(uintptr_t));
		return reinterpret_cast<void *>(v);
	}

	inline uint16_t ReadU16(const uint8_t *p, int off)
	{
		uint16_t v;
		std::memcpy(&v, p + off, 2);
		return v;
	}

	inline int16_t ReadI16(const uint8_t *p, int off)
	{
		int16_t v;
		std::memcpy(&v, p + off, 2);
		return v;
	}

	inline uint8_t ReadU8(const uint8_t *p, int off)
	{
		return p[off];
	}

	inline int GetKeyInt(IGameConfig *gc, const char *key, int defaultVal)
	{
		const char *s = gc->GetKeyValue(key);
		return (s && *s) ? std::atoi(s) : defaultVal;
	}

	// denom = twice the XY area. Small power-4 tris reach ~1, so only truly vertical ones are rejected.
	constexpr float kTriDegenerateDenom = 1e-3f;

	// Barycentric Z interpolation in XY plane.
	// Returns interpolated Z if (x,y) is inside triangle, else NaN.
	inline float TriHeightZXY(const Vec3 &a, const Vec3 &b, const Vec3 &c, float x, float y)
	{
		float denom = (b.y - c.y) * (a.x - c.x) + (c.x - b.x) * (a.y - c.y);
		if (std::fabs(denom) < kTriDegenerateDenom)
		{
			return NAN;
		}
		float wa = ((b.y - c.y) * (x - c.x) + (c.x - b.x) * (y - c.y)) / denom;
		float wb = ((c.y - a.y) * (x - c.x) + (a.x - c.x) * (y - c.y)) / denom;
		float wc = 1.0f - wa - wb;
		if (wa < -1e-4f || wb < -1e-4f || wc < -1e-4f)
		{
			return NAN;
		}
		return wa * a.z + wb * b.z + wc * c.z;
	}

	// Closest point on triangle (a,b,c) to p, written into q.
	// Ericson, Real-Time Collision Detection, ClosestPtPointTriangle.
	inline void ClosestPtPointTriangle(const Vec3 &p, const Vec3 &a, const Vec3 &b, const Vec3 &c, float q[3])
	{
		float ab[3] = {b.x - a.x, b.y - a.y, b.z - a.z};
		float ac[3] = {c.x - a.x, c.y - a.y, c.z - a.z};
		float ap[3] = {p.x - a.x, p.y - a.y, p.z - a.z};
		float d1 = ab[0] * ap[0] + ab[1] * ap[1] + ab[2] * ap[2];
		float d2 = ac[0] * ap[0] + ac[1] * ap[1] + ac[2] * ap[2];
		if (d1 <= 0.0f && d2 <= 0.0f)
		{
			q[0] = a.x;
			q[1] = a.y;
			q[2] = a.z;
			return;
		}
		float bp[3] = {p.x - b.x, p.y - b.y, p.z - b.z};
		float d3 = ab[0] * bp[0] + ab[1] * bp[1] + ab[2] * bp[2];
		float d4 = ac[0] * bp[0] + ac[1] * bp[1] + ac[2] * bp[2];
		if (d3 >= 0.0f && d4 <= d3)
		{
			q[0] = b.x;
			q[1] = b.y;
			q[2] = b.z;
			return;
		}
		float vc = d1 * d4 - d3 * d2;
		if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f)
		{
			float v = d1 / (d1 - d3);
			q[0] = a.x + v * ab[0];
			q[1] = a.y + v * ab[1];
			q[2] = a.z + v * ab[2];
			return;
		}
		float cp[3] = {p.x - c.x, p.y - c.y, p.z - c.z};
		float d5 = ab[0] * cp[0] + ab[1] * cp[1] + ab[2] * cp[2];
		float d6 = ac[0] * cp[0] + ac[1] * cp[1] + ac[2] * cp[2];
		if (d6 >= 0.0f && d5 <= d6)
		{
			q[0] = c.x;
			q[1] = c.y;
			q[2] = c.z;
			return;
		}
		float vb = d5 * d2 - d1 * d6;
		if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f)
		{
			float w = d2 / (d2 - d6);
			q[0] = a.x + w * ac[0];
			q[1] = a.y + w * ac[1];
			q[2] = a.z + w * ac[2];
			return;
		}
		float va = d3 * d6 - d5 * d4;
		if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f)
		{
			float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
			q[0] = b.x + w * (c.x - b.x);
			q[1] = b.y + w * (c.y - b.y);
			q[2] = b.z + w * (c.z - b.z);
			return;
		}
		float denom = 1.0f / (va + vb + vc);
		float v = vb * denom;
		float w = vc * denom;
		q[0] = a.x + ab[0] * v + ac[0] * w;
		q[1] = a.y + ab[1] * v + ac[1] * w;
		q[2] = a.z + ab[2] * v + ac[2] * w;
	}

	inline float DistSqToTri(const Vec3 &p, const Vec3 &a, const Vec3 &b, const Vec3 &c)
	{
		float q[3];
		ClosestPtPointTriangle(p, a, b, c, q);
		float dx = p.x - q[0], dy = p.y - q[1], dz = p.z - q[2];
		return dx * dx + dy * dy + dz * dz;
	}

} // namespace BSPUtil

#endif
