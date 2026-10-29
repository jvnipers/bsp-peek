#include "collide.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <unordered_map>

// MinSS/MaxSS and the compares keep the instructions' operand order and branch sense: they decide -0.0 and NaN.
namespace collide
{
	namespace
	{
		constexpr float NEVER_UPDATED = -99999.0f;

		inline float MinSS(float a, float b)
		{
			return (a < b) ? a : b;
		}

		inline float MaxSS(float a, float b)
		{
			return (a > b) ? a : b;
		}

		inline void SetSurface(Trace &tr, uint16_t index)
		{
			tr.surface = index;
			tr.worldSurfaceIndex = index;
		}
	} // namespace

	Ray MakeRay(const float start[3], const float end[3], const float mins[3], const float maxs[3])
	{
		Ray ray;
		for (int k = 0; k < 3; ++k)
		{
			ray.delta[k] = end[k] - start[k];
			ray.extents[k] = (maxs[k] - mins[k]) * 0.5f;
			ray.start[k] = start[k] + (mins[k] + maxs[k]) * 0.5f;
		}
		ray.isPoint = (ray.extents[1] * ray.extents[1] + ray.extents[0] * ray.extents[0]) + ray.extents[2] * ray.extents[2] < 0.000001f;
		for (int k = 0; k < 3; ++k)
		{
			ray.end[k] = ray.start[k] + ray.delta[k];
			ray.invDelta[k] = (ray.delta[k] == 0.0f) ? FLT_MAX : 1.0f / ray.delta[k];
		}
		return ray;
	}

	void ClearTrace(Trace &tr)
	{
		std::memset(&tr, 0, sizeof(tr));
		tr.fraction = 1.0f;
		tr.surface = NULL_SURFACE;
	}

	void ClipBoxToBrush(const Ray &ray, Trace &tr, const Side *sides, int numSides, int contents)
	{
		if (numSides == 0)
		{
			return;
		}

		float enterfrac = NEVER_UPDATED;
		float leavefrac = 1.0f;
		bool startout = false;
		bool getout = false;
		const Side *leadside = nullptr;

		for (const Side *side = sides; side < sides + numSides; ++side)
		{
			const float *n = side->plane->normal;
			float dist =
				std::fabs(ray.extents[2] * n[2]) + ((std::fabs(ray.extents[1] * n[1]) + std::fabs(ray.extents[0] * n[0])) + side->plane->dist);
			float d1 = (ray.start[2] * n[2] + (ray.start[0] * n[0] + ray.start[1] * n[1])) - dist;
			float d2 = ((n[0] * ray.end[0] + n[1] * ray.end[1]) + ray.end[2] * n[2]) - dist;

			if (d1 > 0.0f)
			{
				if (d2 > 0.0f)
				{
					return;
				}
				startout = true;
			}
			else
			{
				if (0.0f >= d2)
				{
					continue;
				}
				getout = true;
			}

			if (d1 > d2)
			{
				float f = MaxSS(d1 - DIST_EPSILON, 0.0f) / (d1 - d2);
				if (f > enterfrac)
				{
					leadside = side;
					enterfrac = f;
				}
			}
			else
			{
				// Double as in the engine. Same bits as float wherever f can matter.
				float f = (float)(((double)d1 + (double)DIST_EPSILON) / (double)(d1 - d2));
				leavefrac = MinSS(f, leavefrac);
			}
		}

		if (startout)
		{
			if (!(enterfrac > NEVER_UPDATED) || !(leavefrac > enterfrac) || enterfrac >= tr.fraction)
			{
				return;
			}
			tr.dispHit = false;
			tr.fraction = MaxSS(0.0f, enterfrac);
			tr.plane = *leadside->plane;
			SetSurface(tr, leadside->surfaceIndex);
			tr.contents = contents;
			return;
		}

		tr.startSolid = true;
		tr.contents = contents;
		if (!getout)
		{
			tr.allSolid = true;
			tr.fraction = 0.0f;
			tr.fractionLeftSolid = 1.0f;
			return;
		}
		// 1 means never updated. NaN takes the same jz.
		if (leavefrac == 1.0f || std::isnan(leavefrac) || !(leavefrac > tr.fractionLeftSolid))
		{
			return;
		}
		bool keepFraction = !(leavefrac >= tr.fraction);
		tr.fractionLeftSolid = leavefrac;
		if (!keepFraction)
		{
			tr.fraction = 1.0f;
			tr.surface = NULL_SURFACE;
		}
	}

