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
#include "engine/ivmodelinfo.h"
#include "gametrace.h"
#include "iservernetworkable.h"
#include "iserverunknown.h"
#include "tier1/utlvector.h"

#include <algorithm>
#include <array>
#include <cstdlib>
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
		// box: a small box instead of a ray, which meets a vphysics model where movement does (SettleEdge).
		int Continuation(IEngineTrace *et, const float p[3], const float fn[2], bool box)
		{
			const float h = box ? 0.0625f : 0.0f;
			Vector from(p[0] + fn[0], p[1] + fn[1], p[2]), to(p[0] - fn[0], p[1] - fn[1], p[2]);
			Ray_t ray;
			ray.Init(from, to, Vector(-h, -h, -h), Vector(h, h, h));
			auto flush = [&](const CGameTrace &tr)
			{
				float depth = (tr.endpos.x - p[0]) * fn[0] + (tr.endpos.y - p[1]) * fn[1] - h * (std::fabs(fn[0]) + std::fabs(fn[1]));
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

		// shrunk: moved down or up with a vphysics brush entity's collision model (SettleEdge).
		struct WallEdge
		{
			float a[2], c[2], z, fn[2];
			bool top, shrunk;
		};

		// Faces on one plane share it: flat normal to 1/1000, distance to 1/64u.
		std::tuple<int, int, int> PlaneKey(const WallEdge &e)
		{
			float d = e.fn[0] * e.a[0] + e.fn[1] * e.a[1];
			return std::make_tuple((int)std::lround(e.fn[0] * 1000.0f), (int)std::lround(e.fn[1] * 1000.0f), (int)std::lround(d * 64.0f));
		}

		// Samples e between s0 and s1 units along it from a. ownAbove: a bottom edge at this height covers the stretch.
		void SampleSpan(IEngineTrace *et, const WallEdge &e, float s0, float s1, float spacing, int edge, bool ownAbove)
		{
			float span = std::sqrt((e.c[0] - e.a[0]) * (e.c[0] - e.a[0]) + (e.c[1] - e.a[1]) * (e.c[1] - e.a[1]));
			float dir[2] = {(e.c[0] - e.a[0]) / span, (e.c[1] - e.a[1]) / span};
			// Above a shrunk top edge a shrunk brush starts up to 1u higher.
			float past = e.top ? (e.shrunk ? 1.5f : 1.0f) : -1.0f;
			int samples = (std::max)(1, (int)((s1 - s0) / spacing));
			for (int k = 0; k < samples; ++k)
			{
				float c0 = s0 + (s1 - s0) * k / samples, c1 = s0 + (s1 - s0) * (k + 1) / samples, at = (c0 + c1) * 0.5f;
				float p[3] = {e.a[0] + dir[0] * at, e.a[1] + dir[1] * at, e.z + past};
				// Solid in front, just past the edge: a floor or ceiling meets the face here.
				Vector front(p[0] + e.fn[0], p[1] + e.fn[1], p[2]);
				if (et->GetPointContents(front, MASK_PLAYERSOLID) & MASK_PLAYERSOLID)
				{
					continue;
				}
				int bits = e.top ? EDGE_TOP : 0;
				if (!e.shrunk)
				{
					bits |= Continuation(et, p, e.fn, false);
					// Under a world face only inside it, the brush above reaching lower: else that brush's bottom edge has the seam.
					if (e.top && !(bits & EDGE_NONWORLD) && (ownAbove || !(bits & EDGE_CONTINUES)))
					{
						continue;
					}
				}
				else if (e.top)
				{
					// Its own floor carries the seam, with any face flush above: a world brush's bottom edge is higher.
					bits |= Continuation(et, p, e.fn, true);
					if (!(bits & EDGE_CONTINUES))
					{
						continue;
					}
				}
				// A shrunk bottom edge gets no pixelsurf test: the floor under it is lower, and has a top edge of its own.
				const float pt[EDGE_POINT_FLOATS] = {p[0],
													 p[1],
													 e.z,
													 e.fn[0],
													 e.fn[1],
													 (float)edge,
													 (float)bits,
													 e.a[0] + dir[0] * c0,
													 e.a[1] + dir[1] * c0,
													 e.a[0] + dir[0] * c1,
													 e.a[1] + dir[1] * c1};
				s_edgePoints.insert(s_edgePoints.end(), pt, pt + EDGE_POINT_FLOATS);
			}
		}

		void AddEdge(std::vector<WallEdge> &edges, const float a[2], const float c[2], float z, const float fn[2], bool top, bool shrunk)
		{
			if ((c[0] - a[0]) * (c[0] - a[0]) + (c[1] - a[1]) * (c[1] - a[1]) < 1.0f)
			{
				return;
			}
			edges.push_back({{a[0], a[1]}, {c[0], c[1]}, z, {fn[0], fn[1]}, top, shrunk});
		}

		// Horizontal edges of brush b's wall faces, moved by xf (a brush entity's collision transform) when given.
		void BrushFaceEdges(std::vector<WallEdge> &edges, int b, const matrix3x4_t *xf)
		{
			std::vector<std::pair<Vector, std::vector<Vector>>> faces; // outward normal, winding
			int sides = BSPData::BrushNumSides(b);
			if (sides <= 0)
			{
				// A box brush has no side list (its solid is in the cboxbrush_t table): its faces are the bounds'.
				float mn[3], mx[3];
				if (!BSPData::GetBrushBounds(b, mn, mx))
				{
					return;
				}
				for (int axis = 0; axis < 2; ++axis)
				{
					for (int sign : {-1, 1})
					{
						Vector n(0.0f, 0.0f, 0.0f), lo, hi;
						n[axis] = (float)sign;
						lo.Init(mn[0], mn[1], mn[2]);
						hi.Init(mx[0], mx[1], mx[2]);
						lo[axis] = hi[axis] = sign > 0 ? mx[axis] : mn[axis];
						int o = 1 - axis;
						Vector q0 = lo, q1 = lo, q2 = hi, q3 = hi;
						q1[o] = hi[o];
						q3[o] = lo[o];
						faces.push_back({n, {q0, q1, q2, q3}});
					}
				}
			}
			for (int s = 0; s < sides; ++s)
			{
				float n[3], d, v[64 * 3];
				if (BSPData::BrushSideBevel(b, s) || !BSPData::BrushSidePlane(b, s, n, d))
				{
					continue;
				}
				int nv = BSPData::BrushSideWinding(b, s, v, 64);
				if (nv < 3)
				{
					continue;
				}
				std::vector<Vector> w;
				for (int i = 0; i < nv; ++i)
				{
					w.emplace_back(v[3 * i], v[3 * i + 1], v[3 * i + 2]);
				}
				faces.push_back({Vector(n[0], n[1], n[2]), w});
			}
			for (const auto &f : faces)
			{
				const Vector &n = f.first;
				const std::vector<Vector> &w = f.second;
				if (std::fabs(n.z) > 0.3f)
				{
					continue;
				}
				float zMin = w[0].z, zMax = w[0].z;
				for (const Vector &q : w)
				{
					zMin = (std::min)(zMin, q.z);
					zMax = (std::max)(zMax, q.z);
				}
				if (zMax - zMin < 0.01f)
				{
					continue;
				}
				for (size_t i = 0; i < w.size(); ++i)
				{
					Vector a = w[i], c = w[(i + 1) % w.size()];
					bool top = std::fabs(a.z - zMax) < 0.01f && std::fabs(c.z - zMax) < 0.01f;
					bool bottom = std::fabs(a.z - zMin) < 0.01f && std::fabs(c.z - zMin) < 0.01f;
					if (!top && !bottom)
					{
						continue;
					}
					a.z = c.z = top ? zMax : zMin;
					Vector wn = n;
					if (xf)
					{
						Vector t;
						VectorTransform(a, *xf, t);
						a = t;
						VectorTransform(c, *xf, t);
						c = t;
						VectorRotate(n, *xf, wn);
					}
					float len = std::sqrt(wn.x * wn.x + wn.y * wn.y);
					if (std::fabs(a.z - c.z) > 0.01f || len < 0.5f)
					{
						continue;
					}
					const float ea[2] = {a.x, a.y}, ec[2] = {c.x, c.y}, fn[2] = {wn.x / len, wn.y / len};
					AddEdge(edges, ea, ec, a.z, fn, top, false);
				}
			}
		}

		// vbsp builds a vphysics brush entity's vcollide with a side moved in 0.5u where it borders open space and the brush is 1.5u or
		// more thick along its normal (SDK 2013 ivp.cpp). What borders open space isn't in the BSP: find where movement meets e,
		// with box traces against that entity alone (rays meet the brush).
		void SettleEdge(IEngineTrace *et, IHandleEntity *ent, WallEdge &e)
		{
			const float h = 0.0625f;
			const Vector half(h, h, h), fn(e.fn[0], e.fn[1], 0.0f);
			// 0.5u in, or anything else (the brush's own face, other brushes of the entity): left as the brush has it.
			auto in = [](float depth) { return std::fabs(depth - 0.5f) < 0.05f ? 0.5f : 0.0f; };
			Vector mid((e.a[0] + e.c[0]) * 0.5f, (e.a[1] + e.c[1]) * 0.5f, e.top ? e.z - 0.75f : e.z + 0.75f);
			Ray_t ray;
			CGameTrace tr;
			ray.Init(mid + fn * 2.0f, mid - fn * 2.0f, -half, half);
			et->ClipRayToEntity(ray, MASK_PLAYERSOLID, ent, &tr);
			if (tr.fraction < 1.0f && !tr.startsolid)
			{
				// A box stops DIST_EPSILON off a face.
				float side = in(-((tr.endpos - mid).Dot(fn) - h * (std::fabs(fn.x) + std::fabs(fn.y)) - collide::DIST_EPSILON));
				for (float *q : {e.a, e.c})
				{
					q[0] -= fn.x * side;
					q[1] -= fn.y * side;
				}
			}
			Vector q = mid - fn * 0.75f;
			float sign = e.top ? 1.0f : -1.0f;
			ray.Init(Vector(q.x, q.y, e.z + sign), Vector(q.x, q.y, e.z - sign), -half, half);
			et->ClipRayToEntity(ray, MASK_PLAYERSOLID, ent, &tr);
			if (tr.fraction < 1.0f && !tr.startsolid)
			{
				float dz = in(sign * (e.z - (tr.endpos.z - sign * (h + collide::DIST_EPSILON))));
				e.z -= sign * dz;
				e.shrunk = dz > 0.0f;
			}
		}

		// Brushes in submodel model's tree; 0 is the world.
		void ModelBrushes(int model, std::vector<int> &out)
		{
			if (model < 0 || model >= BSPData::GetNumCModels())
			{
				return;
			}
			std::vector<char> seen((size_t)(std::max)(0, BSPData::GetNumBrushes()));
			std::vector<int> stack = {BSPData::CModelHeadnode(model)};
			int buf[1024];
			while (!stack.empty())
			{
				int n = stack.back();
				stack.pop_back();
				if (n < 0)
				{
					int k = BSPData::LeafBrushes(-1 - n, buf, 1024);
					for (int i = 0; i < k; ++i)
					{
						if (buf[i] >= 0 && buf[i] < (int)seen.size() && !seen[buf[i]])
						{
							seen[buf[i]] = 1;
							out.push_back(buf[i]);
						}
					}
					continue;
				}
				int l, r;
				if (BSPData::NodeChildren(n, l, r))
				{
					stack.push_back(l);
					stack.push_back(r);
				}
			}
		}

		// Brush entities that stay put and block movement.
		class StaticBrushEntities : public IEntityEnumerator
		{
		public:
			std::vector<ICollideable *> found;

			bool EnumEntity(IHandleEntity *ent) override
			{
				MovementFilter movement;
				if ((BSPProps::StaticPropMgr() && BSPProps::StaticPropMgr()->IsStaticProp(ent)) || !movement.ShouldHitEntity(ent, MASK_PLAYERSOLID))
				{
					return true;
				}
				IServerNetworkable *net = static_cast<IServerUnknown *>(ent)->GetNetworkable();
				const char *cls = net ? net->GetClassName() : nullptr;
				if (!cls || (std::strcmp(cls, "func_brush") && std::strcmp(cls, "func_wall") && std::strcmp(cls, "func_wall_toggle")))
				{
					return true;
				}
				ICollideable *c = static_cast<IServerUnknown *>(ent)->GetCollideable();
				if (c && (c->GetSolid() == SOLID_BSP || c->GetSolid() == SOLID_VPHYSICS))
				{
					found.push_back(c);
				}
				return true;
			}
		};

		void EntityEdges(IEngineTrace *et, std::vector<WallEdge> &edges)
		{
			IVModelInfo *mi = BSPProps::ModelInfo();
			float mn[3], mx[3];
			if (!mi || !BSPData::CModelBounds(0, mn, mx))
			{
				return;
			}
			StaticBrushEntities ents;
			et->EnumerateEntities(Vector(mn[0], mn[1], mn[2]), Vector(mx[0], mx[1], mx[2]), &ents);
			for (ICollideable *c : ents.found)
			{
				const model_t *m = c->GetCollisionModel();
				// Brush models are "*N", cmodel N. GetModelType answers 0 for them on CS:GO 1575.
				const char *name = m ? mi->GetModelName(m) : nullptr;
				if (!name || name[0] != '*')
				{
					continue;
				}
				matrix3x4_t xf;
				AngleMatrix(c->GetCollisionAngles(), c->GetCollisionOrigin(), xf);
				std::vector<int> brushes;
				ModelBrushes(std::atoi(name + 1), brushes);
				size_t first = edges.size();
				for (int b : brushes)
				{
					if (BSPData::GetBrushContents(b) & MASK_PLAYERSOLID)
					{
						BrushFaceEdges(edges, b, &xf);
					}
				}
				if (c->GetSolid() == SOLID_VPHYSICS)
				{
					for (size_t i = first; i < edges.size(); ++i)
					{
						SettleEdge(et, c->GetEntityHandle(), edges[i]);
					}
				}
			}
		}

		// Top edges of displacement walls: a triangle side no other triangle of the displacement has, with the third corner below.
		// A brush above has the seam on its bottom edge.
		void DispEdges(std::vector<WallEdge> &edges)
		{
			std::vector<float> tris;
			for (int d = 0; d < BSPDisp::EngineCount(); ++d)
			{
				if (!(BSPDisp::EngineGetContents(d) & MASK_PLAYERSOLID) || !BSPDisp::EngineTriangles(d, tris))
				{
					continue;
				}
				// Corners are shared bit for bit within a displacement.
				std::map<std::array<float, 6>, int> uses;
				auto key = [&](size_t t, int i, int j)
				{
					const float *p = &tris[12 * t + 3 * i], *q = &tris[12 * t + 3 * j];
					std::array<float, 6> k = {p[0], p[1], p[2], q[0], q[1], q[2]};
					if (std::lexicographical_compare(q, q + 3, p, p + 3))
					{
						k = {q[0], q[1], q[2], p[0], p[1], p[2]};
					}
					return k;
				};
				size_t count = tris.size() / 12;
				for (size_t t = 0; t < count; ++t)
				{
					for (int i = 0; i < 3; ++i)
					{
						++uses[key(t, i, (i + 1) % 3)];
					}
				}
				for (size_t t = 0; t < count; ++t)
				{
					const float *n = &tris[12 * t + 9];
					float len = std::sqrt(n[0] * n[0] + n[1] * n[1]);
					if (std::fabs(n[2]) > 0.3f || len < 0.5f)
					{
						continue;
					}
					for (int i = 0; i < 3; ++i)
					{
						const float *a = &tris[12 * t + 3 * i], *c = &tris[12 * t + 3 * ((i + 1) % 3)], *o = &tris[12 * t + 3 * ((i + 2) % 3)];
						if (std::fabs(a[2] - c[2]) > 0.01f || o[2] > a[2] - 0.01f || uses[key(t, i, (i + 1) % 3)] != 1)
						{
							continue;
						}
						const float fn[2] = {n[0] / len, n[1] / len};
						AddEdge(edges, a, c, (std::max)(a[2], c[2]), fn, true, false);
					}
				}
			}
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
			std::vector<int> world;
			ModelBrushes(0, world);
			for (int b : world)
			{
				if (BSPData::GetBrushContents(b) & MASK_PLAYERSOLID)
				{
					BrushFaceEdges(edges, b, nullptr);
				}
			}
			EntityEdges(et, edges);
			if (BSPDisp::EngineReady())
			{
				DispEdges(edges);
			}

			// A flush face can cover only part of a seam edge, a few units at its end:
			// cut the edge where those faces start and end, so even a short flush stretch gets a point of its own.
			// A bottom edge's are top edges at its height, a top edge's bottom edges, a shrunk top edge's bottom edges up to 1u above it.
			std::map<std::tuple<int, int, int>, std::vector<int>> onPlane;
			for (int i = 0; i < (int)edges.size(); ++i)
			{
				onPlane[PlaneKey(edges[i])].push_back(i);
			}
			for (int i = 0; i < (int)edges.size(); ++i)
			{
				const WallEdge &e = edges[i];
				float span = std::sqrt((e.c[0] - e.a[0]) * (e.c[0] - e.a[0]) + (e.c[1] - e.a[1]) * (e.c[1] - e.a[1]));
				std::vector<float> cuts = {0.0f, span};
				std::vector<std::pair<float, float>> above;
				if (e.top || !e.shrunk)
				{
					float dir[2] = {(e.c[0] - e.a[0]) / span, (e.c[1] - e.a[1]) / span};
					for (int j : onPlane[PlaneKey(e)])
					{
						const WallEdge &t = edges[j];
						float up = t.z - e.z;
						if (t.top == e.top || (e.shrunk ? up < 0.49f || up > 1.01f : std::fabs(up) > 0.01f))
						{
							continue;
						}
						float at[2];
						for (int k = 0; k < 2; ++k)
						{
							const float *q = k ? t.c : t.a;
							at[k] = (q[0] - e.a[0]) * dir[0] + (q[1] - e.a[1]) * dir[1];
							if (at[k] > 0.5f && at[k] < span - 0.5f)
							{
								cuts.push_back(at[k]);
							}
						}
						if (e.top)
						{
							above.push_back({(std::min)(at[0], at[1]), (std::max)(at[0], at[1])});
						}
					}
					std::sort(cuts.begin(), cuts.end());
				}
				for (size_t k = 0; k + 1 < cuts.size(); ++k)
				{
					if (cuts[k + 1] - cuts[k] >= 0.5f)
					{
						float mid = (cuts[k] + cuts[k + 1]) * 0.5f;
						bool ownAbove = std::any_of(above.begin(), above.end(),
													[&](const std::pair<float, float> &r) { return r.first <= mid && mid <= r.second; });
						SampleSpan(et, e, cuts[k], cuts[k + 1], spacing, i, ownAbove);
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
		int total = (int)(s_edgePoints.size() / EDGE_POINT_FLOATS);
		for (int i = 0; i < maxOut && first + i < total; ++i)
		{
			std::memcpy(out + EDGE_POINT_FLOATS * i, &s_edgePoints[EDGE_POINT_FLOATS * (first + i)], EDGE_POINT_FLOATS * sizeof(float));
		}
		return total;
	}

	void OnMapEnd()
	{
		s_edgePoints.clear();
		s_edgeSpacing = 0.0f;
	}
} // namespace BSPSeam
