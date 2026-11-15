#include "bsp_seam.h"
#include "bsp_data.h"
#include "bsp_disp.h"
#include "bsp_props.h"
#include "collide.h"

#include "bspflags.h"
#include "const.h"
#include "mathlib/mathlib.h"
#include "engine/ICollideable.h"
#include "engine/IEngineTrace.h"
#include "engine/IStaticPropMgr.h"
#include "gametrace.h"
#include "iserverunknown.h"
#include "tier1/utlvector.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <map>
#include <tuple>
#include <vector>

namespace BSPSeam
{
	namespace
	{
		// CTraceFilterSimple with COLLISION_GROUP_PLAYER_MOVEMENT, minus players.
		// Groups: CCSGameRules::ShouldCollide and its base in CS:GO server.dll (CCSGameRules vtable slot 28).
		class MovementFilter : public ITraceFilter
		{
		public:
			bool ShouldHitEntity(IHandleEntity *ent, int) override
			{
				if (BSPProps::StaticPropMgr() && BSPProps::StaticPropMgr()->IsStaticProp(ent))
				{
					return true;
				}
				ICollideable *c = static_cast<IServerUnknown *>(ent)->GetCollideable();
				if (!c || c->GetSolid() == SOLID_NONE || (c->GetSolidFlags() & FSOLID_NOT_SOLID))
				{
					return false;
				}
				switch (c->GetCollisionGroup())
				{
					case COLLISION_GROUP_DEBRIS:
					case COLLISION_GROUP_DEBRIS_TRIGGER:
					case COLLISION_GROUP_INTERACTIVE_DEBRIS:
					case COLLISION_GROUP_PLAYER:
					case COLLISION_GROUP_IN_VEHICLE:
					case COLLISION_GROUP_WEAPON:
					case COLLISION_GROUP_PROJECTILE:
					case COLLISION_GROUP_DOOR_BLOCKER:
					case COLLISION_GROUP_DISSOLVING:
					case COLLISION_GROUP_PUSHAWAY:
					case COLLISION_GROUP_VEHICLE_CLIP:
					case COLLISION_GROUP_DEBRIS_BLOCK_PROJECTILE:
						return false;
					default:
						return true;
				}
			}

			TraceType_t GetTraceType() const override
			{
				return TRACE_EVERYTHING;
			}
		};

		// A box hovers only at the top of the window at speed.
		const float kCatchGaps[] = {0.0002f, 0.002f, 0.008f, 0.016f, 0.024f, 0.030f, 0.03125f, 0.0318f};
		const float kCatchHeights[] = {0.03125f, 0.03025f, 0.015625f};
		const float kCatchSpeeds[] = {2.0f, 20.0f, 100.0f, 300.0f, 700.0f, 1500.0f, 3000.0f};
		const float kCatchIn[] = {5.0f, 60.0f, -20.0f}; // away: a hull pushed off a tilted face
		const float kSpeedMax = 6062.0f;                // sqrt(3) * sv_maxvelocity
		// Texturebug grid: tops as shares of the tick's drop, pushes as shares of the fall speed (window gap / top to eps / (H_brush + H_hull)).
		const float kTbSpeeds[] = {150.0f, 400.0f, 1000.0f, 2500.0f};
		const float kTbTops[] = {0.25f, 0.5f, 0.75f};
		const float kTbPushes[] = {1e-6f, 3e-6f, 1e-5f, 3e-5f, 1e-4f, 2e-4f, 4e-4f};

		struct MoveTrace
		{
			ITraceListData *list;
			MovementFilter filter;
			Vector mins, maxs;

			void Run(const Vector &from, const Vector &to, CGameTrace &tr)
			{
				Ray_t ray;
				ray.Init(from, to, mins, maxs);
				tr.fraction = 1.0f;
				tr.startsolid = false;
				tr.allsolid = false;
				if (list->CanTraceRay(ray))
				{
					BSPProps::EngineTrace()->TraceRayAgainstLeafAndEntityList(ray, list, MASK_PLAYERSOLID, &filter, &tr);
				}
				else
				{
					BSPProps::EngineTrace()->TraceRay(ray, MASK_PLAYERSOLID, &filter, &tr);
				}
			}
		};