	void ClipRayToBrush(const Ray &ray, Trace &tr, const Side *sides, int numSides, int contents)
	{
		if (numSides == 0)
		{
			return;
		}

		float enterfrac = NEVER_UPDATED;
		float leavefrac = 1.0f;
		bool startout = false;
		bool getout = false;
		const Side *leadside = nullptr;

		for (const Side *side = sides; side < sides + numSides; ++side)
		{
			if (side->bevel)
			{
				continue;
			}
			const float *n = side->plane->normal;
			float dist = side->plane->dist;
			float d1 = ray.start[2] * n[2] + (ray.start[0] * n[0] + ray.start[1] * n[1]) - dist;
			float d2 = n[2] * ray.end[2] + (n[1] * ray.end[1] + n[0] * ray.end[0]) - dist;

			if (d1 > 0.0f)
			{
				if (d2 > 0.0f)
				{
					return;
				}
				startout = true;
			}
			else
			{
				if (0.0f >= d2)
				{
					continue;
				}
				getout = true;
			}

			if (d1 > d2)
			{
				float f = MaxSS(d1 - DIST_EPSILON, 0.0f) / (d1 - d2);
				if (f > enterfrac)
				{
					leadside = side;
					enterfrac = f;
				}
			}
			else
			{
				float f = (float)(((double)d1 + (double)DIST_EPSILON) / (double)(d1 - d2));
				leavefrac = MinSS(leavefrac, f);
			}
		}

		// Entered after leaving an earlier brush: still outside.
		if (startout && !(tr.fractionLeftSolid - enterfrac > 0.0f))
		{
			if (!(enterfrac > NEVER_UPDATED) || !(leavefrac > enterfrac) || enterfrac >= tr.fraction)
			{
				return;
			}
			tr.dispHit = false;
			tr.fraction = MaxSS(enterfrac, 0.0f);
			tr.plane = *leadside->plane;
			SetSurface(tr, leadside->surfaceIndex);
			tr.contents = contents;
			return;
		}

		tr.startSolid = true;
		tr.contents = contents;
		if (!getout)
		{
			tr.allSolid = true;
			tr.fraction = 0.0f;
			tr.fractionLeftSolid = 1.0f;
			return;
		}
		if (leavefrac == 1.0f || std::isnan(leavefrac) || !(leavefrac > tr.fractionLeftSolid))
		{
			return;
		}
		bool keepFraction = !(leavefrac >= tr.fraction);
		tr.fractionLeftSolid = leavefrac;
		if (!keepFraction)
		{
			tr.fraction = 1.0f;
			tr.surface = NULL_SURFACE;
		}
	}

