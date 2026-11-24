#include "bsp_seam.h"
#include "bsp_seam_filter.h"
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
#include "tier1/utlvector.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>
#include <vector>

namespace BSPSeam
{
	namespace
	{
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

		// A displacement hit on an axis-aligned normal reports that face's coordinate as dist: the sign is off on a negative axis.
		// A brush entity reports its model-space dist. A vphysics hit (a prop) reports the plane through the hull's origin,
		// far from the contact: then the face is taken 1/32 behind the contact, and own is false.
		float HitDist(const CGameTrace &tr, const Vector &mins, const Vector &maxs, bool *own = nullptr)
		{
			const Vector &n = tr.plane.normal;
			float c = 0.0f;
			for (int k = 0; k < 3; ++k)
			{
				c += n[k] * (tr.endpos[k] + (n[k] > 0.0f ? mins[k] : maxs[k]));
			}
			float d = tr.plane.dist;
			IHandleEntity *hit = reinterpret_cast<IHandleEntity *>(tr.m_pEnt);
			bool prop = !hit || (BSPProps::StaticPropMgr() && BSPProps::StaticPropMgr()->IsStaticProp(hit));
			ICollideable *ent = prop ? nullptr : static_cast<IServerUnknown *>(hit)->GetCollideable();
			if (ent && ent->GetSolid() == SOLID_BSP)
			{
				d += DotProduct(n, ent->GetCollisionOrigin());
			}
			bool axial = std::fabs(n.x) == 1.0f || std::fabs(n.y) == 1.0f || std::fabs(n.z) == 1.0f;
			if (tr.dispFlags && axial && std::fabs(c + d) < std::fabs(c - d))
			{
				d = -d;
			}
			bool plane = std::fabs(c - d) < 0.5f;
			if (own)
			{
				*own = plane;
			}
			return plane ? d : c - 0.03125f;
		}

		// AirAccelerate's step for this hull: ducked, the input is 0.34 of it;
		// CategorizePosition leaves friction 0.25 after a rise of 140 u/s or less, and the last tick moved at vz + halfG.
		float AirScale(const MoveTrace &mt, float vz, float halfG)
		{
			return (mt.maxs.z < 72.0f ? 0.34f : 1.0f) * ((vz + halfG > 0.0f && vz + halfG <= 140.0f) ? 0.25f : 1.0f);
		}