		bool Caught(MoveTrace &mt, const Vector &start, const float normal[3], float vin, float vz, float frametime, float gravity,
					float airaccelerate, float maxspeed, bool &wallNonAxial)
		{
			float vel[3] = {-normal[0] * vin, -normal[1] * vin, vz};
			float origin[3] = {start.x, start.y, start.z}, mn[3], mx[3];
			collide::MovementListBox(origin, vel, maxspeed, frametime, 18.0f, mn, mx);
			BSPProps::EngineTrace()->SetupLeafAndEntityListBox(Vector(mn[0], mn[1], mn[2]), Vector(mx[0], mx[1], mx[2]), mt.list);

			float add = (std::min)((std::max)(30.0f - vin, 0.0f), airaccelerate * maxspeed * frametime);
			Vector v(vel[0] - normal[0] * add, vel[1] - normal[1] * add, vz - gravity * 0.5f * frametime);
			CGameTrace tr;
			mt.Run(start, start + v * frametime, tr);
			if (tr.startsolid || tr.fraction >= 0.0001f || tr.plane.normal.z <= 0.7f)
			{
				return false;
			}
			// The next bump can carry the hull on to a lip, and a face leaning out drops it.
			v.z = 0.0f;
			mt.Run(start, start + v * frametime, tr);
			if (tr.startsolid || (tr.fraction < 1.0f && tr.plane.normal.z < -0.001f))
			{
				return false;
			}
			wallNonAxial = tr.fraction < 0.0001f && tr.plane.type >= PLANE_Z;
			Vector end = tr.endpos;
			mt.Run(end, end - Vector(0.0f, 0.0f, 2.0f), tr);
			return tr.startsolid || tr.fraction >= 1.0f || tr.plane.normal.z < 0.7f;
		}

		// A start whose straight rise meets a ceiling (an overhang) sets underCeiling instead.
		bool CaughtHead(MoveTrace &mt, const Vector &start, const float normal[3], float vin, float vz, float frametime, float gravity,
						float airaccelerate, float maxspeed, bool &wallNonAxial, bool &underCeiling)
		{
			float vel[3] = {-normal[0] * vin, -normal[1] * vin, vz};
			float origin[3] = {start.x, start.y, start.z}, mn[3], mx[3];
			collide::MovementListBox(origin, vel, maxspeed, frametime, 18.0f, mn, mx);
			BSPProps::EngineTrace()->SetupLeafAndEntityListBox(Vector(mn[0], mn[1], mn[2]), Vector(mx[0], mx[1], mx[2]), mt.list);

			// CategorizePosition leaves friction 0.25 after a rise of 140 u/s or less; the last tick moved at vz + halfG.
			float halfG = gravity * 0.5f * frametime;
			float friction = (vz + halfG > 0.0f && vz + halfG <= 140.0f) ? 0.25f : 1.0f;
			float add = (std::min)((std::max)(30.0f - vin, 0.0f), airaccelerate * maxspeed * frametime * friction);
			Vector v(vel[0] - normal[0] * add, vel[1] - normal[1] * add, vz - halfG);
			CGameTrace tr;
			mt.Run(start, start + v * frametime, tr);
			if (tr.startsolid || tr.fraction >= 0.0001f || tr.plane.normal.z >= -0.7f)
			{
				return false;
			}
			v.z = 0.0f;
			mt.Run(start, start + v * frametime, tr);
			if (tr.startsolid || (tr.fraction < 1.0f && tr.plane.normal.z > 0.001f))
			{
				return false;
			}
			wallNonAxial = tr.fraction < 0.0001f && tr.plane.type >= PLANE_Z;
			// From the start: the second bump can leave an overhang.
			mt.Run(start, start + Vector(0.0f, 0.0f, 2.0f), tr);
			if (tr.startsolid || tr.fraction >= 1.0f || tr.plane.normal.z > -0.7f)
			{
				return true;
			}
			// Only a flat one: under a slope the head hit depends on the speed along it and where the hull is.
			// And only one at this height, within ε: a higher one is listed at its own, its bottom being a candidate too.
			if (tr.plane.normal.z <= -0.99999f && tr.fraction * 2.0f <= 0.03125f)
			{
				underCeiling = true;
			}
			return false;
		}

