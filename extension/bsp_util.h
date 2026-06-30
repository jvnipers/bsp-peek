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

	// Barycentric Z interpolation in XY plane.
	// Returns interpolated Z if (x,y) is inside triangle, else NaN.
	// Rejects near-vertical tris (|denom| < 2).
	inline float TriHeightZXY(const Vec3 &a, const Vec3 &b, const Vec3 &c, float x, float y)
	{
		float denom = (b.y - c.y) * (a.x - c.x) + (c.x - b.x) * (a.y - c.y);
		if (std::fabs(denom) < 2.0f)
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

} // namespace BSPUtil

#endif
