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
#include "iservernetworkable.h"
#include "iserverunknown.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>
#include <tuple>
#include <vector>

namespace BSPSeam
{
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
		// back: how far behind where it would be the nearest face facing the same way is, unchanged if none.
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
			bool facing = tr.fraction < 1.0f && !tr.startsolid && tr.plane.normal.x * fn[0] + tr.plane.normal.y * fn[1] > 0.9f;
			if (back && facing)
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
			// A prop or entity face in front of the world's (or with none behind) gives back too.
			if (back && te.fraction < 1.0f && !te.startsolid && te.plane.normal.x * fn[0] + te.plane.normal.y * fn[1] > 0.9f
				&& (!facing || te.fraction < tr.fraction))
			{
				*back = expect - ((te.endpos.x - p[0]) * fn[0] + (te.endpos.y - p[1]) * fn[1] - h * (std::fabs(fn[0]) + std::fabs(fn[1])));
			}
			return flush(te) ? (EDGE_CONTINUES | EDGE_NONWORLD) : 0;
		}

		// lean: the face normal's z over its flat length, how far the face moves in per unit up.
		// shrunk: moved down or up with a vphysics brush entity's collision model (SettleEdge).
		// prop: a prop's top edge; a wall top kept only under a displacement, prop or entity (a prop under the world never hovers).
		// floor: a displacement floor side or wall top end, a ledge for pixelwalks only.
		// outBelow: the face under the top edge reaches out past it by more than float noise, leaning back too little for lean
		// (a displacement sheet whose top vbsp welded onto the wall above).
		// row: a level row inside a displacement; slope: how much a row's height changes per unit from a (z is its height at a).
		struct WallEdge
		{
			float a[2], c[2], z, fn[2], lean;
			bool top, shrunk, prop, ramp, floor, outBelow, row;
			float slope;
			bool sheetEnd; // a displacement row's end at the sheet's border, facing along the row: a hull meets the sheet edge-on
		};

		// Four float steps at the largest coordinate, at least 1e-4: vertices' float noise.
		float FloatNoise(float extent)
		{
			return (std::max)(1e-4f, 4.0f * (std::nextafter(extent, 2.0f * extent + 1.0f) - extent));
		}

		// Faces on one plane share it: flat normal to 1/1000, distance to 1/64u, up units above e (a leaning face moves in going up).
		std::tuple<int, int, int> PlaneKey(const WallEdge &e, float up = 0.0f)
		{
			float d = e.fn[0] * e.a[0] + e.fn[1] * e.a[1] - e.lean * up;
			return std::make_tuple((int)std::lround(e.fn[0] * 1000.0f), (int)std::lround(e.fn[1] * 1000.0f), (int)std::lround(d * 64.0f));
		}

		// Height to 1/2u, flat normal to 1/250.
		std::array<int, 3> NearKey(float z, float fx, float fy)
		{
			return {(int)std::lround(z * 2.0f), (int)std::lround(fx * 250.0f), (int)std::lround(fy * 250.0f)};
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

		// Nothing flush above the top edge: a hull over it, closing the last 1u to the face's plane,
		// can still meet something that holds it (the plane starting again higher up, an overhang), as a pixelsurf's wall.
		bool Resumes(IEngineTrace *et, const WallEdge &e, const float p[3], float ez)
		{
			float reach = 16.0f * (std::fabs(e.fn[0]) + std::fabs(e.fn[1]));
			Vector from(p[0] + e.fn[0] * (reach + 1.0f), p[1] + e.fn[1] * (reach + 1.0f), ez + 0.1f);
			// On to 0.5u past the plane: a face on it stops the hull 1/32u short.
			Vector to(p[0] + e.fn[0] * (reach - 0.5f), p[1] + e.fn[1] * (reach - 0.5f), ez + 0.1f);
			Ray_t hull;
			hull.Init(from, to, Vector(-16.0f, -16.0f, 0.0f), Vector(16.0f, 16.0f, 72.0f));
			MovementFilter movement;
			CGameTrace th;
			et->TraceRay(hull, MASK_PLAYERSOLID, &movement, &th);
			return th.startsolid || th.fraction < 1.0f;
		}

		// Samples e between s0 and s1 units along it from a. ownAbove: a bottom edge at this height covers the stretch (set for a prop's).
		// hair: e stands out past that bottom edge by more than float noise and under 1/64u (a ledge).
		// ends: END points may go at a (bit 0) and c (bit 1).
		void SampleSpan(IEngineTrace *et, const WallEdge &e, float s0, float s1, float spacing, int edge, bool ownAbove, bool hair, int ends)
		{
			float span = std::sqrt((e.c[0] - e.a[0]) * (e.c[0] - e.a[0]) + (e.c[1] - e.a[1]) * (e.c[1] - e.a[1]));
			float dir[2] = {(e.c[0] - e.a[0]) / span, (e.c[1] - e.a[1]) / span};
			// Above a shrunk top edge a shrunk brush starts up to 1u higher.
			float past = e.top ? (e.shrunk ? 1.5f : 1.0f) : -1.0f;
			int samples = (std::max)(1, (int)std::lround((s1 - s0) / spacing));
			for (int k = 0; k < samples; ++k)
			{
				float c0 = s0 + (s1 - s0) * k / samples, c1 = s0 + (s1 - s0) * (k + 1) / samples, at = (c0 + c1) * 0.5f;
				// A displacement row needn't be level.
				// On an axial face the hull meets it across its whole width, standing on its highest point there and meeting its lowest with its head:
				// both; else only the corner nearest the face meets it, off the point.
				bool axial = std::fabs(e.fn[0]) > 0.999f || std::fabs(e.fn[1]) > 0.999f;
				float corner = -16.0f * ((e.fn[0] > 0.0f ? dir[0] : -dir[0]) + (e.fn[1] > 0.0f ? dir[1] : -dir[1]));
				float lo = axial ? at - 16.0f : at + corner, hi = axial ? at + 16.0f : at + corner;
				float zs[2] = {e.z + e.slope * std::clamp(lo, 0.0f, span), e.z + e.slope * std::clamp(hi, 0.0f, span)};
				int nz = std::fabs(zs[1] - zs[0]) < 1.0f / 64.0f ? 1 : 2;
				if (nz == 1 && axial)
				{
					zs[0] = e.z + e.slope * at;
				}
				for (int zi = 0; zi < nz; ++zi)
				{
					float ez = zs[zi];
					float p[3] = {e.a[0] + dir[0] * at, e.a[1] + dir[1] * at, ez + past};
					// Solid in front, just past the edge: a floor or ceiling meets the face here.
					Vector front(p[0] + e.fn[0], p[1] + e.fn[1], p[2]);
					if (et->GetPointContents(front, MASK_PLAYERSOLID) & MASK_PLAYERSOLID)
					{
						continue;
					}
					int bits = (e.top ? EDGE_TOP : 0) | (e.row ? EDGE_ROW : 0);
					if (e.sheetEnd)
					{
						// The sheet goes on above and below, edge-on: no face for a probe to meet.
						bits |= EDGE_CONTINUES | EDGE_NONWORLD;
					}
					else if (e.ramp)
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
						// A face below under 1u tall: a covered top's seam is this edge's.
						if (!e.top && !(bits & EDGE_CONTINUES))
						{
							const float q[3] = {p[0], p[1], ez - 0.25f};
							bits |= Continuation(et, q, e.fn, e.lean * 0.25f);
						}
						if (hair)
						{
							bits = (bits & ~(EDGE_CONTINUES | EDGE_NONWORLD)) | EDGE_LEDGE;
						}
						// A ledge: the face above set back 0.25 to 1u.
						if (e.top && !(bits & EDGE_CONTINUES) && back > 0.25f && back < 1.0f)
						{
							bits |= EDGE_LEDGE;
						}
						if (e.top && !e.prop && !(bits & EDGE_CONTINUES) && Resumes(et, e, p, ez))
						{
							bits |= EDGE_RESUMES;
						}
						// A covered top edge's ends (exact pixelwalks), before the skip: the cover's bottom edge carrying the seam has other ends.
						if (zi == 0 && e.top && !e.prop && (bits & EDGE_CONTINUES))
						{
							for (int end = 0; end < 2; ++end)
							{
								if (!(ends & (1 << end)) || (end ? (k != samples - 1 || std::fabs(s1 - span) > 0.001f) : (k != 0 || s0 > 0.001f)))
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
						// A wall leaning back holds like a ramp, whatever is above (a brush on its own bevel).
						// Under its own bottom edge, which has the pixelsurf, only as a ramp.
						if (e.top && (e.lean > 0.001f || e.outBelow) && !(bits & EDGE_LEDGE))
						{
							bits |= EDGE_RAMP;
							if (ownAbove && !(bits & EDGE_NONWORLD))
							{
								bits &= ~EDGE_CONTINUES;
							}
						}
						// Under a world face only inside it, the brush above reaching lower: else that brush's bottom edge has the seam.
						if (e.top && !(bits & (EDGE_NONWORLD | EDGE_LEDGE | EDGE_RAMP | EDGE_RESUMES)) && (ownAbove || !(bits & EDGE_CONTINUES)))
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
							if (!Resumes(et, e, p, ez))
							{
								continue;
							}
							bits |= EDGE_RESUMES;
						}
					}
					// A shrunk bottom edge gets no pixelsurf test: the floor under it is lower, and has a top edge of its own.
					const float pt[EDGE_POINT_FLOATS] = {p[0],
														 p[1],
														 ez,
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
		}

		void AddEdge(std::vector<WallEdge> &edges, const float a[2], const float c[2], float z, const float fn[2], float lean, bool top, bool shrunk,
					 bool prop = false, bool ramp = false, bool floor = false, bool outBelow = false, bool row = false, float slope = 0.0f,
					 bool sheetEnd = false)
		{
			if ((c[0] - a[0]) * (c[0] - a[0]) + (c[1] - a[1]) * (c[1] - a[1]) < 1.0f)
			{
				return;
			}
			edges.push_back({{a[0], a[1]}, {c[0], c[1]}, z, {fn[0], fn[1]}, lean, top, shrunk, prop, ramp, floor, outBelow, row, slope, sheetEnd});
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
		// more thick along its normal (SDK 2013 ivp.cpp).
		// What borders open space isn't in the BSP: find where movement meets e, with box traces against that entity alone (rays meet the brush).
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

		// Entities of these classes that block movement, static props aside, in enumeration order.
		class SolidEntities : public IEntityEnumerator
		{
		public:
			SolidEntities(std::initializer_list<const char *> classes) : m_classes(classes) {}

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
				if (cls && std::any_of(m_classes.begin(), m_classes.end(), [cls](const char *c) { return !std::strcmp(cls, c); }))
				{
					found.push_back(static_cast<IServerUnknown *>(ent)->GetCollideable());
				}
				return true;
			}

		private:
			std::vector<const char *> m_classes;
		};

		void EntityEdges(IEngineTrace *et, std::vector<WallEdge> &edges)
		{
			IVModelInfo *mi = BSPProps::ModelInfo();
			float mn[3], mx[3];
			if (!mi || !BSPData::CModelBounds(0, mn, mx))
			{
				return;
			}
			// Brush entities that stay put, or stay put until used, broken or touched.
			SolidEntities ents({"func_brush", "func_wall", "func_wall_toggle", "func_button", "func_breakable", "func_door", "func_movelinear"});
			et->EnumerateEntities(Vector(mn[0], mn[1], mn[2]), Vector(mx[0], mx[1], mx[2]), &ents);
			for (ICollideable *c : ents.found)
			{
				if (!c || (c->GetSolid() != SOLID_BSP && c->GetSolid() != SOLID_VPHYSICS))
				{
					continue;
				}
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
		// Kept under a brush face too, unless that brush's bottom edge is on the same plane at the same height (a leaning one then only as a ramp).
		// Wall edges are listed on the sheet's back too where a standing hull fits behind it.
		// For pixelwalks: such floor sides (facing out) and wall top ends (facing along: a hull over the sheet stands on its top
		// under a wall within 1u, and rows inside a face leaning back.
		// Rows inside any other wall, and the sheet's bottom border, as bottom edges: the triangles above and below meet there.
		// Rows and borders may slope up to 0.3; a row's ends at the sheet's border face along it.
		void DispEdges(IEngineTrace *et, std::vector<WallEdge> &edges)
		{
			std::vector<float> tris;
			for (int d = 0; d < BSPDisp::EngineCount(); ++d)
			{
				if (!(BSPDisp::EngineGetContents(d) & MASK_PLAYERSOLID) || !BSPDisp::EngineTriangles(d, tris))
				{
					continue;
				}
				size_t first = edges.size();
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
				// A corner only one wall top side has is the top's end; one only one row side has, the row's.
				std::map<std::array<float, 3>, std::vector<std::array<float, 3>>> tops, rows;
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
						int u = uses[key(t, i, (i + 1) % 3)];
						if (!floor && u == 2)
						{
							// A row inside the wall, its third corner under it: leaning back, a ramp's top; else a bottom edge.
							float hl = std::hypot(c[0] - a[0], c[1] - a[1]);
							if (hl >= 0.001f && std::fabs(c[2] - a[2]) <= 0.3f * hl && o[2] < (std::min)(a[2], c[2]) - 0.01f)
							{
								const float fn[2] = {n[0] / len, n[1] / len};
								bool back = n[2] > 0.001f * len;
								bool level = std::fabs(a[2] - c[2]) <= 0.01f;
								AddEdge(edges, a, c, level ? (std::max)(a[2], c[2]) : a[2], fn, n[2] / len, back, false, false, back, false, false,
										true, level ? 0.0f : (c[2] - a[2]) / hl);
								rows[{a[0], a[1], a[2]}].push_back({c[0], c[1], c[2]});
								rows[{c[0], c[1], c[2]}].push_back({a[0], a[1], a[2]});
							}
							continue;
						}
						if (!floor && !ramp && u == 1 && o[2] > (std::max)(a[2], c[2]) + 0.01f)
						{
							// The sheet's bottom border: a head meets a thin sheet's underside.
							float hl = std::hypot(c[0] - a[0], c[1] - a[1]);
							if (hl >= 0.001f && std::fabs(c[2] - a[2]) <= 0.3f * hl)
							{
								const float fn[2] = {n[0] / len, n[1] / len};
								bool level = std::fabs(a[2] - c[2]) <= 0.01f;
								AddEdge(edges, a, c, level ? (std::min)(a[2], c[2]) : a[2], fn, n[2] / len, false, false, false, false, false, false,
										false, level ? 0.0f : (c[2] - a[2]) / hl);
							}
							continue;
						}
						if (std::fabs(a[2] - c[2]) > 0.01f || (!floor && o[2] > a[2] - 0.01f) || u != 1)
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
						float extent =
							(std::max)({std::fabs(a[0]), std::fabs(a[1]), std::fabs(c[0]), std::fabs(c[1]), std::fabs(o[0]), std::fabs(o[1])});
						bool out = !ramp && (o[0] - a[0]) * fn[0] + (o[1] - a[1]) * fn[1] > FloatNoise(extent);
						AddEdge(edges, a, c, (std::max)(a[2], c[2]), fn, n[2] / len, true, false, false, ramp, false, out);
						if (!ramp)
						{
							tops[{a[0], a[1], a[2]}].push_back({c[0], c[1], c[2]});
							tops[{c[0], c[1], c[2]}].push_back({a[0], a[1], a[2]});
						}
					}
				}
				for (int pass = 0; pass < 2; ++pass)
				{
					for (const auto &[q, others] : pass ? rows : tops)
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
						if (pass)
						{
							AddEdge(edges, a, c, q[2], fn, 0.0f, false, false, false, false, false, false, true, 0.0f, true);
						}
						else
						{
							AddEdge(edges, a, c, q[2], fn, 0.0f, true, false, false, false, true);
						}
					}
				}
				for (size_t k = first, last = edges.size(); k < last; ++k)
				{
					WallEdge e = edges[k];
					if (e.floor || e.sheetEnd)
					{
						continue;
					}
					// Across, over or under the edge, out by the sheet's lean over a hull's height (an overhang from behind).
					float reach = 16.0f * (std::fabs(e.fn[0]) + std::fabs(e.fn[1])) + 1.0f + std::fabs(e.lean) * 72.0f;
					bool fits = false;
					for (float z : {e.z - 36.0f, e.z + 0.1f, e.z - 72.1f})
					{
						Vector behind((e.a[0] + e.c[0]) * 0.5f - e.fn[0] * reach, (e.a[1] + e.c[1]) * 0.5f - e.fn[1] * reach, z);
						Ray_t hull;
						hull.Init(behind, behind, Vector(-16.0f, -16.0f, 0.0f), Vector(16.0f, 16.0f, 72.0f));
						MovementFilter movement;
						CGameTrace tr;
						et->TraceRay(hull, MASK_PLAYERSOLID, &movement, &tr);
						fits = fits || !tr.startsolid;
					}
					// Open just behind the edge too: a sheet laid on a wall that ends there has the wall's edge.
					bool open = false;
					for (float z : {e.z + 0.5f, e.z - 0.5f})
					{
						Vector at((e.a[0] + e.c[0]) * 0.5f - e.fn[0], (e.a[1] + e.c[1]) * 0.5f - e.fn[1], z);
						Ray_t point;
						point.Init(at, at);
						MovementFilter movement;
						CGameTrace tr;
						et->TraceRay(point, MASK_PLAYERSOLID, &movement, &tr);
						open = open || !tr.startsolid;
					}
					if (!fits || !open)
					{
						continue;
					}
					const float fn[2] = {-e.fn[0], -e.fn[1]};
					// Its rows lean out from the back: bottom edges.
					bool top = e.top && !e.row;
					AddEdge(edges, e.a, e.c, e.z, fn, -e.lean, top, false, false, false, false, false, e.row, e.slope);
				}
			}
		}

		// Top edges of a prop mesh's wall and ramp faces:
		// a triangle side no coplanar triangle shares, third corner below; bottom edges of its wall faces likewise, third corner above.
		// Such a side may slope up to 0.3 (an arch's curve).
		// A level side two coplanar wall triangles share, the other above, is a row inside the face (ROW); a sloped one is the face's diagonal.
		// The winding isn't reliable and combined props hide faces inside: the front is the side a ray meets the face from;
		// neither side, inside.
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
				if (std::fabs(n.z) >= 0.7f || len < 0.5f)
				{
					continue;
				}
				for (int i = 0; i < 3; ++i)
				{
					const float *a = &m[9 * t + 3 * i], *b = &m[9 * t + 3 * ((i + 1) % 3)], *o = &m[9 * t + 3 * ((i + 2) % 3)];
					// The third corner above: a bottom edge.
					float hl = std::hypot(b[0] - a[0], b[1] - a[1]);
					bool level = std::fabs(a[2] - b[2]) <= 0.01f;
					bool bottom = o[2] > (std::max)(a[2], b[2]) + 0.01f;
					if ((!level && (hl < 0.001f || std::fabs(b[2] - a[2]) > 0.3f * hl)) || (!bottom && o[2] > (std::min)(a[2], b[2]) - 0.01f))
					{
						continue;
					}
					float slope = level ? 0.0f : (b[2] - a[2]) / hl;
					bool split = false;
					std::pair<std::array<int, 6>, size_t> first = {key(t, i, (i + 1) % 3), 0};
					for (auto it = std::lower_bound(sides.begin(), sides.end(), first); it != sides.end() && it->first == first.first; ++it)
					{
						split |= it->second != t && std::fabs(normal[it->second].Dot(n)) > 0.99f;
					}
					if ((bottom || !level) && split)
					{
						continue; // the row, from the triangle under it
					}
					float z = level ? (bottom ? (std::min)(a[2], b[2]) : (std::max)(a[2], b[2])) : a[2];
					float mz = (a[2] + b[2]) * 0.5f;
					Vector q((a[0] + b[0]) * 0.5f, (a[1] + b[1]) * 0.5f,
							 mz + (bottom ? 1.0f : -1.0f) * (std::min)(1.0f, std::fabs(mz - o[2]) * 0.5f));
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
					bool ramp = n.z * side > 0.3f;
					const float fn[2] = {n.x / len * side, n.y / len * side};
					if (bottom)
					{
						if (side != 0.0f && std::fabs(n.z) <= 0.3f)
						{
							AddEdge(edges, a, b, z, fn, n.z / len * side, false, false, true, false, false, false, false, slope);
						}
					}
					else if (split)
					{
						// A row inside the face, between coplanar triangles: a seam with the face going on above and below.
						if (side != 0.0f && std::fabs(n.z) <= 0.3f)
						{
							AddEdge(edges, a, b, z, fn, n.z / len * side, false, false, true, false, false, false, true, slope);
						}
					}
					else if (side != 0.0f && (ramp || std::fabs(n.z) <= 0.3f))
					{
						AddEdge(edges, a, b, z, fn, n.z / len * side, true, false, true, ramp, false, false, false, slope);
					}
				}
			}
		}

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
			SolidEntities ents({"prop_dynamic", "prop_dynamic_override"});
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
				DispEdges(et, edges);
			}
			PropEdges(et, edges);

			// A flush face can cover only part of a seam edge, a few units at its end:
			// cut the edge where those faces start and end, so even a short flush stretch gets a point of its own.
			// A bottom edge's are top edges at its height, a top edge's bottom edges, a shrunk top edge's bottom edges up to 1u above it.
			// Not floor or ramp edges: no wall face of the plane.
			std::map<std::tuple<int, int, int>, std::vector<int>> onPlane;
			// Faces meeting at an edge with normals a little apart can be far apart in distance: an unshrunk edge's by height and normal.
			std::map<std::array<int, 3>, std::vector<int>> nearBy;
			// Top edge ends by corner, 1/16u: where another top edge on the same line goes on, the top doesn't end.
			std::map<std::array<int, 3>, std::vector<int>> topEnds;
			auto endKey = [](const float q[2], float z)
			{ return std::array<int, 3> {(int)std::lround(q[0] * 16.0f), (int)std::lround(q[1] * 16.0f), (int)std::lround(z * 16.0f)}; };
			for (int i = 0; i < (int)edges.size(); ++i)
			{
				onPlane[PlaneKey(edges[i])].push_back(i);
				nearBy[NearKey(edges[i].z, edges[i].fn[0], edges[i].fn[1])].push_back(i);
				if (edges[i].top && !edges[i].floor)
				{
					topEnds[endKey(edges[i].a, edges[i].z)].push_back(i);
					topEnds[endKey(edges[i].c, edges[i].z)].push_back(i);
				}
			}
			auto endsAt = [&](int i)
			{
				const WallEdge &e = edges[i];
				float span = std::hypot(e.c[0] - e.a[0], e.c[1] - e.a[1]);
				float dir[2] = {(e.c[0] - e.a[0]) / span, (e.c[1] - e.a[1]) / span};
				int ends = 3;
				for (int end = 0; end < 2 && e.top; ++end)
				{
					for (int j : topEnds[endKey(end ? e.c : e.a, e.z)])
					{
						const WallEdge &t = edges[j];
						if (j == i || t.fn[0] * e.fn[0] + t.fn[1] * e.fn[1] <= 0.999f)
						{
							continue;
						}
						// Only one that goes on past the end: an overlapping or duplicate brush ending there too doesn't.
						for (const float *q : {t.a, t.c})
						{
							float at = (q[0] - e.a[0]) * dir[0] + (q[1] - e.a[1]) * dir[1];
							if (end ? at > span + 0.5f : at < -0.5f)
							{
								ends &= ~(1 << end);
							}
						}
					}
				}
				return ends;
			};
			for (int i = 0; i < (int)edges.size(); ++i)
			{
				const WallEdge &e = edges[i];
				float span = std::sqrt((e.c[0] - e.a[0]) * (e.c[0] - e.a[0]) + (e.c[1] - e.a[1]) * (e.c[1] - e.a[1]));
				std::vector<float> cuts = {0.0f, span};
				std::vector<std::array<float, 4>> above; // t's ends along e; how far e stands in front at each
				int ends = endsAt(i);
				if (e.top || !e.shrunk)
				{
					float dir[2] = {(e.c[0] - e.a[0]) / span, (e.c[1] - e.a[1]) / span};
					// A shrunk top edge's are 0.49 to 1.01u above it: on a leaning plane, under the keys of that whole stretch.
					auto lo = PlaneKey(e, e.shrunk ? 0.49f : 0.0f);
					int d0 = std::get<2>(lo), d1 = std::get<2>(PlaneKey(e, e.shrunk ? 1.01f : 0.0f));
					std::vector<int> coplanar;
					if (e.shrunk)
					{
						// One bucket either side: a distance on a bucket boundary splits on vertex noise.
						for (int d = (std::min)(d0, d1) - 1; d <= (std::max)(d0, d1) + 1; ++d)
						{
							auto it = onPlane.find({std::get<0>(lo), std::get<1>(lo), d});
							if (it != onPlane.end())
							{
								coplanar.insert(coplanar.end(), it->second.begin(), it->second.end());
							}
						}
					}
					else
					{
						// The buckets the tolerances below span.
						auto k0 = NearKey(e.z - 0.01f, e.fn[0] - 0.002f, e.fn[1] - 0.002f),
							 k1 = NearKey(e.z + 0.01f, e.fn[0] + 0.002f, e.fn[1] + 0.002f);
						for (int z = k0[0]; z <= k1[0]; ++z)
						{
							for (int x = k0[1]; x <= k1[1]; ++x)
							{
								for (int y = k0[2]; y <= k1[2]; ++y)
								{
									auto it = nearBy.find({z, x, y});
									if (it != nearBy.end())
									{
										coplanar.insert(coplanar.end(), it->second.begin(), it->second.end());
									}
								}
							}
						}
					}
					for (int j : coplanar)
					{
						const WallEdge &t = edges[j];
						float up = t.z - e.z;
						if (std::fabs(t.fn[0] - e.fn[0]) > 0.002f || std::fabs(t.fn[1] - e.fn[1]) > 0.002f || t.top == e.top || t.floor || t.ramp
							|| (e.shrunk ? up < 0.49f || up > 1.01f : std::fabs(up) > 0.01f))
						{
							continue;
						}
						// How far e stands in front of t's face at e's height, at t's ends: t's normal can be a little off e's.
						float at[2], front[2];
						for (int k = 0; k < 2; ++k)
						{
							const float *q = k ? t.c : t.a;
							at[k] = (q[0] - e.a[0]) * dir[0] + (q[1] - e.a[1]) * dir[1];
							front[k] = (e.a[0] - q[0]) * e.fn[0] + (e.a[1] - q[1]) * e.fn[1] - t.lean * up;
						}
						auto frontAt = [&](float s) { return front[0] + (front[1] - front[0]) * (s - at[0]) / (at[1] - at[0]); };
						float from = (std::max)(0.0f, (std::min)(at[0], at[1])), to = (std::min)(span, (std::max)(at[0], at[1]));
						if (!e.shrunk && (std::fabs(frontAt(from)) > 0.015625f || std::fabs(frontAt(to)) > 0.015625f))
						{
							continue;
						}
						for (int k = 0; k < 2; ++k)
						{
							if (at[k] > 0.5f && at[k] < span - 0.5f)
							{
								cuts.push_back(at[k]);
							}
						}
						if (e.top)
						{
							above.push_back({at[0], at[1], front[0], front[1]});
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
						// Out past it by more than the vertices' float noise: a ledge thinner than the plane key's 1/64u.
						float noise = FloatNoise((std::max)({std::fabs(e.a[0]), std::fabs(e.a[1]), std::fabs(e.c[0]), std::fabs(e.c[1])}));
						bool ownAbove = false, hair = false;
						for (const auto &r : above)
						{
							if ((std::min)(r[0], r[1]) <= mid && mid <= (std::max)(r[0], r[1]))
							{
								(r[2] + (r[3] - r[2]) * (mid - r[0]) / (r[1] - r[0]) > noise ? hair : ownAbove) = true;
							}
						}
						SampleSpan(et, e, cuts[k], cuts[k + 1], spacing, i, ownAbove || e.prop, hair && !ownAbove, ends);
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

	void OnMapEnd()
	{
		s_edgePoints.clear();
		s_edgeSpacing = 0.0f;
	}
} // namespace BSPSeam