		// A vcollide's box stands 0.25 off its surface.
		void AddBounds(ICollideable *c, std::vector<float> &zs)
		{
			float z = c->GetCollisionOrigin().z, hi = z + c->OBBMaxs().z, lo = z + c->OBBMins().z;
			zs.insert(zs.end(), {hi, hi - 0.25f, lo, lo + 0.25f});
		}

		class SolidBounds : public IEntityEnumerator
		{
		public:
			explicit SolidBounds(std::vector<float> &zs) : m_zs(zs) {}

			bool EnumEntity(IHandleEntity *ent) override
			{
				ICollideable *c = BSPProps::EngineTrace()->GetCollideable(ent);
				if (c && c->GetSolid() != SOLID_NONE && !(c->GetSolidFlags() & FSOLID_NOT_SOLID))
				{
					AddBounds(c, m_zs);
				}
				return true;
			}

		private:
			std::vector<float> &m_zs;
		};
	} // namespace

	int Catch(const float wallPos[3], const float normal[3], float seamZ, float frametime, float gravity, float airaccelerate, float maxspeed,
			  float outSpeed[2], float outStart[6], bool head)
	{
		std::fill(outStart, outStart + 6, 0.0f);
		outSpeed[0] = 0.0f;
		outSpeed[1] = kSpeedMax;
		if (!BSPProps::EngineTrace())
		{
			return -1;
		}
		ITraceListData *list = BSPProps::EngineTrace()->AllocTraceListData();
		if (!list)
		{
			return -1;
		}
		int flags = 0;
		bool underCeiling = false;
		bool speedHit[std::size(kCatchSpeeds)] = {};
		float reach = 16.0f * (std::fabs(normal[0]) + std::fabs(normal[1]));
		MoveTrace mt;
		mt.list = list;
		mt.mins.Init(-16.0f, -16.0f, 0.0f);
		for (int duck = 0; duck < 2; ++duck)
		{
			mt.maxs.Init(16.0f, 16.0f, duck ? 54.0f : 72.0f);
			for (float dz : kCatchHeights)
			{
				float z = head ? seamZ - mt.maxs.z - dz : seamZ + dz;
				// A side wall can fill the far start.
				CGameTrace hit;
				bool found = false;
				for (float out : {48.0f, reach + 2.0f})
				{
					Vector from(wallPos[0] + normal[0] * out, wallPos[1] + normal[1] * out, z);
					Vector to(wallPos[0] - normal[0] * 8.0f, wallPos[1] - normal[1] * 8.0f, z);
					Ray_t ray;
					ray.Init(from, to, mt.mins, mt.maxs);
					BSPProps::EngineTrace()->TraceRay(ray, MASK_PLAYERSOLID, &mt.filter, &hit);
					if (!hit.startsolid && hit.fraction < 1.0f && std::fabs(hit.plane.normal.z) <= 0.7f)
					{
						found = true;
						break;
					}
				}
				if (!found)
				{
					continue;
				}
				for (float gap : kCatchGaps)
				{
					float into = 0.03125f - gap;
					Vector start(hit.endpos.x - normal[0] * into, hit.endpos.y - normal[1] * into, z);
					CGameTrace at;
					Ray_t ray;
					ray.Init(start, start, mt.mins, mt.maxs);
					BSPProps::EngineTrace()->TraceRay(ray, MASK_PLAYERSOLID, &mt.filter, &at);
					if (at.startsolid)
					{
						continue;
					}
					for (size_t s = 0; s < std::size(kCatchSpeeds); ++s)
					{
						if (speedHit[s] && (flags & (duck ? CATCH_DUCKED : CATCH_STANDING)))
						{
							continue;
						}
						for (float vin : kCatchIn)
						{
							bool nonAxial = false;
							if (head ? CaughtHead(mt, start, normal, vin, kCatchSpeeds[s], frametime, gravity, airaccelerate, maxspeed, nonAxial,
												  underCeiling)
									 : Caught(mt, start, normal, vin, -kCatchSpeeds[s], frametime, gravity, airaccelerate, maxspeed, nonAxial))
							{
								if (!flags)
								{
									outStart[0] = start.x;
									outStart[1] = start.y;
									outStart[2] = start.z;
									outStart[3] = -normal[0] * vin;
									outStart[4] = -normal[1] * vin;
									outStart[5] = head ? kCatchSpeeds[s] : -kCatchSpeeds[s];
								}
								speedHit[s] = true;
								flags |= (duck ? CATCH_DUCKED : CATCH_STANDING) | (nonAxial ? CATCH_WALL_NON_AXIAL : 0);
								break;
							}
						}
					}
				}
			}
		}
		BSPProps::EngineTrace()->FreeTraceListData(list);
		if (!flags)
		{
			return underCeiling ? CATCH_UNDER_CEILING : 0;
		}
		// To the next grid speed with no catch.
		size_t first = 0, last = std::size(kCatchSpeeds) - 1;
		while (!speedHit[first])
		{
			++first;
		}
		while (!speedHit[last])
		{
			--last;
		}
		outSpeed[0] = first == 0 ? 0.0f : kCatchSpeeds[first - 1];
		outSpeed[1] = last + 1 == std::size(kCatchSpeeds) ? kSpeedMax : kCatchSpeeds[last + 1];
		return flags | (underCeiling ? CATCH_UNDER_CEILING : 0);
	}

