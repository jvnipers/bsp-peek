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
#include <vector>

namespace BSPSeam
{
	namespace
	{
		// CTraceFilterSimple with COLLISION_GROUP_PLAYER_MOVEMENT, minus players.
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
					case COLLISION_GROUP_DOOR_BLOCKER:
					case COLLISION_GROUP_PASSABLE_DOOR:
					case COLLISION_GROUP_DISSOLVING:
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

			float add = (std::min)((std::max)(30.0f - vin, 0.0f), airaccelerate * 250.0f * frametime);
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
			  float outSpeed[2], float outStart[6])
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
				float z = seamZ + dz;
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
							if (Caught(mt, start, normal, vin, -kCatchSpeeds[s], frametime, gravity, airaccelerate, maxspeed, nonAxial))
							{
								if (!flags)
								{
									outStart[0] = start.x;
									outStart[1] = start.y;
									outStart[2] = start.z;
									outStart[3] = -normal[0] * vin;
									outStart[4] = -normal[1] * vin;
									outStart[5] = -kCatchSpeeds[s];
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
			return 0;
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
		return flags;
	}

	int Candidates(const float wallPos[3], const float normal[3], float zMin, float zMax, float *out, int maxOut)
	{
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
} // namespace BSPSeam