	bool ClipBoxToBoxBrush(const Ray &ray, Trace &tr, const float mins[3], const float maxs[3], const uint16_t surfaces[6], int contents)
	{
		float offMin[3], offMax[3];
		bool cross[3];
		bool startOut = false;
		for (int k = 0; k < 3; ++k)
		{
			offMin[k] = (mins[k] - ray.start[k]) - ray.extents[k];
			offMax[k] = (ray.extents[k] + maxs[k]) - ray.start[k];
			bool startOutMin = 0.0f < offMin[k];
			bool endOutMin = ray.delta[k] < offMin[k];
			bool startOutMax = offMax[k] < 0.0f;
			bool endOutMax = offMax[k] < ray.delta[k];
			if ((startOutMax && endOutMax) || (startOutMin && endOutMin))
			{
				return false;
			}
			cross[k] = (endOutMin != startOutMin) || (endOutMax != startOutMax);
			startOut = startOut || startOutMax || startOutMin;
		}

		// Without the epsilon: only the separation test uses it.
		{
			float mint[3], maxt[3];
			for (int k = 0; k < 3; ++k)
			{
				float tmin = ray.invDelta[k] * offMin[k];
				float tmax = ray.invDelta[k] * offMax[k];
				mint[k] = cross[k] ? MinSS(tmin, tmax) : -FLT_MAX;
				maxt[k] = cross[k] ? MaxSS(tmax, tmin) : FLT_MAX;
			}
			float firstOut = MinSS(MinSS(maxt[2], MinSS(maxt[1], maxt[0])), 1.0f);
			float lastIn = MaxSS(MaxSS(mint[2], MaxSS(mint[1], mint[0])), 0.0f);
			if (firstOut < lastIn)
			{
				return false;
			}
		}

		float mint[3], maxt[3];
		int faceId[3];
		for (int k = 0; k < 3; ++k)
		{
			float tmin = (offMin[k] + -DIST_EPSILON) * ray.invDelta[k];
			float tmax = (offMax[k] + DIST_EPSILON) * ray.invDelta[k];
			faceId[k] = (tmin <= tmax) ? k : k + 3;
			mint[k] = cross[k] ? MinSS(tmin, tmax) : -FLT_MAX;
			maxt[k] = cross[k] ? MaxSS(tmin, tmax) : FLT_MAX;
		}
		// Ties: y beats x, z beats both.
		float maxXY = MaxSS(mint[1], mint[0]);
		int faceXY = (mint[1] < mint[0]) ? faceId[0] : faceId[1];
		float lastIn = MaxSS(mint[2], maxXY);
		int face = (mint[2] < maxXY) ? faceXY : faceId[2];
		float firstOut = MinSS(MinSS(maxt[2], MinSS(maxt[1], maxt[0])), 1.0f);
		lastIn = MaxSS(0.0f, lastIn); // keeps -0.0
		if (firstOut < lastIn)
		{
			return false;
		}

		if (!startOut || (ray.isPoint && !(lastIn >= tr.fractionLeftSolid)))
		{
			tr.startSolid = true;
			tr.contents = contents;
			if (!(firstOut < 1.0f))
			{
				tr.allSolid = true;
				tr.fraction = 0.0f;
				return false;
			}
			if (!(firstOut > tr.fractionLeftSolid))
			{
				return false;
			}
			bool keepFraction = !(firstOut >= tr.fraction);
			tr.fractionLeftSolid = firstOut;
			if (!keepFraction)
			{
				tr.fraction = 1.0f;
				tr.surface = NULL_SURFACE;
			}
			return false;
		}

		if (lastIn >= tr.fraction)
		{
			return false;
		}
		tr.dispHit = false;
		tr.fraction = lastIn;
		tr.plane.normal[0] = tr.plane.normal[1] = tr.plane.normal[2] = 0.0f;
		SetSurface(tr, surfaces[face]);
		static const uint8_t signbits[3] = {1, 2, 4};
		if (face > 2)
		{
			face -= 3;
			tr.plane.dist = maxs[face];
			tr.plane.normal[face] = 1.0f;
			tr.plane.signbits = 0;
		}
		else
		{
			tr.plane.dist = -mins[face];
			tr.plane.normal[face] = -1.0f;
			tr.plane.signbits = signbits[face];
		}
		tr.plane.type = (uint8_t)face;
		tr.contents = contents;
		return true;
	}