	int TexturebugCatch(const float wallPos[3], const float normal[3], float edgeZ, float frametime, float gravity, float maxspeed, float outInfo[2])
	{
		outInfo[0] = 0.0f;
		outInfo[1] = 0.0f;
		if (!BSPProps::EngineTrace())
		{
			return -1;
		}
		int axis = std::fabs(normal[0]) > 0.999f ? 0 : (std::fabs(normal[1]) > 0.999f ? 1 : -1);
		if (axis < 0)
		{
			return 0; // box brushes are axial
		}
		float sign = normal[axis] > 0.0f ? 1.0f : -1.0f;
		ITraceListData *list = BSPProps::EngineTrace()->AllocTraceListData();
		if (!list)
		{
			return -1;
		}
		int flags = 0;
		float halfG = gravity * 0.5f * frametime;
		MoveTrace mt;
		mt.list = list;
		mt.mins.Init(-16.0f, -16.0f, 0.0f);
		for (int duck = 0; duck < 2; ++duck)
		{
			mt.maxs.Init(16.0f, 16.0f, duck ? 54.0f : 72.0f);
			bool caught = false;
			for (float vz : kTbSpeeds)
			{
				float vm = vz + halfG;
				for (float top : kTbTops)
				{
					float z = edgeZ - mt.maxs.z + top * vm * frametime;
					CGameTrace hit;
					bool found = false;
					for (float out : {48.0f, 18.0f})
					{
						Vector from(wallPos[0] + normal[0] * out, wallPos[1] + normal[1] * out, z);
						Vector to(wallPos[0] - normal[0] * 8.0f, wallPos[1] - normal[1] * 8.0f, z);
						Ray_t ray;
						ray.Init(from, to, mt.mins, mt.maxs);
						BSPProps::EngineTrace()->TraceRay(ray, MASK_PLAYERSOLID, &mt.filter, &hit);
						if (!hit.startsolid && hit.fraction < 1.0f && hit.plane.normal[axis] * sign > 0.999f)
						{
							found = true;
							break;
						}
					}
					if (!found)
					{
						continue;
					}
					// One float step off, the closest a hug gets; at 0 the clip takes the hull as inside.
					float face = hit.plane.dist * sign;
					Vector start = hit.endpos;
					start[axis] = std::nextafter(face + 16.0f * sign, face + 64.0f * sign);
					CGameTrace at;
					Ray_t still;
					still.Init(start, start, mt.mins, mt.maxs);
					BSPProps::EngineTrace()->TraceRay(still, MASK_PLAYERSOLID, &mt.filter, &at);
					if (at.startsolid)
					{
						continue;
					}
					float gap = (start[axis] - face) * sign - 16.0f;
					for (float k : kTbPushes)
					{
						float vel[3] = {-normal[0] * k * vm, -normal[1] * k * vm, -vm};
						float origin[3] = {start.x, start.y, start.z}, mn[3], mx[3];
						collide::MovementListBox(origin, vel, maxspeed, frametime, 18.0f, mn, mx);
						BSPProps::EngineTrace()->SetupLeafAndEntityListBox(Vector(mn[0], mn[1], mn[2]), Vector(mx[0], mx[1], mx[2]), mt.list);
						CGameTrace tr;
						mt.Run(start, start + Vector(vel[0], vel[1], vel[2]) * frametime, tr);
						// A floor at fraction 0 above the feet is the box's top.
						if (!tr.startsolid && tr.fraction < 0.0001f && tr.plane.normal.z > 0.7f && tr.plane.dist > start.z + 1.0f)
						{
							if (!flags)
							{
								outInfo[0] = tr.plane.dist - edgeZ;
								outInfo[1] = gap;
							}
							caught = true;
							break;
						}
					}
					if (caught)
					{
						break;
					}
				}
				if (caught)
				{
					break;
				}
			}
			if (caught)
			{
				flags |= duck ? CATCH_DUCKED : CATCH_STANDING;
			}
		}
		BSPProps::EngineTrace()->FreeTraceListData(list);
		return flags;
	}

