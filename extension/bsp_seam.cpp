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
		// Pushes as a fraction of the fall, and over the window's lower bound g v / o (the hull reaches the wall before its
		// top passes the edge): the upper, v (eps - g) / (H_b + H_h - o + eps), is under twice that for a tall brush.
		const float kTbPushes[] = {1e-6f, 3e-6f, 1e-5f, 3e-5f, 1e-4f, 2e-4f, 4e-4f};
		const float kTbOverLow[] = {1.05f, 1.3f, 2.0f, 4.0f};

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
					float pushes[std::size(kTbPushes) + std::size(kTbOverLow)];
					size_t n = 0;
					for (float k : kTbPushes)
					{
						pushes[n++] = k * vm;
					}
					for (float k : kTbOverLow)
					{
						pushes[n++] = k * gap / (top * frametime);
					}
					for (float push : pushes)
					{
						float vel[3] = {-normal[0] * push, -normal[1] * push, -vm};
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

	int PixelwalkCatch(const float wallPos[3], const float normal[3], float edgeZ, float outInfo[4])
	{
		outInfo[0] = outInfo[1] = outInfo[2] = outInfo[3] = 0.0f;
		IEngineTrace *et = BSPProps::EngineTrace();
		if (!et)
		{
			return -1;
		}
		float len = std::hypot(normal[0], normal[1]);
		if (len < 0.001f)
		{
			return 0;
		}
		Vector n(normal[0] / len, normal[1] / len, 0.0f);
		ITraceListData *list = et->AllocTraceListData();
		if (!list)
		{
			return -1;
		}
		MoveTrace mt;
		mt.list = list;
		mt.mins.Init(-16.0f, -16.0f, 0.0f);
		Vector pad(18.0f, 18.0f, 18.0f);
		auto clear = [&](const Vector &p)
		{
			et->SetupLeafAndEntityListBox(p + mt.mins - pad, p + mt.maxs + pad, mt.list);
			CGameTrace tr;
			mt.Run(p, p, tr);
			return !tr.startsolid;
		};
		// CategorizePosition's 2u trace can start clear where a zero-length one starts solid (touching a cover): stuck, not standing.
		// Again from where it rests: a +Z bevel vbsp put under a slanted ramp's top, after the slope in side order, gives way to the slope.
		auto stands = [&](const Vector &p, float &groundZ)
		{
			Vector down(p.x, p.y, p.z - 2.0f);
			et->SetupLeafAndEntityListBox(down - Vector(0.0f, 0.0f, 2.0f) + mt.mins - pad, p + mt.maxs + pad, mt.list);
			CGameTrace tr;
			mt.Run(p, p, tr);
			if (tr.startsolid)
			{
				return false;
			}
			mt.Run(p, down, tr);
			if (tr.startsolid || tr.fraction >= 1.0f || tr.plane.normal.z < 0.7f)
			{
				return false;
			}
			Vector rest = tr.endpos;
			mt.Run(rest, rest - Vector(0.0f, 0.0f, 2.0f), tr);
			groundZ = tr.endpos.z;
			return !tr.startsolid && tr.fraction < 1.0f && tr.plane.normal.z >= 0.7f;
		};
		// Exactly on a face that goes on, a zero-length test can pass where every move stops at once:
		// it and the sweep round the face's distance differently. So a drop from 2u up must land there, and a jump from there must get off it.
		auto moves = [&](const Vector &p)
		{
			Vector up(p.x, p.y, p.z + 2.0f), down(p.x, p.y, p.z - 2.0f);
			et->SetupLeafAndEntityListBox(down + mt.mins - pad, up + mt.maxs + pad, mt.list);
			CGameTrace tr;
			mt.Run(up, down, tr);
			if (tr.startsolid || tr.fraction <= 0.0f || tr.fraction >= 1.0f || tr.plane.normal.z < 0.7f)
			{
				return false;
			}
			mt.Run(p, up, tr);
			return !tr.startsolid && tr.fraction > 0.0f;
		};
		// 1u out from 1u up: a floor out there a little higher than the strip would start a hull at the strip's height solid.
		auto floorOut = [&](const Vector &p)
		{
			Vector up(p.x, p.y, p.z + 1.0f), down(p.x, p.y, p.z - 2.0f);
			et->SetupLeafAndEntityListBox(down + mt.mins - pad, up + mt.maxs + pad, mt.list);
			CGameTrace tr;
			mt.Run(up, down, tr);
			return !tr.startsolid && tr.fraction < 1.0f && tr.plane.normal.z >= 0.7f;
		};
		float reach = 16.0f * (std::fabs(n.x) + std::fabs(n.y));
		int creep = 0, exact = 0;
		for (int duck = 0; duck < 2; ++duck)
		{
			mt.maxs.Init(16.0f, 16.0f, duck ? 54.0f : 72.0f);
			float z = edgeZ + 0.03125f;
			// The face the hull rests against (over a ledge, the wall above it), 1u up: a ramp rising into the wall is in the way lower.
			CGameTrace hit;
			bool found = false;
			for (float out : {48.0f, reach + 2.0f})
			{
				Vector from(wallPos[0] + n.x * out, wallPos[1] + n.y * out, z + 1.0f);
				Vector to(wallPos[0] - n.x * 8.0f, wallPos[1] - n.y * 8.0f, z + 1.0f);
				// Movement pads its list box (MovementListBox): a brush only touching the hull's box is in it.
				Vector lo, hi;
				VectorMin(from, to, lo);
				VectorMax(from, to, hi);
				et->SetupLeafAndEntityListBox(lo + mt.mins - pad, hi + mt.maxs + pad, mt.list);
				mt.Run(from, to, hit);
				// Up to 1u behind the point (a ledge's wall); a face further out is another strip's.
				float behind = DotProduct(Vector(wallPos[0], wallPos[1], wallPos[2]), hit.plane.normal) - hit.plane.dist;
				if (!hit.startsolid && hit.fraction < 1.0f && DotProduct(hit.plane.normal, n) > 0.999f && behind > -0.1f && behind < 1.0f)
				{
					found = true;
					break;
				}
			}
			if (!found)
			{
				continue;
			}
			// The gap runs from the hull's nearest edge to the face's plane; on an axial face in float steps of one coordinate.
			const Vector &f = hit.plane.normal;
			// Not plane.type: it doesn't follow the normal on a box brush's hit.
			int axis = std::fabs(f.x) == 1.0f ? 0 : (std::fabs(f.y) == 1.0f ? 1 : -1);
			float sign = axis >= 0 && f[axis] < 0.0f ? -1.0f : 1.0f;
			float onFace = axis >= 0 ? (hit.plane.dist + 16.0f) * sign : 0.0f, groundZ;
			Vector p(hit.endpos.x, hit.endpos.y, z);
			// A face leaning out meets the hull's top edge.
			double pGap = (double)p.x * f.x + (double)p.y * f.y + (double)p.z * f.z - hit.plane.dist - 16.0 * (std::fabs(f.x) + std::fabs(f.y))
						  + (std::min)(0.0, (double)mt.maxs.z * f.z);
			double h = (double)f.x * f.x + (double)f.y * f.y;
			auto at = [&](float gap)
			{
				Vector q = p;
				if (axis >= 0)
				{
					q[axis] = onFace + sign * gap;
				}
				else
				{
					q.x = (float)(p.x + f.x * (gap - pGap) / h);
					q.y = (float)(p.y + f.y * (gap - pGap) / h);
				}
				return q;
			};
			float creptGap;
			if (axis >= 0)
			{
				creptGap = (std::nextafter(onFace, onFace + 64.0f * sign) - onFace) * sign;
			}
			else
			{
				// The closest clear gap, two float steps out: closer, movement's list traces can start solid at other heights.
				float lo = 0.0f, hi = 0.03125f;
				if (!clear(at(hi)))
				{
					continue;
				}
				for (int k = 0; k < 24; ++k)
				{
					float mid = (lo + hi) * 0.5f;
					(clear(at(mid)) ? hi : lo) = mid;
				}
				float c = (std::max)(std::fabs(p.x), std::fabs(p.y));
				creptGap = hi + 2.0f * (std::nextafter(c, 2.0f * c + 1.0f) - c);
			}
			Vector crept = at(creptGap);
			int bit = duck ? CATCH_DUCKED : CATCH_STANDING;
			if (stands(crept, groundZ) && !stands(at(1.0f), groundZ) && !floorOut(at(1.0f)))
			{
				// The standing hull's origin: under an overhang the ducked one gets closer, where a standing hull is stuck.
				if (!creep)
				{
					stands(crept, groundZ);
					outInfo[1] = groundZ;
					outInfo[2] = crept.x;
					outInfo[3] = crept.y;
				}
				creep |= bit;
				float lo = creptGap, hi = 1.0f;
				for (int k = 0; k < 16; ++k)
				{
					float mid = (lo + hi) * 0.5f, g;
					(stands(at(mid), g) ? lo : hi) = mid;
				}
				outInfo[0] = (std::max)(outInfo[0], lo);
			}
			else if (axis >= 0 && !creep && stands(at(0.0f), groundZ) && moves(at(0.0f)))
			{
				if (!exact)
				{
					Vector on = at(0.0f);
					stands(on, groundZ);
					outInfo[1] = groundZ;
					outInfo[2] = on.x;
					outInfo[3] = on.y;
				}
				exact |= bit;
			}
		}
		et->FreeTraceListData(list);
		return creep ? creep : (exact ? exact | CATCH_EXACT : 0);
	}

	int PixelwalkEnd(const float endPos[3], const float normal[3], const float along[2], float edgeZ, float out[3])
	{
		out[0] = out[1] = out[2] = 0.0f;
		IEngineTrace *et = BSPProps::EngineTrace();
		if (!et)
		{
			return -1;
		}
		int axis = std::fabs(normal[0]) > 0.999f ? 0 : (std::fabs(normal[1]) > 0.999f ? 1 : -1);
		if (axis < 0)
		{
			return 0;
		}
		float sign = normal[axis] > 0.0f ? 1.0f : -1.0f, z = edgeZ + 0.03125f;
		ITraceListData *list = et->AllocTraceListData();
		if (!list)
		{
			return -1;
		}
		MoveTrace mt;
		mt.list = list;
		mt.mins.Init(-16.0f, -16.0f, 0.0f);
		Vector pad(18.0f, 18.0f, 18.0f);
		int flags = 0;
		for (int duck = 0; duck < 2; ++duck)
		{
			mt.maxs.Init(16.0f, 16.0f, duck ? 54.0f : 72.0f);
			Vector in(endPos[0] + along[0] * 17.0f, endPos[1] + along[1] * 17.0f, z);
			Vector from(in.x + normal[0] * 48.0f, in.y + normal[1] * 48.0f, z), to(in.x - normal[0] * 8.0f, in.y - normal[1] * 8.0f, z);
			Vector lo, hi;
			VectorMin(from, to, lo);
			VectorMax(from, to, hi);
			et->SetupLeafAndEntityListBox(lo + mt.mins - pad, hi + mt.maxs + pad, mt.list);
			CGameTrace hit;
			mt.Run(from, to, hit);
			if (hit.startsolid || hit.fraction >= 1.0f || hit.plane.normal[axis] * sign <= 0.999f
				|| std::fabs((endPos[axis] - hit.plane.dist * sign) * sign) > 0.1f)
			{
				continue;
			}
			float onFace = hit.plane.dist * sign + 16.0f * sign;
			auto origin = [&](float s, float gap)
			{
				Vector q(endPos[0] + along[0] * s, endPos[1] + along[1] * s, z);
				q[axis] = onFace + sign * gap;
				return q;
			};
			auto down = [&](const Vector &p, CGameTrace &tr)
			{
				Vector d(p.x, p.y, p.z - 2.0f);
				et->SetupLeafAndEntityListBox(d + mt.mins - pad, p + mt.maxs + pad, mt.list);
				mt.Run(p, d, tr);
			};
			// Held: on ground or stuck; inside: stuck (a zero-length trace starts solid). Both only grow from off the end along the edge.
			auto stuck = [&](const Vector &p)
			{
				CGameTrace tr;
				mt.Run(p, p, tr);
				return tr.startsolid;
			};
			auto held = [&](float s)
			{
				CGameTrace tr;
				down(origin(s, 0.0f), tr);
				return tr.startsolid || stuck(origin(s, 0.0f)) || (tr.fraction < 1.0f && tr.plane.normal.z >= 0.7f);
			};
			auto inside = [&](float s)
			{
				CGameTrace tr;
				down(origin(s, 0.0f), tr);
				return tr.startsolid || stuck(origin(s, 0.0f));
			};
			if (held(-17.0f) || !inside(17.0f))
			{
				continue;
			}
			auto bisect = [&](auto test)
			{
				float a = -17.0f, b = 17.0f;
				for (int k = 0; k < 24; ++k)
				{
					float m = (a + b) * 0.5f;
					(test(m) ? b : a) = m;
				}
				return b;
			};
			float sa = bisect(held), sb = bisect(inside);
			if (sb <= sa)
			{
				continue;
			}
			float mid = (sa + sb) * 0.5f;
			CGameTrace tr;
			down(origin(mid, 0.0f), tr);
			bool stands = !tr.startsolid && !stuck(origin(mid, 0.0f)) && tr.fraction < 1.0f && tr.plane.normal.z >= 0.7f;
			// Not a floor 1u into the wall either (the top past the cover), nor 1u out.
			CGameTrace tin, tout;
			down(origin(mid, -1.0f), tin);
			down(origin(mid, 1.0f), tout);
			bool floorIn = !tin.startsolid && tin.fraction < 1.0f && tin.plane.normal.z >= 0.7f;
			bool floorOut = !tout.startsolid && tout.fraction < 1.0f && tout.plane.normal.z >= 0.7f;
			if (!stands || floorIn || floorOut)
			{
				continue;
			}
			flags |= duck ? CATCH_DUCKED : CATCH_STANDING;
			Vector m = origin(mid, 0.0f);
			out[0] = m.x;
			out[1] = m.y;
			out[2] = sb - sa;
		}
		et->FreeTraceListData(list);
		return flags ? flags | CATCH_EXACT : 0;
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
				// The top of a face leaning back the way the wall faces: where the wall rises from a ramp, outside the column.
				for (int s = 0, sides = BSPData::BrushNumSides(b); s < sides; ++s)
				{
					float n[3], d, v[3 * 64];
					if (BSPData::BrushSideBevel(b, s) || !BSPData::BrushSidePlane(b, s, n, d) || n[2] <= 0.001f || n[2] >= 0.7f
						|| n[0] * normal[0] + n[1] * normal[1] < 0.999f * std::sqrt(n[0] * n[0] + n[1] * n[1]))
					{
						continue;
					}
					int nv = BSPData::BrushSideWinding(b, s, v, 64);
					float zTop = -FLT_MAX;
					for (int i = 0; i < nv; ++i)
					{
						zTop = (std::max)(zTop, v[3 * i + 2]);
					}
					if (nv >= 3)
					{
						zs.push_back(zTop);
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
		// End points, after every edge's cells: in among them they'd split neighbouring cells of one edge.
		std::vector<float> s_edgeEnds;
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

		// Flush: within 1/4u of where the face would be at p (expect, for a leaning face), facing the same way. World first:
		// displacements are world. A box, not a ray: it meets a vphysics model where movement does, a ray meets the brush.
		// back: how far behind where it would be a face facing the same way is (world first), 0 if none.
		int Continuation(IEngineTrace *et, const float p[3], const float fn[2], float expect, float *back = nullptr)
		{
			const float h = 0.0625f;
			Vector from(p[0] + fn[0], p[1] + fn[1], p[2]), to(p[0] - fn[0], p[1] - fn[1], p[2]);
			Ray_t ray;
			ray.Init(from, to, Vector(-h, -h, -h), Vector(h, h, h));
			auto flush = [&](const CGameTrace &tr)
			{
				float depth = (tr.endpos.x - p[0]) * fn[0] + (tr.endpos.y - p[1]) * fn[1] - h * (std::fabs(fn[0]) + std::fabs(fn[1]));
				return tr.fraction < 1.0f && !tr.startsolid && tr.plane.normal.x * fn[0] + tr.plane.normal.y * fn[1] > 0.9f
					   && std::fabs(depth - expect) < 0.25f;
			};
			NothingFilter world;
			CGameTrace tr;
			et->TraceRay(ray, MASK_PLAYERSOLID, &world, &tr);
			if (back && tr.fraction < 1.0f && !tr.startsolid && tr.plane.normal.x * fn[0] + tr.plane.normal.y * fn[1] > 0.9f)
			{
				*back = expect - ((tr.endpos.x - p[0]) * fn[0] + (tr.endpos.y - p[1]) * fn[1] - h * (std::fabs(fn[0]) + std::fabs(fn[1])));
			}
			if (flush(tr))
			{
				return EDGE_CONTINUES | (tr.dispFlags ? EDGE_NONWORLD : 0);
			}
			MovementFilter movement;
			CGameTrace te;
			et->TraceRay(ray, MASK_PLAYERSOLID, &movement, &te);
			return flush(te) ? (EDGE_CONTINUES | EDGE_NONWORLD) : 0;
		}

		// lean: the face normal's z over its flat length, how far the face moves in per unit up.
		// shrunk: moved down or up with a vphysics brush entity's collision model (SettleEdge).
		// prop: a prop's top edge, kept only under a displacement, prop or entity (a prop under the world never hovers).
		// floor: a displacement floor's open side or wall top's end, a ledge for pixelwalks only.
		struct WallEdge
		{
			float a[2], c[2], z, fn[2], lean;
			bool top, shrunk, prop, ramp, floor;
		};

		// Faces on one plane share it: flat normal to 1/1000, distance to 1/64u, up units above e (a leaning face moves in going up).
		std::tuple<int, int, int> PlaneKey(const WallEdge &e, float up = 0.0f)
		{
			float d = e.fn[0] * e.a[0] + e.fn[1] * e.a[1] - e.lean * up;
			return std::make_tuple((int)std::lround(e.fn[0] * 1000.0f), (int)std::lround(e.fn[1] * 1000.0f), (int)std::lround(d * 64.0f));
		}

		// A wall face, for cutting the edges of other faces on its plane where it crosses their height.
		struct WallFace
		{
			float zMin, zMax;
			std::vector<Vector> w;
		};

		std::map<std::tuple<int, int, int>, std::vector<WallFace>> s_wallFaces;

		// A plane's key at z 0: flat normal to 1/1000, distance to 1/64u.
		std::tuple<int, int, int> FaceKey(const float fn[2], float lean, float x, float y, float z)
		{
			float d = fn[0] * x + fn[1] * y + lean * z;
			return std::make_tuple((int)std::lround(fn[0] * 1000.0f), (int)std::lround(fn[1] * 1000.0f), (int)std::lround(d * 64.0f));
		}

		// Samples e between s0 and s1 units along it from a. ownAbove: a bottom edge at this height covers the stretch.
		void SampleSpan(IEngineTrace *et, const WallEdge &e, float s0, float s1, float spacing, int edge, bool ownAbove)
		{
			float span = std::sqrt((e.c[0] - e.a[0]) * (e.c[0] - e.a[0]) + (e.c[1] - e.a[1]) * (e.c[1] - e.a[1]));
			float dir[2] = {(e.c[0] - e.a[0]) / span, (e.c[1] - e.a[1]) / span};
			// Above a shrunk top edge a shrunk brush starts up to 1u higher.
			float past = e.top ? (e.shrunk ? 1.5f : 1.0f) : -1.0f;
			int samples = (std::max)(1, (int)std::lround((s1 - s0) / spacing));
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
				if (e.ramp)
				{
					bits |= EDGE_RAMP;
				}
				else if (e.floor)
				{
					// A wall flush above it or set back under 1u.
					float back = 0.0f;
					if (!(Continuation(et, p, e.fn, 0.0f, &back) & EDGE_CONTINUES) && !(back > 0.0f && back < 1.0f))
					{
						continue;
					}
					bits |= EDGE_LEDGE;
				}
				else if (!e.shrunk)
				{
					float back = 0.0f;
					bits |= Continuation(et, p, e.fn, -e.lean * past, &back);
					// A ledge: the face above set back 0.25 to 1u.
					if (e.top && !(bits & EDGE_CONTINUES) && back > 0.25f && back < 1.0f)
					{
						bits |= EDGE_LEDGE;
					}
					// A covered top edge's ends (exact pixelwalks), before the skip: the cover's bottom edge carrying the seam has other ends.
					if (e.top && !e.prop && (bits & EDGE_CONTINUES))
					{
						for (int end = 0; end < 2; ++end)
						{
							if (end ? (k != samples - 1 || std::fabs(s1 - span) > 0.001f) : (k != 0 || s0 > 0.001f))
							{
								continue;
							}
							const float *q = end ? e.c : e.a;
							float in = end ? -1.0f : 1.0f;
							const float ept[EDGE_POINT_FLOATS] = {q[0],
																  q[1],
																  e.z,
																  e.fn[0],
																  e.fn[1],
																  (float)edge,
																  (float)(bits | EDGE_END),
																  q[0],
																  q[1],
																  q[0] + dir[0] * in,
																  q[1] + dir[1] * in};
							s_edgeEnds.insert(s_edgeEnds.end(), ept, ept + EDGE_POINT_FLOATS);
						}
					}
					// A wall leaning back is a ramp too, unless the face above goes on in its plane. A face straight up passes the 1/4u flush test;
					// back tells them apart (the probe stops 1/32 off: 1/32 + lean / 16 in front in the plane, 1/32 + lean * past straight up).
					if (e.top && e.lean > 0.001f && !(bits & EDGE_LEDGE)
						&& !((bits & EDGE_CONTINUES) && back > -0.03125f - e.lean * (past + 0.0625f) * 0.5f))
					{
						bits |= EDGE_RAMP;
					}
					// Under a world face only inside it, the brush above reaching lower: else that brush's bottom edge has the seam.
					if (e.top && !(bits & (EDGE_NONWORLD | EDGE_LEDGE | EDGE_RAMP)) && (ownAbove || !(bits & EDGE_CONTINUES)))
					{
						continue;
					}
				}
				else if (e.top)
				{
					// Its own floor carries the seam, with any face flush above: a world brush's bottom edge is higher.
					bits |= Continuation(et, p, e.fn, -e.lean * past);
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

		void AddEdge(std::vector<WallEdge> &edges, const float a[2], const float c[2], float z, const float fn[2], float lean, bool top, bool shrunk,
					 bool prop = false, bool ramp = false, bool floor = false)
		{
			if ((c[0] - a[0]) * (c[0] - a[0]) + (c[1] - a[1]) * (c[1] - a[1]) < 1.0f)
			{
				return;
			}
			edges.push_back({{a[0], a[1]}, {c[0], c[1]}, z, {fn[0], fn[1]}, lean, top, shrunk, prop, ramp, floor});
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
			for (auto &f : faces)
			{
				Vector n = f.first;
				std::vector<Vector> &w = f.second;
				if (xf)
				{
					Vector t;
					for (Vector &q : w)
					{
						VectorTransform(q, *xf, t);
						q = t;
					}
					VectorRotate(f.first, *xf, n);
				}
				float len = std::sqrt(n.x * n.x + n.y * n.y);
				// A ramp: only its top edge, where a wall rising from it gives a pixelwalk.
				bool ramp = n.z > 0.3f && n.z < 0.7f;
				if ((std::fabs(n.z) > 0.3f && !ramp) || len < 0.5f)
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
				const float fn[2] = {n.x / len, n.y / len};
				if (!ramp)
				{
					s_wallFaces[FaceKey(fn, n.z / len, w[0].x, w[0].y, w[0].z)].push_back({zMin, zMax, w});
				}
				for (size_t i = 0; i < w.size(); ++i)
				{
					Vector a = w[i], c = w[(i + 1) % w.size()];
					bool top = std::fabs(a.z - zMax) < 0.01f && std::fabs(c.z - zMax) < 0.01f;
					bool bottom = std::fabs(a.z - zMin) < 0.01f && std::fabs(c.z - zMin) < 0.01f;
					if (!top && (ramp || !bottom))
					{
						continue;
					}
					const float ea[2] = {a.x, a.y}, ec[2] = {c.x, c.y};
					AddEdge(edges, ea, ec, top ? zMax : zMin, fn, n.z / len, top, false, false, ramp);
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
				// A box stops DIST_EPSILON off a face. A side moves 0.5u along its own normal: across, by k on a leaning face.
				float k = std::sqrt(1.0f + e.lean * e.lean), brush = -e.lean * (mid.z - e.z);
				float hit = (tr.endpos - mid).Dot(fn) - h * (std::fabs(fn.x) + std::fabs(fn.y)) - collide::DIST_EPSILON;
				float side = in((brush - hit) / k) * k;
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
				// A leaning face is lean * dz across at the new height.
				for (float *v : {e.a, e.c})
				{
					v[0] += fn.x * e.lean * sign * dz;
					v[1] += fn.y * e.lean * sign * dz;
				}
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
				// Brush models are "*N", cmodel N.
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

		// Top edges of displacement walls and ramps:
		// a level triangle side no other triangle of the displacement has, the third corner below.
		// Kept under a brush face too, unless that brush's bottom edge is on the same plane at the same height.
		// And such sides of floors, facing away from the third corner: ledges where a wall stands above, flush or set back under 1u.
		// Likewise a wall's top end, facing along it: a hull straddling the sheet stands on its top, here against a wall it runs past.
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
				// Wall top sides by corner: a corner only one of them has is the top's end.
				std::map<std::array<float, 3>, std::vector<std::array<float, 3>>> tops;
				for (size_t t = 0; t < count; ++t)
				{
					const float *n = &tris[12 * t + 9];
					float len = std::sqrt(n[0] * n[0] + n[1] * n[1]);
					bool ramp = n[2] > 0.3f && n[2] < 0.7f, floor = n[2] >= 0.7f;
					if (!floor && ((std::fabs(n[2]) > 0.3f && !ramp) || len < 0.5f))
					{
						continue;
					}
					for (int i = 0; i < 3; ++i)
					{
						const float *a = &tris[12 * t + 3 * i], *c = &tris[12 * t + 3 * ((i + 1) % 3)], *o = &tris[12 * t + 3 * ((i + 2) % 3)];
						if (std::fabs(a[2] - c[2]) > 0.01f || (!floor && o[2] > a[2] - 0.01f) || uses[key(t, i, (i + 1) % 3)] != 1)
						{
							continue;
						}
						if (floor)
						{
							float ex = c[0] - a[0], ey = c[1] - a[1], el = std::sqrt(ex * ex + ey * ey);
							if (el < 0.001f)
							{
								continue;
							}
							float fn[2] = {ey / el, -ex / el};
							if ((o[0] - a[0]) * fn[0] + (o[1] - a[1]) * fn[1] > 0.0f)
							{
								fn[0] = -fn[0];
								fn[1] = -fn[1];
							}
							AddEdge(edges, a, c, (std::max)(a[2], c[2]), fn, 0.0f, true, false, false, false, true);
							continue;
						}
						const float fn[2] = {n[0] / len, n[1] / len};
						AddEdge(edges, a, c, (std::max)(a[2], c[2]), fn, n[2] / len, true, false, false, ramp);
						if (!ramp)
						{
							tops[{a[0], a[1], a[2]}].push_back({c[0], c[1], c[2]});
							tops[{c[0], c[1], c[2]}].push_back({a[0], a[1], a[2]});
						}
					}
				}
				for (const auto &[q, others] : tops)
				{
					if (others.size() != 1)
					{
						continue;
					}
					float dx = q[0] - others[0][0], dy = q[1] - others[0][1], dl = std::sqrt(dx * dx + dy * dy);
					if (dl < 0.001f)
					{
						continue;
					}
					const float fn[2] = {dx / dl, dy / dl};
					const float a[3] = {q[0] + fn[1], q[1] - fn[0], q[2]}, c[3] = {q[0] - fn[1], q[1] + fn[0], q[2]};
					AddEdge(edges, a, c, q[2], fn, 0.0f, true, false, false, false, true);
				}
			}
		}

		// Top edges of a prop mesh's wall faces: a triangle side no coplanar triangle shares, third corner below. The winding isn't
		// reliable and combined props hide faces inside: the front is the side a ray meets the face from; neither side, inside.
		void MeshTopEdges(IEngineTrace *et, ICollideable *c, const std::vector<float> &m, std::vector<WallEdge> &edges)
		{
			size_t count = m.size() / 9;
			std::vector<Vector> normal(count);
			std::vector<std::pair<std::array<int, 6>, size_t>> sides; // triangle sides by their corners, sorted
			auto key = [&](size_t t, int i, int j)
			{
				const float *p = &m[9 * t + 3 * i], *q = &m[9 * t + 3 * j];
				std::array<int, 6> k = {(int)std::lround(p[0] * 256.0f), (int)std::lround(p[1] * 256.0f), (int)std::lround(p[2] * 256.0f),
										(int)std::lround(q[0] * 256.0f), (int)std::lround(q[1] * 256.0f), (int)std::lround(q[2] * 256.0f)};
				if (std::lexicographical_compare(k.begin() + 3, k.end(), k.begin(), k.begin() + 3))
				{
					std::rotate(k.begin(), k.begin() + 3, k.end());
				}
				return k;
			};
			for (size_t t = 0; t < count; ++t)
			{
				Vector a(m[9 * t], m[9 * t + 1], m[9 * t + 2]), b(m[9 * t + 3], m[9 * t + 4], m[9 * t + 5]),
					cc(m[9 * t + 6], m[9 * t + 7], m[9 * t + 8]);
				normal[t] = (b - a).Cross(cc - a);
				normal[t].NormalizeInPlace();
				for (int i = 0; i < 3; ++i)
				{
					sides.push_back({key(t, i, (i + 1) % 3), t});
				}
			}
			std::sort(sides.begin(), sides.end());
			for (size_t t = 0; t < count; ++t)
			{
				const Vector &n = normal[t];
				float len = std::sqrt(n.x * n.x + n.y * n.y);
				if (std::fabs(n.z) > 0.3f || len < 0.5f)
				{
					continue;
				}
				for (int i = 0; i < 3; ++i)
				{
					const float *a = &m[9 * t + 3 * i], *b = &m[9 * t + 3 * ((i + 1) % 3)], *o = &m[9 * t + 3 * ((i + 2) % 3)];
					if (std::fabs(a[2] - b[2]) > 0.01f || o[2] > a[2] - 0.01f)
					{
						continue;
					}
					bool split = false;
					std::pair<std::array<int, 6>, size_t> first = {key(t, i, (i + 1) % 3), 0};
					for (auto it = std::lower_bound(sides.begin(), sides.end(), first); it != sides.end() && it->first == first.first; ++it)
					{
						split |= it->second != t && std::fabs(normal[it->second].Dot(n)) > 0.99f;
					}
					if (split)
					{
						continue;
					}
					float z = (std::max)(a[2], b[2]);
					Vector q((a[0] + b[0]) * 0.5f, (a[1] + b[1]) * 0.5f, z - (std::min)(1.0f, (z - o[2]) * 0.5f));
					float side = 0.0f;
					for (float s : {1.0f, -1.0f})
					{
						Ray_t ray;
						CGameTrace tr;
						ray.Init(q + n * (2.0f * s), q - n * (2.0f * s));
						et->ClipRayToCollideable(ray, MASK_PLAYERSOLID, c, &tr);
						// This face: not another one of the prop in front of it.
						bool onFace = std::fabs(n.Dot(tr.endpos - Vector(a[0], a[1], a[2]))) < 0.1f;
						if (tr.fraction < 1.0f && !tr.startsolid && onFace && tr.plane.normal.Dot(n) * s > 0.7f)
						{
							side = s;
							break;
						}
					}
					if (side != 0.0f)
					{
						const float fn[2] = {n.x / len * side, n.y / len * side};
						AddEdge(edges, a, b, z, fn, n.z / len * side, true, false, true);
					}
				}
			}
		}

		// prop_dynamic entities that block movement.
		class PropEntities : public IEntityEnumerator
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
				if (cls && (!std::strcmp(cls, "prop_dynamic") || !std::strcmp(cls, "prop_dynamic_override")))
				{
					found.push_back(static_cast<IServerUnknown *>(ent)->GetCollideable());
				}
				return true;
			}
		};

		void PropEdges(IEngineTrace *et, std::vector<WallEdge> &edges)
		{
			std::vector<float> m;
			for (int i = 0; i < BSPProps::RtCount(); ++i)
			{
				ICollideable *c = BSPProps::StaticProp(i);
				if (c && c->GetSolid() != SOLID_NONE && !(c->GetSolidFlags() & FSOLID_NOT_SOLID))
				{
					BSPProps::CollisionMesh(c, m);
					MeshTopEdges(et, c, m, edges);
				}
			}
			float mn[3], mx[3];
			if (!BSPData::CModelBounds(0, mn, mx))
			{
				return;
			}
			PropEntities ents;
			et->EnumerateEntities(Vector(mn[0], mn[1], mn[2]), Vector(mx[0], mx[1], mx[2]), &ents);
			for (ICollideable *c : ents.found)
			{
				if (c)
				{
					BSPProps::CollisionMesh(c, m);
					MeshTopEdges(et, c, m, edges);
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
			s_wallFaces.clear();
			s_edgeEnds.clear();
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
			PropEdges(et, edges);

			// A flush face can cover only part of a seam edge, a few units at its end:
			// cut the edge where those faces start and end, so even a short flush stretch gets a point of its own.
			// A bottom edge's are top edges at its height, a top edge's bottom edges, a shrunk top edge's bottom edges up to 1u above it.
			// Not a displacement floor's side or wall top's end: no face of the plane.
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
					// A shrunk top edge's are 0.49 to 1.01u above it: on a leaning plane, under the keys of that whole stretch.
					auto lo = PlaneKey(e, e.shrunk ? 0.49f : 0.0f);
					int d0 = std::get<2>(lo), d1 = std::get<2>(PlaneKey(e, e.shrunk ? 1.01f : 0.0f));
					std::vector<int> coplanar;
					for (int d = (std::min)(d0, d1); d <= (std::max)(d0, d1); ++d)
					{
						auto it = onPlane.find({std::get<0>(lo), std::get<1>(lo), d});
						if (it != onPlane.end())
						{
							coplanar.insert(coplanar.end(), it->second.begin(), it->second.end());
						}
					}
					for (int j : coplanar)
					{
						const WallEdge &t = edges[j];
						float up = t.z - e.z;
						if (t.top == e.top || t.floor || (e.shrunk ? up < 0.49f || up > 1.01f : std::fabs(up) > 0.01f))
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
					// So does a face on the plane that crosses the edge's height: a pillar through a trim.
					auto crossing = s_wallFaces.find(FaceKey(e.fn, e.lean, e.a[0], e.a[1], e.z));
					if (crossing != s_wallFaces.end())
					{
						for (const WallFace &f : crossing->second)
						{
							if (f.zMin > e.z - 0.01f || f.zMax < e.z + 0.01f)
							{
								continue;
							}
							for (size_t k = 0; k < f.w.size(); ++k)
							{
								const Vector &p = f.w[k], &q = f.w[(k + 1) % f.w.size()];
								if (p.z == q.z || (p.z - e.z) * (q.z - e.z) > 0.0f)
								{
									continue;
								}
								Vector x = p + (q - p) * ((e.z - p.z) / (q.z - p.z));
								float at = (x.x - e.a[0]) * dir[0] + (x.y - e.a[1]) * dir[1];
								if (at > 0.5f && at < span - 0.5f)
								{
									cuts.push_back(at);
								}
							}
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
						SampleSpan(et, e, cuts[k], cuts[k + 1], spacing, i, ownAbove || e.prop);
					}
				}
			}
			s_wallFaces.clear();
			s_edgePoints.insert(s_edgePoints.end(), s_edgeEnds.begin(), s_edgeEnds.end());
			s_edgeEnds.clear();
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