	void BuildTraceList(const Bsp &bsp, const float boxMins[3], const float boxMaxs[3], TraceList &out)
	{
		out.leafs.clear();
		out.brushes.clear();
		out.disps.clear();
		out.foundNonSolidLeaf = false;
		float center[3], extents[3];
		for (int k = 0; k < 3; ++k)
		{
			out.mins[k] = boxMins[k] - 1.0f;
			out.maxs[k] = boxMaxs[k] + 1.0f;
			center[k] = (out.mins[k] + out.maxs[k]) * 0.5f;
			extents[k] = out.maxs[k] - center[k];
		}

		std::vector<uint8_t> brushSeen(bsp.numBrushes), dispSeen(bsp.numDispCounters);
		// The engine's ring buffer, no overflow check.
		int queue[1024];
		unsigned readIndex = 0, writeIndex = 0;
		int num = 0;
		for (;;)
		{
			while (num >= 0)
			{
				const Node &node = bsp.nodes[num];
				const float *n = node.plane->normal;
				float d = ((n[1] * center[1] + n[0] * center[0]) - node.plane->dist) + center[2] * n[2];
				float r = std::fabs(n[2] * extents[2]) + (std::fabs(n[1] * extents[1]) + std::fabs(n[0] * extents[0]));
				if (d >= r)
				{
					num = node.children[0];
					continue;
				}
				if (!(-r > d))
				{
					queue[writeIndex] = node.children[0];
					writeIndex = (writeIndex + 1) & 0x3FF;
				}
				num = node.children[1];
			}

			int leafIndex = -1 - num;
			const Leaf &leaf = bsp.leafs[leafIndex];
			out.leafs.push_back(leafIndex);
			if (!(leaf.contents & 1))
			{
				out.foundNonSolidLeaf = true;
			}
			unsigned brushEnd = (unsigned)leaf.firstLeafBrush + leaf.numLeafBrushes;
			for (unsigned i = leaf.firstLeafBrush; i < brushEnd; ++i)
			{
				uint16_t brush = bsp.leafBrushes[i];
				if (!brushSeen[brush])
				{
					brushSeen[brush] = 1;
					out.brushes.push_back(brush);
				}
			}
			for (unsigned i = 0; i < leaf.numLeafDisps; ++i)
			{
				uint16_t disp = bsp.leafDisps[leaf.firstLeafDisp + i];
				const DispBounds &b = bsp.disps[disp];
				// Seen before the overlap test, so a miss is never retried.
				if (dispSeen[b.counter])
				{
					continue;
				}
				dispSeen[b.counter] = 1;
				if (b.mins[0] <= out.maxs[0] && out.mins[0] <= b.maxs[0] && b.mins[1] <= out.maxs[1] && out.mins[1] <= b.maxs[1]
					&& b.mins[2] <= out.maxs[2] && b.maxs[2] >= out.mins[2])
				{
					out.disps.push_back(disp);
				}
			}

			if (readIndex == writeIndex)
			{
				break;
			}
			num = queue[readIndex];
			readIndex = (readIndex + 1) & 0x3FF;
		}
	}

	void MovementListBox(const float origin[3], const float velocity[3], float maxspeed, float frametime, float stepsize, float boxMins[3],
						 float boxMaxs[3])
	{
		static const float hullMins[3] = {-16.0f, -16.0f, 0.0f};
		static const float hullMaxs[3] = {16.0f, 16.0f, 72.0f};
		float speed = std::sqrt(velocity[2] * velocity[2] + (velocity[1] * velocity[1] + velocity[0] * velocity[0]));
		float radius = (speed + maxspeed) * frametime + 1.0f;
		float radiusZ = radius + stepsize;
		float hi[3], lo[3];
		for (int k = 0; k < 2; ++k)
		{
			hi[k] = (origin[k] + hullMaxs[k]) + radius;
			lo[k] = (hullMins[k] + origin[k]) - radius;
		}
		hi[2] = (origin[2] + hullMaxs[2]) + radiusZ;
		lo[2] = (hullMins[2] + origin[2]) - radiusZ;
		// ClearBounds, then AddPointToBounds of the max corner and the min corner (comiss: NaN lands in mins only).
		for (int k = 0; k < 3; ++k)
		{
			boxMins[k] = FLT_MAX;
			boxMaxs[k] = -FLT_MAX;
		}
		for (const float *p : {hi, lo})
		{
			for (int k = 0; k < 3; ++k)
			{
				if (!(p[k] >= boxMins[k]))
				{
					boxMins[k] = p[k];
				}
				if (p[k] > boxMaxs[k])
				{
					boxMaxs[k] = p[k];
				}
			}
		}
	}

	namespace
	{
		SeamOrder OrderOf(const TraceList &list, uint16_t lower, uint16_t upper)
		{
			int lowerAt = -1, upperAt = -1;
			for (int i = 0; i < (int)list.brushes.size(); ++i)
			{
				if (list.brushes[i] == lower && lowerAt < 0)
				{
					lowerAt = i;
				}
				if (list.brushes[i] == upper && upperAt < 0)
				{
					upperAt = i;
				}
			}
			if (lowerAt < 0 && upperAt < 0)
			{
				return SeamOrder::BothMissing;
			}
			if (lowerAt < 0)
			{
				return SeamOrder::LowerMissing;
			}
			if (upperAt < 0)
			{
				return SeamOrder::UpperMissing;
			}
			return lowerAt < upperAt ? SeamOrder::LowerFirst : SeamOrder::UpperFirst;
		}