	int Candidates(const float wallPos[3], const float normal[3], float zMin, float zMax, float *out, int maxOut)
	{
		// Past the map the 1-unit scan below runs for ever (z + 1 == z from 2^24).
		zMin = (std::max)(zMin, -32768.0f);
		zMax = (std::min)(zMax, 32768.0f);
		std::vector<float> zs;
		float along[2] = {-normal[1], normal[0]};
		float mn[3] = {wallPos[0] - 24.0f, wallPos[1] - 24.0f, zMin};
		float mx[3] = {wallPos[0] + 24.0f, wallPos[1] + 24.0f, zMax};
		if (BSPData::CollideBsp())
		{
			// Not the world tree: a brush entity's brushes are in its own.
			int count = BSPData::GetNumBrushes();
			for (int b = 0; b < count; ++b)
			{
				float bmn[3], bmx[3];
				if (!BSPData::GetBrushBounds(b, bmn, bmx) || bmn[0] > mx[0] || bmx[0] < mn[0] || bmn[1] > mx[1] || bmx[1] < mn[1] || bmn[2] > mx[2]
					|| bmx[2] < mn[2] || !(BSPData::GetBrushContents(b) & MASK_PLAYERSOLID))
				{
					continue;
				}
				for (float off : {0.0f, -8.0f, 8.0f, -15.0f, 15.0f})
				{
					float top, bottom;
					float x = wallPos[0] - normal[0] * 0.25f + along[0] * off;
					float y = wallPos[1] - normal[1] * 0.25f + along[1] * off;
					if (BSPData::BrushColumnSpan(b, x, y, bottom, top))
					{
						zs.push_back(top);
						zs.push_back(bottom);
					}
				}
			}
			BSPDisp::EngineVertexZsInBox(mn, mx, zs);
		}

		if (BSPProps::EngineTrace())
		{
			Vector boxMin(mn[0], mn[1], mn[2]), boxMax(mx[0], mx[1], mx[2]);
			if (BSPProps::StaticPropMgr())
			{
				CUtlVector<ICollideable *> props;
				BSPProps::StaticPropMgr()->GetAllStaticPropsInAABB(boxMin, boxMax, &props);
				for (int i = 0; i < props.Count(); ++i)
				{
					AddBounds(props[i], zs);
				}
			}
			SolidBounds entities(zs);
			BSPProps::EngineTrace()->EnumerateEntities(boxMin, boxMax, &entities);

			MovementFilter filter;
			Vector from(wallPos[0] + normal[0] * 2.0f, wallPos[1] + normal[1] * 2.0f, 0.0f);
			Vector to(wallPos[0] - normal[0] * 2.0f, wallPos[1] - normal[1] * 2.0f, 0.0f);

			struct Sample
			{
				bool hit;
				const void *ent;
				const char *surface;
				float depth;
			};

			auto probe = [&](float z)
			{
				from.z = to.z = z;
				Ray_t ray;
				ray.Init(from, to);
				CGameTrace tr;
				tr.fraction = 1.0f;
				BSPProps::EngineTrace()->TraceRay(ray, MASK_PLAYERSOLID, &filter, &tr);
				return Sample {tr.fraction < 1.0f, tr.m_pEnt, tr.surface.name, tr.endpos.x * normal[0] + tr.endpos.y * normal[1]};
			};
			auto same = [](const Sample &a, const Sample &b)
			{
				return a.hit == b.hit && a.ent == b.ent && (a.surface == b.surface || (a.surface && b.surface && !std::strcmp(a.surface, b.surface)))
					   && std::fabs(a.depth - b.depth) < 0.01f;
			};
			Sample last = probe(zMin);
			for (float z = zMin + 1.0f; z <= zMax; z += 1.0f)
			{
				Sample now = probe(z);
				if (!same(last, now))
				{
					float lo = z - 1.0f, hi = z;
					Sample low = last;
					for (int i = 0; i < 12; ++i)
					{
						float mid = (lo + hi) * 0.5f;
						if (same(low, probe(mid)))
						{
							lo = mid;
						}
						else
						{
							hi = mid;
						}
					}
					zs.push_back((lo + hi) * 0.5f);
				}
				last = now;
			}
		}

		std::sort(zs.begin(), zs.end());
		int n = 0;
		for (float z : zs)
		{
			if (z < zMin || z > zMax || (n > 0 && z - out[n - 1] < 0.01f))
			{
				continue;
			}
			if (n == maxOut)
			{
				break;
			}
			out[n++] = z;
		}
		return n;
	}