		bool Caught(MoveTrace &mt, const Vector &start, const float normal[3], float vin, float vz, float frametime, float gravity,
					float airaccelerate, float maxspeed, bool &wallNonAxial)
		{
			float vel[3] = {-normal[0] * vin, -normal[1] * vin, vz};
			float origin[3] = {start.x, start.y, start.z}, mn[3], mx[3];
			collide::MovementListBox(origin, vel, maxspeed, frametime, 18.0f, mn, mx);
			BSPProps::EngineTrace()->SetupLeafAndEntityListBox(Vector(mn[0], mn[1], mn[2]), Vector(mx[0], mx[1], mx[2]), mt.list);

			float halfG = gravity * 0.5f * frametime;
			float add = (std::min)((std::max)(30.0f - vin, 0.0f), airaccelerate * maxspeed * frametime * AirScale(mt, vz, halfG));
			Vector v(vel[0] - normal[0] * add, vel[1] - normal[1] * add, vz - halfG);
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

		// Not a start whose straight rise meets a ceiling (an overhang): a plain ceiling stops it.
		bool CaughtHead(MoveTrace &mt, const Vector &start, const float normal[3], float vin, float vz, float frametime, float gravity,
						float airaccelerate, float maxspeed, bool &wallNonAxial)
		{
			float vel[3] = {-normal[0] * vin, -normal[1] * vin, vz};
			float origin[3] = {start.x, start.y, start.z}, mn[3], mx[3];
			collide::MovementListBox(origin, vel, maxspeed, frametime, 18.0f, mn, mx);
			BSPProps::EngineTrace()->SetupLeafAndEntityListBox(Vector(mn[0], mn[1], mn[2]), Vector(mx[0], mx[1], mx[2]), mt.list);

			float halfG = gravity * 0.5f * frametime;
			float add = (std::min)((std::max)(30.0f - vin, 0.0f), airaccelerate * maxspeed * frametime * AirScale(mt, vz, halfG));
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
			return tr.startsolid || tr.fraction >= 1.0f || tr.plane.normal.z > -0.7f;
		}

		// Heights where c's collision changes inside the box mn..mx: its bounds (a vcollide's box stands 0.25 off its surface),
		// its collision mesh's corners, and a brush entity's brushes' tops and bottoms at its origin.
		void AddBounds(ICollideable *c, const float mn[3], const float mx[3], std::vector<float> &zs)
		{
			float z = c->GetCollisionOrigin().z, hi = z + c->OBBMaxs().z, lo = z + c->OBBMins().z;
			zs.insert(zs.end(), {hi, hi - 0.25f, lo, lo + 0.25f});
			const model_t *m = c->GetCollisionModel();
			const char *name = m && BSPProps::ModelInfo() ? BSPProps::ModelInfo()->GetModelName(m) : nullptr;
			if (c->GetSolid() == SOLID_BSP && name && name[0] == '*')
			{
				const Vector &o = c->GetCollisionOrigin();
				std::vector<int> brushes;
				ModelBrushes(std::atoi(name + 1), brushes);
				for (int b : brushes)
				{
					float bmn[3], bmx[3];
					if (BSPData::GetBrushBounds(b, bmn, bmx) && (BSPData::GetBrushContents(b) & MASK_PLAYERSOLID) && bmn[0] + o.x <= mx[0]
						&& bmx[0] + o.x >= mn[0] && bmn[1] + o.y <= mx[1] && bmx[1] + o.y >= mn[1])
					{
						zs.insert(zs.end(), {bmn[2] + o.z, bmx[2] + o.z});
					}
				}
				return;
			}
			std::vector<float> mesh;
			BSPProps::CollisionMesh(c, mesh);
			// Corners of triangles reaching over the box.
			for (size_t t = 0; t + 8 < mesh.size(); t += 9)
			{
				const float *v = &mesh[t];
				if ((std::max)({v[0], v[3], v[6]}) < mn[0] || (std::min)({v[0], v[3], v[6]}) > mx[0] || (std::max)({v[1], v[4], v[7]}) < mn[1]
					|| (std::min)({v[1], v[4], v[7]}) > mx[1])
				{
					continue;
				}
				zs.insert(zs.end(), {v[2], v[5], v[8]});
			}
		}

		// A world box brush with its bottom at z and a side on the face (axis, sign) that a hull at wallPos reaches along it.
		// A fall crossing that bottom in a tick that started higher, or past the box's top too, stops the same way:
		// only the bottom's own height is the seam.
		bool BoxBottomAt(int axis, float sign, float face, const float wallPos[3], float z)
		{
			int o = 1 - axis;
			for (int b = 0, count = BSPData::GetNumBrushes(); b < count; ++b)
			{
				float mn[3], mx[3];
				if (BSPData::BrushNumSides(b) > 0 || !(BSPData::GetBrushContents(b) & MASK_PLAYERSOLID) || !BSPData::GetBrushBounds(b, mn, mx)
					|| std::fabs(mn[2] - z) > 0.01f || std::fabs((sign > 0.0f ? mx[axis] : mn[axis]) - face) > 0.01f || mn[o] >= wallPos[o] + 16.0f
					|| mx[o] <= wallPos[o] - 16.0f)
				{
					continue;
				}
				return true;
			}
			return false;
		}

		class SolidBounds : public IEntityEnumerator
		{
		public:
			SolidBounds(const float mn[3], const float mx[3], std::vector<float> &zs) : m_mn(mn), m_mx(mx), m_zs(zs) {}

			bool EnumEntity(IHandleEntity *ent) override
			{
				ICollideable *c = BSPProps::EngineTrace()->GetCollideable(ent);
				if (c && c->GetSolid() != SOLID_NONE && !(c->GetSolidFlags() & FSOLID_NOT_SOLID))
				{
					AddBounds(c, m_mn, m_mx, m_zs);
				}
				return true;
			}

		private:
			const float *m_mn, *m_mx;
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
							if (head ? CaughtHead(mt, start, normal, vin, kCatchSpeeds[s], frametime, gravity, airaccelerate, maxspeed, nonAxial)
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
		float boxFace = NAN;
		bool boxBottom = false;
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
					// The first clear float step off, the closest a hug gets: at 0 the clip takes the hull as inside,
					// and in a coordinate-stop band (collisions §7.4) the first step rounds onto the face.
					bool own;
					float face = HitDist(hit, mt.mins, mt.maxs, &own) * sign;
					if (face != boxFace)
					{
						boxFace = face;
						boxBottom = own && BoxBottomAt(axis, sign, face, wallPos, edgeZ);
					}
					if (!boxBottom)
					{
						continue;
					}
					Vector start = hit.endpos;
					start[axis] = face + 16.0f * sign;
					CGameTrace at;
					bool clear = false;
					for (int k = 0; k < 4 && !clear; ++k)
					{
						start[axis] = std::nextafter(start[axis], start[axis] + 64.0f * sign);
						Ray_t still;
						still.Init(start, start, mt.mins, mt.maxs);
						BSPProps::EngineTrace()->TraceRay(still, MASK_PLAYERSOLID, &mt.filter, &at);
						clear = !at.startsolid;
					}
					if (!clear)
					{
						continue;
					}
					// Ground within 2u: CategorizePosition would have landed the player at the end of the tick before.
					{
						float fall[3] = {0.0f, 0.0f, -vz}, origin[3] = {start.x, start.y, start.z}, mn[3], mx[3];
						collide::MovementListBox(origin, fall, maxspeed, frametime, 18.0f, mn, mx);
						BSPProps::EngineTrace()->SetupLeafAndEntityListBox(Vector(mn[0], mn[1], mn[2]), Vector(mx[0], mx[1], mx[2]), mt.list);
						mt.Run(start, start - Vector(0.0f, 0.0f, 2.0f), at);
					}
					if (!at.startsolid && at.fraction < 1.0f && at.plane.normal.z >= 0.7f)
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
						// Movement builds its list box before gravity.
						float vel[3] = {-normal[0] * push, -normal[1] * push, -vm}, start0[3] = {vel[0], vel[1], -vz};
						float origin[3] = {start.x, start.y, start.z}, mn[3], mx[3];
						collide::MovementListBox(origin, start0, maxspeed, frametime, 18.0f, mn, mx);
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
		// Zero-length start solid is stuck (touching a cover), though the 2u trace may hit. On ground CategorizePosition traces
		// 2u + step size instead (server.so 0x7162B0), which can pass an edge the 2u trace stops on.
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
			mt.Run(rest, rest - Vector(0.0f, 0.0f, 20.0f), tr);
			groundZ = tr.endpos.z;
			return !tr.startsolid && tr.fraction < 1.0f && tr.plane.normal.z >= 0.7f && tr.endpos.z > rest.z - 0.1f;
		};
		// Exactly on a face the point test and the sweeps round its distance differently: every move can stop at once.
		// drop: a drop from 2u up must land there too (a prop's face only needs the rise).
		auto moves = [&](const Vector &p, bool drop)
		{
			Vector up(p.x, p.y, p.z + 2.0f), down(p.x, p.y, p.z - 2.0f);
			et->SetupLeafAndEntityListBox(down + mt.mins - pad, up + mt.maxs + pad, mt.list);
			CGameTrace tr;
			mt.Run(up, down, tr);
			if (drop && (tr.startsolid || tr.fraction <= 0.0f || tr.fraction >= 1.0f || tr.plane.normal.z < 0.7f))
			{
				return false;
			}
			mt.Run(p, up, tr);
			return !tr.startsolid && tr.fraction > 0.0f;
		};
		// From 1u up: a floor out there a bit higher than the strip starts a hull at its height solid.
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
			// The face the hull rests against, 1u up: lower, a ramp rising into the wall is in the way.
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
				// Leaning out under ~2.6°, or back up to a ramp.
				const Vector &hn = hit.plane.normal;
				float behind = DotProduct(Vector(wallPos[0], wallPos[1], wallPos[2]), hn) - HitDist(hit, mt.mins, mt.maxs);
				if (!hit.startsolid && hit.fraction < 1.0f && hn.x * n.x + hn.y * n.y > 0.999f * std::hypot(hn.x, hn.y) && hn.z > -0.045f
					&& hn.z < 0.7f && behind > -0.1f && behind < 1.0f)
				{
					found = true;
					break;
				}
			}
			if (!found)
			{
				continue;
			}
			// gap: the hull's nearest edge to the face's plane.
			const Vector &f = hit.plane.normal;
			// Not plane.type: it doesn't follow the normal on a box brush's hit.
			// A prop's face isn't a plane movement clips exactly: search it as a slanted one.
			bool own;
			float dist = HitDist(hit, mt.mins, mt.maxs, &own);
			int axis = !own ? -1 : (std::fabs(f.x) == 1.0f ? 0 : (std::fabs(f.y) == 1.0f ? 1 : -1));
			float sign = axis >= 0 && f[axis] < 0.0f ? -1.0f : 1.0f;
			float onFace = axis >= 0 ? (dist + 16.0f) * sign : 0.0f, groundZ;
			Vector p(hit.endpos.x, hit.endpos.y, z);
			// A face leaning out meets the hull's top edge.
			double pGap = (double)p.x * f.x + (double)p.y * f.y + (double)p.z * f.z - dist - 16.0 * (std::fabs(f.x) + std::fabs(f.y))
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
			// setposGap: where only a setpos stands it (exactly on an axial face; on a prop, closer than a creep stops).
			float creptGap, setposGap = 0.0f;
			if (axis >= 0)
			{
				// The first clear float step off: in a coordinate-stop band (collisions §7.4) the first one rounds onto the face.
				float c = onFace;
				for (int k = 0; k < 4; ++k)
				{
					c = std::nextafter(c, c + 64.0f * sign);
					creptGap = (c - onFace) * sign;
					if (clear(at(creptGap)))
					{
						break;
					}
				}
			}
			else
			{
				// Two float steps past the clear boundary: closer, list traces start solid at other heights.
				float lo = 0.0f, hi = 0.03125f;
				if (!clear(at(hi)))
				{
					continue;
				}
				for (int k = 0; k < 24 && at(lo) != at(hi); ++k)
				{
					float mid = (lo + hi) * 0.5f;
					(clear(at(mid)) ? hi : lo) = mid;
				}
				float c = (std::max)(std::fabs(p.x), std::fabs(p.y)), step = std::nextafter(c, 2.0f * c + 1.0f) - c;
				creptGap = hi + 2.0f * step;
				// Right outside a face leaning back it wins the ground trace's fraction-0 tie: from the first gap that stands.
				if (f.z > 0.001f && !stands(at(creptGap), groundZ))
				{
					float out = creptGap, last = creptGap;
					bool any = false;
					for (int k = 1; k <= 32 && !any; ++k)
					{
						last = out;
						out = creptGap + k / 1024.0f;
						any = stands(at(out), groundZ);
					}
					if (!any)
					{
						continue;
					}
					for (int k = 0; k < 16 && at(last) != at(out); ++k)
					{
						float mid = (last + out) * 0.5f;
						(stands(at(mid), groundZ) ? out : last) = mid;
					}
					creptGap = out + 2.0f * step;
				}
				// A creep against a vphysics face stops 0.9 ε off it (collisions §5.2).
				if (!own)
				{
					setposGap = creptGap;
					creptGap = (std::max)(creptGap, hi + 0.9f * 0.03125f);
				}
			}
			Vector crept = at(creptGap);
			int bit = duck ? CATCH_DUCKED : CATCH_STANDING;
			if (stands(at(1.0f), groundZ) || floorOut(at(1.0f)))
			{
				continue;
			}
			if (stands(crept, groundZ))
			{
				// The standing hull's origin: under an overhang the ducked one gets closer.
				if (!creep)
				{
					stands(crept, groundZ);
					outInfo[1] = groundZ;
					outInfo[2] = crept.x;
					outInfo[3] = crept.y;
				}
				creep |= bit;
				float lo = creptGap, hi = 1.0f;
				for (int k = 0; k < 16 && at(lo) != at(hi); ++k)
				{
					float mid = (lo + hi) * 0.5f, g;
					(stands(at(mid), g) ? lo : hi) = mid;
				}
				// On a face leaning back, across at the edge's height.
				outInfo[0] = (std::max)(outInfo[0], f.z > 0.0f ? (lo - f.z * 0.03125f) / (float)std::sqrt(h) : lo);
			}
			else if ((axis >= 0 || !own) && !creep && stands(at(setposGap), groundZ) && moves(at(setposGap), own))
			{
				if (!exact)
				{
					Vector on = at(setposGap);
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
				|| std::fabs((endPos[axis] - HitDist(hit, mt.mins, mt.maxs) * sign) * sign) > 0.1f)
			{
				continue;
			}
			bool own;
			float onFace = HitDist(hit, mt.mins, mt.maxs, &own) * sign + 16.0f * sign;
			if (!own)
			{
				continue;
			}
			auto origin = [&](float s, float gap)
			{
				Vector q(endPos[0] + along[0] * s, endPos[1] + along[1] * s, z);
				q[axis] = onFace + sign * gap;
				return q;
			};
			auto down = [&](const Vector &p, CGameTrace &tr)
			{
				Vector d(p.x, p.y, p.z - 2.0f);
				// 2u lower too, for the on-ground trace from where this one ends.
				et->SetupLeafAndEntityListBox(d - Vector(0.0f, 0.0f, 2.0f) + mt.mins - pad, p + mt.maxs + pad, mt.list);
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
					Vector at = origin(m, 0.0f);
					(at == origin(a, 0.0f) ? false : at == origin(b, 0.0f) || test(m)) ? b = m : a = m;
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
			if (stands)
			{
				// The on-ground trace, as in PixelwalkCatch.
				Vector rest = tr.endpos;
				mt.Run(rest, rest - Vector(0.0f, 0.0f, 20.0f), tr);
				stands = !tr.startsolid && tr.fraction < 1.0f && tr.plane.normal.z >= 0.7f && tr.endpos.z > rest.z - 0.1f;
			}
			if (stands)
			{
				// As PixelwalkCatch's exact class: a drop from 2u up lands, a 2u rise moves.
				Vector p = origin(mid, 0.0f), up(p.x, p.y, p.z + 2.0f), d(p.x, p.y, p.z - 2.0f);
				et->SetupLeafAndEntityListBox(d + mt.mins - pad, up + mt.maxs + pad, mt.list);
				mt.Run(up, d, tr);
				stands = !tr.startsolid && tr.fraction > 0.0f && tr.fraction < 1.0f && tr.plane.normal.z >= 0.7f;
				if (stands)
				{
					mt.Run(p, up, tr);
					stands = !tr.startsolid && tr.fraction > 0.0f;
				}
			}
			// Not a floor 1u into the wall either (the top past the cover), nor 1u out.
			CGameTrace tin, tout;
			down(origin(mid, -1.0f), tin);
			// As PixelwalkCatch's floorOut.
			{
				Vector o = origin(mid, 1.0f), up(o.x, o.y, o.z + 1.0f), d(o.x, o.y, o.z - 2.0f);
				et->SetupLeafAndEntityListBox(d + mt.mins - pad, up + mt.maxs + pad, mt.list);
				mt.Run(up, d, tout);
			}
			bool floorIn = !tin.startsolid && tin.fraction < 1.0f && tin.plane.normal.z >= 0.7f;
			bool floorOut = !tout.startsolid && tout.fraction < 1.0f && tout.plane.normal.z >= 0.7f;
			if (!stands || floorIn || floorOut)
			{
				continue;
			}
			if (!flags)
			{
				// The standing hull's strip where both stand.
				Vector m = origin(mid, 0.0f);
				out[0] = m.x;
				out[1] = m.y;
				out[2] = sb - sa;
			}
			flags |= duck ? CATCH_DUCKED : CATCH_STANDING;
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
				for (int s = 0, sides = BSPData::BrushNumSides(b); s < sides; ++s)
				{
					float n[3], d, v[3 * 64];
					if (BSPData::BrushSideBevel(b, s) || !BSPData::BrushSidePlane(b, s, n, d))
					{
						continue;
					}
					int nv = BSPData::BrushSideWinding(b, s, v, 64);
					// Its corners in the box: a thin or sloped brush's edges the column misses.
					for (int i = 0; i < nv; ++i)
					{
						if (v[3 * i] >= mn[0] && v[3 * i] <= mx[0] && v[3 * i + 1] >= mn[1] && v[3 * i + 1] <= mx[1])
						{
							zs.push_back(v[3 * i + 2]);
						}
					}
					// The top of a face leaning back the way the wall faces: where the wall rises from a ramp, outside the column.
					if (n[2] <= 0.001f || n[2] >= 0.7f || n[0] * normal[0] + n[1] * normal[1] < 0.999f * std::sqrt(n[0] * n[0] + n[1] * n[1]))
					{
						continue;
					}
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
					AddBounds(props[i], mn, mx, zs);
				}
			}
			SolidBounds entities(mn, mx, zs);
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
					// Down to neighbouring floats: the scan tests the height as found.
					for (int i = 0; i < 40; ++i)
					{
						float mid = (lo + hi) * 0.5f;
						if (mid <= lo || mid >= hi)
						{
							break;
						}
						if (same(low, probe(mid)))
						{
							lo = mid;
						}
						else
						{
							hi = mid;
						}
					}
					zs.push_back(hi);
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