		// Only planes the union box straddles can classify a smaller box differently.
		void StraddledPlanes(const Bsp &bsp, const float mins[3], const float maxs[3], std::vector<const Plane *> &out)
		{
			double c[3], e[3];
			for (int k = 0; k < 3; ++k)
			{
				c[k] = ((double)mins[k] + maxs[k]) * 0.5;
				e[k] = (double)maxs[k] - c[k] + 2.0; // grown by 1, plus rounding slack
			}
			std::vector<int> stack {0};
			while (!stack.empty())
			{
				int num = stack.back();
				stack.pop_back();
				if (num < 0)
				{
					continue;
				}
				const Node &node = bsp.nodes[num];
				const float *n = node.plane->normal;
				double d = n[0] * c[0] + n[1] * c[1] + n[2] * c[2] - node.plane->dist;
				double r = std::fabs(n[0]) * e[0] + std::fabs(n[1]) * e[1] + std::fabs(n[2]) * e[2];
				if (d >= r)
				{
					stack.push_back(node.children[0]);
				}
				else if (d < -r)
				{
					stack.push_back(node.children[1]);
				}
				else
				{
					out.push_back(node.plane);
					stack.push_back(node.children[0]);
					stack.push_back(node.children[1]);
				}
			}
		}
	} // namespace