	namespace
	{
		std::vector<float> s_edgePoints;
		float s_edgeSpacing = 0.0f;

		class NothingFilter : public ITraceFilter
		{
		public:
			bool ShouldHitEntity(IHandleEntity *, int) override
			{
				return false;
			}

			TraceType_t GetTraceType() const override
			{
				return TRACE_WORLD_ONLY;
			}
		};

		// Flush: plane within 1/4u, facing the same way. World first: displacements are world, props and entities need the movement filter.
		int Continuation(IEngineTrace *et, const float p[3], const float fn[2])
		{
			Vector from(p[0] + fn[0], p[1] + fn[1], p[2]), to(p[0] - fn[0], p[1] - fn[1], p[2]);
			Ray_t ray;
			ray.Init(from, to);
			auto flush = [&](const CGameTrace &tr)
			{
				float depth = (tr.endpos.x - p[0]) * fn[0] + (tr.endpos.y - p[1]) * fn[1];
				return tr.fraction < 1.0f && !tr.startsolid && tr.plane.normal.x * fn[0] + tr.plane.normal.y * fn[1] > 0.9f
					   && std::fabs(depth) < 0.25f;
			};
			NothingFilter world;
			CGameTrace tr;
			et->TraceRay(ray, MASK_PLAYERSOLID, &world, &tr);
			if (flush(tr))
			{
				return EDGE_CONTINUES | (tr.dispFlags ? EDGE_NONWORLD : 0);
			}
			MovementFilter movement;
			CGameTrace te;
			et->TraceRay(ray, MASK_PLAYERSOLID, &movement, &te);
			return flush(te) ? (EDGE_CONTINUES | EDGE_NONWORLD) : 0;
		}

		struct WallEdge
		{
			float a[2], c[2], z, fn[2];
			bool top;
		};

		// Faces on one plane share it: flat normal to 1/1000, distance to 1/64u.
		std::tuple<int, int, int> PlaneKey(const WallEdge &e)
		{
			float d = e.fn[0] * e.a[0] + e.fn[1] * e.a[1];
			return std::make_tuple((int)std::lround(e.fn[0] * 1000.0f), (int)std::lround(e.fn[1] * 1000.0f), (int)std::lround(d * 64.0f));
		}

		// Samples e between s0 and s1 units along it from a.
		void SampleSpan(IEngineTrace *et, const WallEdge &e, float s0, float s1, float spacing, int edge)
		{
			float span = std::sqrt((e.c[0] - e.a[0]) * (e.c[0] - e.a[0]) + (e.c[1] - e.a[1]) * (e.c[1] - e.a[1]));
			float dir[2] = {(e.c[0] - e.a[0]) / span, (e.c[1] - e.a[1]) / span};
			float past = e.top ? 1.0f : -1.0f;
			int samples = (std::max)(1, (int)((s1 - s0) / spacing));
			for (int k = 0; k < samples; ++k)
			{
				float at = s0 + (s1 - s0) * (k + 0.5f) / samples;
				float p[3] = {e.a[0] + dir[0] * at, e.a[1] + dir[1] * at, e.z + past};
				// Solid in front, just past the edge: a floor or ceiling meets the face here.
				Vector front(p[0] + e.fn[0], p[1] + e.fn[1], p[2]);
				if (et->GetPointContents(front, MASK_PLAYERSOLID) & MASK_PLAYERSOLID)
				{
					continue;
				}
				int bits = (e.top ? EDGE_TOP : 0) | Continuation(et, p, e.fn);
				if (e.top && !(bits & EDGE_NONWORLD))
				{
					continue;
				}
				const float pt[7] = {p[0], p[1], e.z, e.fn[0], e.fn[1], (float)edge, (float)bits};
				s_edgePoints.insert(s_edgePoints.end(), pt, pt + 7);
			}
		}

		void AddEdge(std::vector<WallEdge> &edges, const float a[2], const float c[2], float z, const float fn[2], bool top)
		{
			if ((c[0] - a[0]) * (c[0] - a[0]) + (c[1] - a[1]) * (c[1] - a[1]) < 1.0f)
			{
				return;
			}
			edges.push_back({{a[0], a[1]}, {c[0], c[1]}, z, {fn[0], fn[1]}, top});
		}