	void SeamOrders(const Bsp &bsp, const SeamOrderQuery &q, std::vector<SeamOrderCell> &out)
	{
		out.clear();
		auto originAt = [&](double t, float o[3])
		{
			for (int k = 0; k < 3; ++k)
			{
				o[k] = (float)(q.origin[k] + t * q.along[k]);
			}
		};
		auto radiusOf = [&](double speed) { return (speed + q.maxspeed) * q.frametime + 1.0; };
		double rMin = radiusOf(0.0), rMax = radiusOf(q.speedMax);

		float unionMins[3], unionMaxs[3];
		{
			float o[3], mn[3], mx[3];
			const float v[3] = {q.speedMax, 0.0f, 0.0f};
			for (int end = 0; end < 2; ++end)
			{
				originAt(end ? q.alongMax : q.alongMin, o);
				MovementListBox(o, v, q.maxspeed, q.frametime, q.stepsize, mn, mx);
				for (int k = 0; k < 3; ++k)
				{
					unionMins[k] = end ? std::fmin(unionMins[k], mn[k]) : mn[k];
					unionMaxs[k] = end ? std::fmax(unionMaxs[k], mx[k]) : mx[k];
				}
			}
		}
		std::vector<const Plane *> planes;
		StraddledPlanes(bsp, unionMins, unionMaxs, planes);
		std::sort(planes.begin(), planes.end());
		planes.erase(std::unique(planes.begin(), planes.end()), planes.end());

		// Flips where |d0 + k t| = r0 + s r: grown list box centre origin + 36 z, half extents 17 + r (xy), 55 + r (z).
		struct Line
		{
			double d0, k, r0, s;
		};

		std::vector<Line> lines;
		for (const Plane *p : planes)
		{
			const float *n = p->normal;
			double d0 = n[0] * (double)q.origin[0] + n[1] * (double)q.origin[1] + n[2] * ((double)q.origin[2] + 36.0) - p->dist;
			double k = n[0] * (double)q.along[0] + n[1] * (double)q.along[1] + n[2] * (double)q.along[2];
			double s = std::fabs(n[0]) + std::fabs(n[1]) + std::fabs(n[2]);
			double r0 = 17.0 * (std::fabs(n[0]) + std::fabs(n[1])) + 55.0 * std::fabs(n[2]);
			lines.push_back({d0, k, r0, s});
		}
		auto rOf = [](const Line &l, double t) { return (std::fabs(l.d0 + l.k * t) - l.r0) / l.s; };

		// Where the cells can change along the wall: ends, apexes, rMin/rMax crossings, pairwise crossings.
		std::vector<double> ts {q.alongMin, q.alongMax};
		auto addT = [&](double t)
		{
			if (t > q.alongMin && t < q.alongMax)
			{
				ts.push_back(t);
			}
		};
		for (const Line &l : lines)
		{
			if (l.k == 0.0)
			{
				continue;
			}
			addT(-l.d0 / l.k);
			for (double r : {rMin, rMax})
			{
				for (double sign : {-1.0, 1.0})
				{
					addT((sign * (l.r0 + r * l.s) - l.d0) / l.k);
				}
			}
		}
		for (size_t i = 0; i < lines.size(); ++i)
		{
			for (size_t j = i + 1; j < lines.size(); ++j)
			{
				const Line &a = lines[i], &b = lines[j];
				for (double sa : {-1.0, 1.0})
				{
					for (double sb : {-1.0, 1.0})
					{
						double ka = sa * a.k / a.s, kb = sb * b.k / b.s;
						if (ka == kb)
						{
							continue;
						}
						double ca = (sa * a.d0 - a.r0) / a.s, cb = (sb * b.d0 - b.r0) / b.s;
						double t = (cb - ca) / (ka - kb);
						double r = ca + ka * t;
						if (r > rMin && r < rMax)
						{
							addT(t);
						}
					}
				}
			}
		}
		std::sort(ts.begin(), ts.end());
		ts.erase(std::unique(ts.begin(), ts.end()), ts.end());
		std::vector<double> samples;
		for (size_t i = 0; i < ts.size(); ++i)
		{
			samples.push_back(ts[i]);
			if (i + 1 < ts.size())
			{
				samples.push_back((ts[i] + ts[i + 1]) * 0.5);
			}
		}

		// One walk per plane-class state. Along a column one plane changes class at each crossing.
		double magnitude = 0.0;
		for (int k = 0; k < 3; ++k)
		{
			magnitude = std::fmax(magnitude, std::fmax(std::fabs(unionMins[k]), std::fabs(unionMaxs[k])));
		}
		// Clear of float rounding at these coordinates.
		const double delta = std::fmax(1e-3, magnitude * std::ldexp(1.0, -20));
		auto mix = [](uint64_t x)
		{
			x += 0x9E3779B97F4A7C15ull;
			x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
			x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
			return x ^ (x >> 31);
		};
		auto classOf = [](const Line &l, double t, double r)
		{
			double d = l.d0 + l.k * t, R = l.r0 + l.s * r;
			return std::fabs(d) < R ? 2 : (d > 0.0 ? 0 : 1);
		};

		std::unordered_map<uint64_t, SeamOrder> orders;
		TraceList list;
		std::vector<std::pair<double, size_t>> events;
		for (double t : samples)
		{
			float o[3];
			originAt(t, o);
			auto orderAt = [&](uint64_t state, double r)
			{
				auto it = orders.find(state);
				if (it != orders.end())
				{
					return it->second;
				}
				double speed = std::fmax(0.0, (r - 1.0) / q.frametime - q.maxspeed);
				const float v[3] = {(float)speed, 0.0f, 0.0f};
				float mn[3], mx[3];
				MovementListBox(o, v, q.maxspeed, q.frametime, q.stepsize, mn, mx);
				BuildTraceList(bsp, mn, mx, list);
				SeamOrder order = OrderOf(list, q.lower, q.upper);
				orders.emplace(state, order);
				return order;
			};
			auto emit = [&](double r0, double r1, SeamOrder order)
			{
				float s0 = (float)std::fmax(0.0, (r0 - 1.0) / q.frametime - q.maxspeed);
				float s1 = (float)std::fmax(0.0, (r1 - 1.0) / q.frametime - q.maxspeed);
				if (!out.empty() && out.back().along == (float)t && out.back().order == order)
				{
					out.back().speedMax = s1;
				}
				else
				{
					out.push_back({(float)t, s0, s1, order});
				}
			};

			uint64_t state = 0;
			events.clear();
			for (size_t i = 0; i < lines.size(); ++i)
			{
				int c = classOf(lines[i], t, rMin + delta);
				state ^= mix(i * 3 + c);
				double r = rOf(lines[i], t);
				if (c != 2 && r < rMax)
				{
					events.push_back({r, i});
				}
			}
			std::sort(events.begin(), events.end());
			double start = rMin;
			SeamOrder order = orderAt(state, rMin + delta);
			for (size_t j = 0; j < events.size(); ++j)
			{
				double r = events[j].first;
				size_t i = events[j].second;
				state ^= mix(i * 3 + classOf(lines[i], t, r - delta)) ^ mix(i * 3 + 2);
				double next = j + 1 < events.size() ? events[j + 1].first : rMax;
				if (next - r <= 2 * delta)
				{
					continue; // narrower than float resolution
				}
				SeamOrder now = orderAt(state, std::fmin(r + delta, (r + next) * 0.5));
				if (now != order)
				{
					emit(start, r, order);
					start = r;
					order = now;
				}
			}
			emit(start, rMax, order);
		}
	}
} // namespace collide