		void BuildEdgePoints(float spacing)
		{
			s_edgePoints.clear();
			s_edgeSpacing = spacing;
			IEngineTrace *et = BSPProps::EngineTrace();
			if (!et || !BSPData::CollideBsp())
			{
				return;
			}
			std::vector<WallEdge> edges;
			int count = BSPData::GetNumBrushes();
			for (int b = 0; b < count; ++b)
			{
				if (!(BSPData::GetBrushContents(b) & MASK_PLAYERSOLID))
				{
					continue;
				}
				// A box brush has no side list (its solid is in the cboxbrush_t table): its four walls are the bounds' faces.
				int sides = BSPData::BrushNumSides(b);
				if (sides <= 0)
				{
					float mn[3], mx[3];
					if (!BSPData::GetBrushBounds(b, mn, mx) || mx[2] - mn[2] < 0.01f)
					{
						continue;
					}
					const float faces[4][6] = {
						// normal x, y, then the edge from (x, y) to (x, y)
						{1.0f, 0.0f, mx[0], mn[1], mx[0], mx[1]},
						{-1.0f, 0.0f, mn[0], mx[1], mn[0], mn[1]},
						{0.0f, 1.0f, mx[0], mx[1], mn[0], mx[1]},
						{0.0f, -1.0f, mn[0], mn[1], mx[0], mn[1]},
					};
					for (const float *f : faces)
					{
						AddEdge(edges, &f[2], &f[4], mn[2], f, false);
						AddEdge(edges, &f[2], &f[4], mx[2], f, true);
					}
					continue;
				}
				for (int s = 0; s < sides; ++s)
				{
					float n[3], d, v[64 * 3];
					if (BSPData::BrushSideBevel(b, s) || !BSPData::BrushSidePlane(b, s, n, d) || std::fabs(n[2]) > 0.3f)
					{
						continue;
					}
					int nv = BSPData::BrushSideWinding(b, s, v, 64);
					if (nv < 3)
					{
						continue;
					}
					float len = std::sqrt(n[0] * n[0] + n[1] * n[1]);
					float fn[2] = {n[0] / len, n[1] / len};
					float zMin = v[2], zMax = v[2];
					for (int i = 1; i < nv; ++i)
					{
						zMin = (std::min)(zMin, v[3 * i + 2]);
						zMax = (std::max)(zMax, v[3 * i + 2]);
					}
					if (zMax - zMin < 0.01f)
					{
						continue;
					}
					for (int i = 0; i < nv; ++i)
					{
						const float *a = &v[3 * i], *c = &v[3 * ((i + 1) % nv)];
						bool top = std::fabs(a[2] - zMax) < 0.01f && std::fabs(c[2] - zMax) < 0.01f;
						bool bottom = std::fabs(a[2] - zMin) < 0.01f && std::fabs(c[2] - zMin) < 0.01f;
						if (top || bottom)
						{
							AddEdge(edges, a, c, top ? zMax : zMin, fn, top);
						}
					}
				}
			}

			// A flush face below can cover only part of a bottom edge, a few units at its end:
			// cut the edge where those faces start and end, so even a short flush stretch gets a point of its own.
			std::map<std::tuple<int, int, int>, std::vector<int>> tops;
			for (int i = 0; i < (int)edges.size(); ++i)
			{
				if (edges[i].top)
				{
					tops[PlaneKey(edges[i])].push_back(i);
				}
			}
			for (int i = 0; i < (int)edges.size(); ++i)
			{
				const WallEdge &e = edges[i];
				float span = std::sqrt((e.c[0] - e.a[0]) * (e.c[0] - e.a[0]) + (e.c[1] - e.a[1]) * (e.c[1] - e.a[1]));
				std::vector<float> cuts = {0.0f, span};
				auto found = e.top ? tops.end() : tops.find(PlaneKey(e));
				if (found != tops.end())
				{
					float dir[2] = {(e.c[0] - e.a[0]) / span, (e.c[1] - e.a[1]) / span};
					for (int j : found->second)
					{
						const WallEdge &t = edges[j];
						if (std::fabs(t.z - e.z) > 0.01f)
						{
							continue;
						}
						for (const float *q : {t.a, t.c})
						{
							float at = (q[0] - e.a[0]) * dir[0] + (q[1] - e.a[1]) * dir[1];
							if (at > 0.5f && at < span - 0.5f)
							{
								cuts.push_back(at);
							}
						}
					}
					std::sort(cuts.begin(), cuts.end());
				}
				for (size_t k = 0; k + 1 < cuts.size(); ++k)
				{
					if (cuts[k + 1] - cuts[k] >= 0.5f)
					{
						SampleSpan(et, e, cuts[k], cuts[k + 1], spacing, i);
					}
				}
			}
		}
	} // namespace

	int WallEdgePoints(float spacing, int first, float *out, int maxOut)
	{
		if (s_edgePoints.empty() || s_edgeSpacing != spacing)
		{
			BuildEdgePoints(spacing);
		}
		int total = (int)(s_edgePoints.size() / 7);
		for (int i = 0; i < maxOut && first + i < total; ++i)
		{
			std::memcpy(out + 7 * i, &s_edgePoints[7 * (first + i)], 7 * sizeof(float));
		}
		return total;
	}

	void OnMapEnd()
	{
		s_edgePoints.clear();
		s_edgeSpacing = 0.0f;
	}
} // namespace BSPSeam
