#include "bsp_props.h"
#include "bsp_lumps.h"
#include "bsp_util.h"

#include "bspflags.h"        // MASK_SOLID, MASK_PLAYERSOLID
#include "const.h"           // SolidType_t
#include "mathlib/mathlib.h" // Vector, QAngle, matrix3x4_t

#include "engine/ICollideable.h"
#include "engine/IEngineTrace.h"
#include "engine/IStaticPropMgr.h"
#include "engine/ivmodelinfo.h"
#include "gametrace.h" // CGameTrace (trace_t) -> pulls cmodel.h/CBaseTrace (Ray_t)
#include "tier1/utlvector.h"
#include "vcollide.h"
#include "vphysics_interface.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace BSPProps
{

	namespace
	{

		IVModelInfo *g_modelinfo = nullptr;
		IPhysicsCollision *g_physcollision = nullptr;
		IEngineTrace *g_enginetrace = nullptr;
		IStaticPropMgrServer *g_staticpropmgr = nullptr;
		IPhysicsSurfaceProps *g_physprops = nullptr;

		// Byte offsets of surfacedata_t::game.* in the live struct.
		// The hl2sdk-csgo header mislocates `game` (CSGO's audio block is larger and surfacegameprops_t has two extra floats before `material`),
		// so the SDK struct reads maxSpeedFactor/jumpFactor as 0.
		// The physics block at +0x00 is correct. Overridable via gamedata (decimal).
		int OFF_SD_MAXSPEED = 0x50;
		int OFF_SD_JUMP = 0x54;
		int OFF_SD_MATERIAL = 0x60;
		int OFF_SD_CLIMBABLE = 0x62;

		typedef void *(*CreateInterfaceFn)(const char *name, int *returnCode);

		// Pull the live IPhysicsSurfaceProps singleton from the loaded vphysics module's CreateInterface export.
		// These are process singletons, so it's the same instance the engine/server use.
		IPhysicsSurfaceProps *ResolveSurfaceProps()
		{
			CreateInterfaceFn fn = nullptr;
#ifdef _WIN32
			HMODULE mod = GetModuleHandleA("vphysics.dll");
			if (mod)
			{
				fn = reinterpret_cast<CreateInterfaceFn>(GetProcAddress(mod, "CreateInterface"));
			}
#else
			// RTLD_NOLOAD: only succeed if vphysics is already mapped (always, on a running server).
			// dlclose balances the implicit refcount bump.
			void *mod = dlopen("vphysics.so", RTLD_NOW | RTLD_NOLOAD);
			if (mod)
			{
				fn = reinterpret_cast<CreateInterfaceFn>(dlsym(mod, "CreateInterface"));
				dlclose(mod);
			}
#endif
			if (!fn)
			{
				return nullptr;
			}
			return reinterpret_cast<IPhysicsSurfaceProps *>(fn(VPHYSICS_SURFACEPROPS_INTERFACE_VERSION, nullptr));
		}

		// model path -> vcollide_t* (engine-owned, not freed here).
		// Props commonly share a model, so cache per name.
		// A cached nullptr means "resolved, no collide".
		std::unordered_map<std::string, vcollide_t *> g_vcollideCache;

		// Runtime (post-combine) static-prop count, cached per map. -1 = uncomputed.
		int g_rtPropCount = -1;

		// ICollideable* -> static-prop-manager index, built lazily per map.
		// Used to turn a trace-hit collideable back into the canonical prop index.
		std::unordered_map<ICollideable *, int> g_collToIndex;
		bool g_collMapBuilt = false;

		// World-space collision triangles per prop idx (9 floats per tri), built lazily.
		std::unordered_map<int, std::vector<float>> g_meshCache;

		// World + static props only.
		// Under TRACE_EVERYTHING props always hit without going through ShouldHitEntity, so returning false there drops dynamic ents.
		class WorldPropsFilter : public ITraceFilter
		{
		public:
			bool ShouldHitEntity(IHandleEntity *, int) override
			{
				return false;
			}

			TraceType_t GetTraceType() const override
			{
				return TRACE_EVERYTHING;
			}
		};

		// World, static props, AND brush-model entities.
		// Returning true keeps server entities (func_* brush models) on top of the always-on world + props.
		class AllFilter : public ITraceFilter
		{
		public:
			bool ShouldHitEntity(IHandleEntity *, int) override
			{
				return true;
			}

			TraceType_t GetTraceType() const override
			{
				return TRACE_EVERYTHING;
			}
		};

		vcollide_t *GetPropVCollide(int propIdx)
		{
			char name[256];
			if (BSPLumps::StaticPropModelName(propIdx, name, sizeof(name)) <= 0)
			{
				return nullptr;
			}
			auto it = g_vcollideCache.find(name);
			if (it != g_vcollideCache.end())
			{
				return it->second;
			}
			vcollide_t *vc = nullptr;
			const model_t *m = g_modelinfo->FindOrLoadModel(name);
			// FindOrLoadModel returns null or (model_t*)-1 for unloadable names (e.g. prop-combine placeholders).
			// GetVCollide on that sentinel crashes.
			if (m == nullptr || m == reinterpret_cast<const model_t *>(~uintptr_t(0)))
			{
				g_vcollideCache[name] = nullptr;
				return nullptr;
			}
			vc = g_modelinfo->GetVCollide(m);
			// solidCount is a 15-bit field, reject absurd values / null array so a bogus vcollide can't drive the trace loop off into garbage.
			if (vc && (vc->solidCount == 0 || vc->solidCount > 64 || !vc->solids))
			{
				vc = nullptr;
			}
			g_vcollideCache[name] = vc;
			return vc;
		}

	} // namespace

	// Resolve a server-module global interface pointer via gamedata.
	// The address IS the variable's location, so deref once.
	template<typename T>
	static T *ResolveGlobal(IGameConfig *gc, const char *key)
	{
		void *addr = nullptr;
		if (gc->GetAddress(key, &addr) && addr)
		{
			return *reinterpret_cast<T **>(addr);
		}
		return nullptr;
	}

	bool Init(IGameConfig *gc, char *err, size_t errLen)
	{
		g_modelinfo = nullptr;
		g_physcollision = nullptr;
		g_enginetrace = nullptr;
		g_staticpropmgr = nullptr;
		if (!gc)
		{
			std::snprintf(err, errLen, "no gameconf");
			return false;
		}
		g_modelinfo = ResolveGlobal<IVModelInfo>(gc, "modelinfo");
		g_physcollision = ResolveGlobal<IPhysicsCollision>(gc, "physcollision");
		g_enginetrace = ResolveGlobal<IEngineTrace>(gc, "enginetrace");
		g_staticpropmgr = ResolveGlobal<IStaticPropMgrServer>(gc, "staticpropmgr");
		// Stays null (friction natives disabled) if vphysics isn't mapped yet. SurfaceProps() retries lazily.
		g_physprops = ResolveSurfaceProps();

		// surfacedata_t::game.* offsets (see decl). Decimal in gamedata.
		OFF_SD_MAXSPEED = BSPUtil::GetKeyInt(gc, "surfacedata_maxspeedfactor", 0x50);
		OFF_SD_JUMP = BSPUtil::GetKeyInt(gc, "surfacedata_jumpfactor", 0x54);
		OFF_SD_MATERIAL = BSPUtil::GetKeyInt(gc, "surfacedata_material", 0x60);
		OFF_SD_CLIMBABLE = BSPUtil::GetKeyInt(gc, "surfacedata_climbable", 0x62);

		// Runtime prop-hull path (enginetrace + staticpropmgr) handles autocombine maps.
		// Vcollide path (modelinfo + physcollision) is the by-name fallback. Succeed if either pair resolved.
		bool sweepReady = g_enginetrace && g_staticpropmgr;
		bool vcollReady = g_modelinfo && g_physcollision;
		if (!sweepReady && !vcollReady)
		{
			std::snprintf(err, errLen,
						  "no interfaces resolved (modelinfo=%d physcollision=%d "
						  "enginetrace=%d staticpropmgr=%d)",
						  g_modelinfo != nullptr, g_physcollision != nullptr, g_enginetrace != nullptr, g_staticpropmgr != nullptr);
			return false;
		}
		return true;
	}

	bool Ready()
	{
		return g_modelinfo && g_physcollision;
	}

	bool SweepReady()
	{
		return g_enginetrace && g_staticpropmgr;
	}

	int Debug(char *buf, size_t buflen)
	{
		if (!buf || buflen == 0)
		{
			return 0;
		}
		// One deref each: a valid interface ptr -> vtable ptr. Garbage here = bad sig.
		uintptr_t mi = reinterpret_cast<uintptr_t>(g_modelinfo);
		uintptr_t pc = reinterpret_cast<uintptr_t>(g_physcollision);
		uintptr_t et = reinterpret_cast<uintptr_t>(g_enginetrace);
		uintptr_t sp = reinterpret_cast<uintptr_t>(g_staticpropmgr);
		uintptr_t etVt = et ? *reinterpret_cast<uintptr_t *>(et) : 0;
		uintptr_t spVt = sp ? *reinterpret_cast<uintptr_t *>(sp) : 0;
		uintptr_t pp = reinterpret_cast<uintptr_t>(g_physprops);
		return std::snprintf(buf, buflen,
							 "modelinfo=0x%zx physcollision=0x%zx | enginetrace=0x%zx vtbl=0x%zx | "
							 "staticpropmgr=0x%zx vtbl=0x%zx | physprops=0x%zx surfaces=%d | props=%d",
							 (size_t)mi, (size_t)pc, (size_t)et, (size_t)etVt, (size_t)sp, (size_t)spVt, (size_t)pp,
							 g_physprops ? g_physprops->SurfacePropCount() : 0, BSPLumps::StaticPropCount());
	}

	void OnMapClear()
	{
		g_vcollideCache.clear();
		g_rtPropCount = -1;
		g_collToIndex.clear();
		g_collMapBuilt = false;
		g_meshCache.clear();
	}

	void Shutdown()
	{
		g_vcollideCache.clear();
		g_modelinfo = nullptr;
		g_physcollision = nullptr;
		g_enginetrace = nullptr;
		g_staticpropmgr = nullptr;
		g_physprops = nullptr;
	}

	namespace
	{
		// Lazy accessor: resolve on first use if Init ran before vphysics was mapped.
		IPhysicsSurfaceProps *SurfaceProps()
		{
			if (!g_physprops)
			{
				g_physprops = ResolveSurfaceProps();
			}
			return g_physprops;
		}
	} // namespace

	bool SurfacePropsReady()
	{
		return SurfaceProps() != nullptr;
	}

	int SurfacePropCount()
	{
		IPhysicsSurfaceProps *sp = SurfaceProps();
		return sp ? sp->SurfacePropCount() : 0;
	}

	int SurfacePropName(int idx, char *buf, int maxlen)
	{
		if (!buf || maxlen <= 0)
		{
			return 0;
		}
		buf[0] = '\0';
		IPhysicsSurfaceProps *sp = SurfaceProps();
		if (!sp || idx < 0 || idx >= sp->SurfacePropCount())
		{
			return 0;
		}
		const char *name = sp->GetPropName(idx);
		if (!name)
		{
			return 0;
		}
		int n = 0;
		while (n < maxlen - 1 && name[n])
		{
			buf[n] = name[n];
			++n;
		}
		buf[n] = '\0';
		return n;
	}

	int SurfacePropIndex(const char *name)
	{
		IPhysicsSurfaceProps *sp = SurfaceProps();
		if (!sp || !name)
		{
			return -1;
		}
		return sp->GetSurfaceIndex(name);
	}

	bool SurfacePropData(int idx, float &friction, float &elasticity, float &density, float &thickness, float &dampening, float &maxSpeedFactor,
						 float &jumpFactor, int &material, bool &climbable)
	{
		friction = elasticity = density = thickness = dampening = 0.0f;
		maxSpeedFactor = jumpFactor = 0.0f;
		material = 0;
		climbable = false;
		IPhysicsSurfaceProps *sp = SurfaceProps();
		if (!sp || idx < 0 || idx >= sp->SurfacePropCount())
		{
			return false;
		}
		surfacedata_t *d = sp->GetSurfaceData(idx);
		if (!d)
		{
			return false;
		}
		friction = d->physics.friction;
		elasticity = d->physics.elasticity;
		density = d->physics.density;
		thickness = d->physics.thickness;
		dampening = d->physics.dampening;
		// game.* via RE'd offsets, not the SDK struct (which mislocates `game`).
		const uint8_t *p = reinterpret_cast<const uint8_t *>(d);
		maxSpeedFactor = BSPUtil::ReadF32(p, OFF_SD_MAXSPEED);
		jumpFactor = BSPUtil::ReadF32(p, OFF_SD_JUMP);
		material = (int)BSPUtil::ReadU16(p, OFF_SD_MATERIAL);
		climbable = BSPUtil::ReadU8(p, OFF_SD_CLIMBABLE) != 0;
		return true;
	}

	int SurfacePropDump(int idx, char *buf, size_t buflen)
	{
		if (!buf || buflen == 0)
		{
			return 0;
		}
		buf[0] = '\0';
		IPhysicsSurfaceProps *sp = SurfaceProps();
		if (!sp || idx < 0 || idx >= sp->SurfacePropCount())
		{
			return 0;
		}
		surfacedata_t *d = sp->GetSurfaceData(idx);
		if (!d)
		{
			return 0;
		}
		const uint8_t *p = reinterpret_cast<const uint8_t *>(d);
		const char *name = sp->GetPropName(idx);
		// 0xC0 bytes covers physics+audio+sounds+game+soundhandles on any layout.
		int off = std::snprintf(buf, buflen, "surfacedata_t[%d] '%s' (friction=+0):\n", idx, name ? name : "?");
		for (int o = 0; o + 4 <= 0xC0 && off < (int)buflen - 1; o += 4)
		{
			uint32_t u;
			float f;
			std::memcpy(&u, p + o, 4);
			std::memcpy(&f, p + o, 4);
			off += std::snprintf(buf + off, buflen - off, "  +0x%02X: %08X  f=%g\n", o, u, f);
		}
		return off;
	}

	int HullSweep(const float start[3], const float end[3], const float mins[3], const float maxs[3], float refZ, float &outFraction,
				  float outEndPos[3], float outNormal[3], bool &outStartSolid)
	{
		outFraction = 1.0f;
		outStartSolid = false;
		outEndPos[0] = end[0];
		outEndPos[1] = end[1];
		outEndPos[2] = end[2];
		outNormal[0] = outNormal[1] = outNormal[2] = 0.0f;
		if (!SweepReady())
		{
			return -1;
		}

		Vector vstart(start[0], start[1], start[2]);
		Vector vend(end[0], end[1], end[2]);
		Vector vmins(mins[0], mins[1], mins[2]);
		Vector vmaxs(maxs[0], maxs[1], maxs[2]);

		// Swept-hull AABB: every static prop the moving box could touch.
		Vector boxMin, boxMax;
		for (int i = 0; i < 3; ++i)
		{
			float lo = (start[i] < end[i] ? start[i] : end[i]) + mins[i];
			float hi = (start[i] > end[i] ? start[i] : end[i]) + maxs[i];
			boxMin[i] = lo;
			boxMax[i] = hi;
		}

		CUtlVector<ICollideable *> props;
		g_staticpropmgr->GetAllStaticPropsInAABB(boxMin, boxMax, &props);
		if (props.Count() == 0)
		{
			return 0;
		}

		Ray_t ray;
		ray.Init(vstart, vend, vmins, vmaxs);

		// Each prop is clipped independently, so the box can contact several.
		// Pick the contact whose surface (endpos.z + maxs.z) is NEAREST refZ, not the lowest.
		float bestDelta = 1.0e30f;
		bool haveHit = false;
		for (int i = 0; i < props.Count(); ++i)
		{
			ICollideable *c = props[i];
			if (!c)
			{
				continue;
			}
			CGameTrace tr;
			tr.fraction = 1.0f;
			tr.startsolid = false;
			tr.allsolid = false;
			g_enginetrace->ClipRayToCollideable(ray, MASK_PLAYERSOLID, c, &tr);
			if (tr.fraction >= 1.0f && !tr.startsolid)
			{
				continue; // this prop wasn't touched
			}
			float surfaceZ = tr.endpos.z + maxs[2];
			float delta = surfaceZ - refZ;
			if (delta < 0.0f)
			{
				delta = -delta;
			}
			if (delta < bestDelta)
			{
				bestDelta = delta;
				haveHit = true;
				outFraction = tr.fraction;
				outStartSolid = tr.startsolid;
				outEndPos[0] = tr.endpos.x;
				outEndPos[1] = tr.endpos.y;
				outEndPos[2] = tr.endpos.z;
				outNormal[0] = tr.plane.normal.x;
				outNormal[1] = tr.plane.normal.y;
				outNormal[2] = tr.plane.normal.z;
			}
		}
		return haveHit ? 1 : 0;
	}

	int WorldTraceHull(const float start[3], const float end[3], const float mins[3], const float maxs[3], int mask, bool hitEntities,
					   float &outFraction, float outEndPos[3], float outNormal[3], bool &outStartSolid, bool &outAllSolid, int &outContents,
					   int &outDispFlags, float &outPlaneDist, int &outSurfaceProps, int &outSurfaceFlags, int &outHitType, char *outSurfName,
					   int surfNameLen)
	{
		outFraction = 1.0f;
		outEndPos[0] = end[0];
		outEndPos[1] = end[1];
		outEndPos[2] = end[2];
		outNormal[0] = outNormal[1] = outNormal[2] = 0.0f;
		outStartSolid = false;
		outAllSolid = false;
		outContents = 0;
		outDispFlags = 0;
		outPlaneDist = 0.0f;
		outSurfaceProps = 0;
		outSurfaceFlags = 0;
		outHitType = 0;
		if (outSurfName && surfNameLen > 0)
		{
			outSurfName[0] = '\0';
		}

		if (!g_enginetrace)
		{
			return -1;
		}

		Vector vstart(start[0], start[1], start[2]);
		Vector vend(end[0], end[1], end[2]);
		Vector vmins(mins[0], mins[1], mins[2]);
		Vector vmaxs(maxs[0], maxs[1], maxs[2]);
		Ray_t ray;
		ray.Init(vstart, vend, vmins, vmaxs);

		CGameTrace tr;
		tr.fraction = 1.0f;
		tr.startsolid = false;
		tr.allsolid = false;
		if (hitEntities)
		{
			AllFilter filter;
			g_enginetrace->TraceRay(ray, mask, &filter, &tr);
		}
		else
		{
			WorldPropsFilter filter;
			g_enginetrace->TraceRay(ray, mask, &filter, &tr);
		}

		outFraction = tr.fraction;
		outEndPos[0] = tr.endpos.x;
		outEndPos[1] = tr.endpos.y;
		outEndPos[2] = tr.endpos.z;
		outNormal[0] = tr.plane.normal.x;
		outNormal[1] = tr.plane.normal.y;
		outNormal[2] = tr.plane.normal.z;
		outPlaneDist = tr.plane.dist;
		outStartSolid = tr.startsolid;
		outAllSolid = tr.allsolid;
		outContents = tr.contents;
		outDispFlags = tr.dispFlags;
		outSurfaceProps = tr.surface.surfaceProps;
		outSurfaceFlags = tr.surface.flags;

		bool hit = (tr.fraction < 1.0f) || tr.startsolid || tr.allsolid;
		if (hit)
		{
			if (tr.dispFlags != 0)
			{
				outHitType = 2; // displacement
			}
			else if (tr.surface.name && std::strcmp(tr.surface.name, "**studio**") == 0)
			{
				outHitType = 3; // static prop
			}
			else
			{
				outHitType = 1; // world brush or brush-model entity
			}
		}

		if (outSurfName && surfNameLen > 0)
		{
			const char *name = tr.surface.name ? tr.surface.name : "";
			int n = 0;
			while (n < surfNameLen - 1 && name[n])
			{
				outSurfName[n] = name[n];
				++n;
			}
			outSurfName[n] = '\0';
		}

		return hit ? 1 : 0;
	}

	int TraceHull(int propIdx, const float start[3], const float end[3], const float mins[3], const float maxs[3], float &outFraction,
				  float outEndPos[3], float outNormal[3], bool &outStartSolid)
	{
		outFraction = 1.0f;
		outStartSolid = false;
		outEndPos[0] = end[0];
		outEndPos[1] = end[1];
		outEndPos[2] = end[2];
		outNormal[0] = outNormal[1] = outNormal[2] = 0.0f;

		if (!Ready())
		{
			return -1;
		}
		vcollide_t *vc = GetPropVCollide(propIdx);
		if (!vc || vc->solidCount == 0 || !vc->solids)
		{
			return -1;
		}

		float po[3], pa[3];
		if (!BSPLumps::StaticPropOrigin(propIdx, po) || !BSPLumps::StaticPropAngles(propIdx, pa))
		{
			return -1;
		}

		Vector vstart(start[0], start[1], start[2]);
		Vector vend(end[0], end[1], end[2]);
		Vector vmins(mins[0], mins[1], mins[2]);
		Vector vmaxs(maxs[0], maxs[1], maxs[2]);
		Vector vorg(po[0], po[1], po[2]);
		QAngle vang(pa[0], pa[1], pa[2]); // QAngle(pitch, yaw, roll)

		// Sweep against each convex solid, keep the earliest contact.
		// OR start-solid across solids (box began inside any piece).
		float bestFrac = 1.0f;
		bool haveHit = false;
		for (int i = 0; i < vc->solidCount; ++i)
		{
			CPhysCollide *collide = vc->solids[i];
			if (!collide)
			{
				continue;
			}
			CGameTrace tr;
			tr.fraction = 1.0f;
			tr.startsolid = false;
			tr.allsolid = false;
			g_physcollision->TraceBox(vstart, vend, vmins, vmaxs, collide, vorg, vang, &tr);
			if (tr.startsolid)
			{
				outStartSolid = true;
			}
			if (tr.fraction < bestFrac)
			{
				bestFrac = tr.fraction;
				haveHit = true;
				outEndPos[0] = tr.endpos.x;
				outEndPos[1] = tr.endpos.y;
				outEndPos[2] = tr.endpos.z;
				outNormal[0] = tr.plane.normal.x;
				outNormal[1] = tr.plane.normal.y;
				outNormal[2] = tr.plane.normal.z;
			}
		}
		outFraction = bestFrac;
		return haveHit ? 1 : 0;
	}

	// Runtime (post-combine) static props, via IStaticPropMgr + ICollideable
	int RtCount()
	{
		if (!g_staticpropmgr)
		{
			return 0;
		}
		if (g_rtPropCount < 0)
		{
			CUtlVector<ICollideable *> props;
			g_staticpropmgr->GetAllStaticProps(&props);
			g_rtPropCount = props.Count();
		}
		return g_rtPropCount;
	}

	namespace
	{
		ICollideable *RtProp(int idx)
		{
			if (!g_staticpropmgr || idx < 0 || idx >= RtCount())
			{
				return nullptr;
			}
			return g_staticpropmgr->GetStaticPropByIndex(idx);
		}

		void EnsureCollMap()
		{
			if (g_collMapBuilt)
			{
				return;
			}
			g_collToIndex.clear();
			int n = RtCount();
			for (int i = 0; i < n; ++i)
			{
				ICollideable *c = g_staticpropmgr->GetStaticPropByIndex(i);
				if (c)
				{
					g_collToIndex[c] = i;
				}
			}
			g_collMapBuilt = true;
		}
	} // namespace

	bool RtBounds(int idx, float outMins[3], float outMaxs[3])
	{
		ICollideable *c = RtProp(idx);
		if (!c)
		{
			return false;
		}
		Vector vmins, vmaxs;
		c->WorldSpaceSurroundingBounds(&vmins, &vmaxs);
		outMins[0] = vmins.x;
		outMins[1] = vmins.y;
		outMins[2] = vmins.z;
		outMaxs[0] = vmaxs.x;
		outMaxs[1] = vmaxs.y;
		outMaxs[2] = vmaxs.z;
		return true;
	}

	bool RtOrigin(int idx, float out[3])
	{
		ICollideable *c = RtProp(idx);
		if (!c)
		{
			return false;
		}
		const Vector &o = c->GetCollisionOrigin();
		out[0] = o.x;
		out[1] = o.y;
		out[2] = o.z;
		return true;
	}

	bool RtAngles(int idx, float out[3])
	{
		ICollideable *c = RtProp(idx);
		if (!c)
		{
			return false;
		}
		const QAngle &a = c->GetCollisionAngles();
		out[0] = a.x;
		out[1] = a.y;
		out[2] = a.z;
		return true;
	}

	int RtSolid(int idx)
	{
		ICollideable *c = RtProp(idx);
		return c ? (int)c->GetSolid() : -1;
	}

	int RtSolidFlags(int idx)
	{
		ICollideable *c = RtProp(idx);
		return c ? c->GetSolidFlags() : -1;
	}

	int RtModelName(int idx, char *buf, int maxlen)
	{
		if (!buf || maxlen <= 0)
		{
			return 0;
		}
		buf[0] = '\0';
		ICollideable *c = RtProp(idx);
		if (!c || !g_modelinfo)
		{
			return 0;
		}
		const model_t *m = c->GetCollisionModel();
		if (!m)
		{
			return 0;
		}
		const char *name = g_modelinfo->GetModelName(m);
		if (!name)
		{
			return 0;
		}
		int n = 0;
		while (n < maxlen - 1 && name[n])
		{
			buf[n] = name[n];
			++n;
		}
		buf[n] = '\0';
		return n;
	}

	int PropAtRay(const float start[3], const float end[3])
	{
		if (!g_enginetrace || !g_staticpropmgr)
		{
			return -1;
		}
		Vector vstart(start[0], start[1], start[2]);
		Vector vend(end[0], end[1], end[2]);
		Ray_t ray;
		ray.Init(vstart, vend);

		// Trace world + static props to find where (and whether) a prop is hit.
		WorldPropsFilter filter;
		CGameTrace tr;
		tr.fraction = 1.0f;
		g_enginetrace->TraceRay(ray, MASK_SOLID, &filter, &tr);
		if (!(tr.fraction < 1.0f && tr.surface.name && std::strcmp(tr.surface.name, "**studio**") == 0))
		{
			return -1; // hit a brush or nothing
		}

		// Identify the prop: clip the same ray against each prop near the hit point (trace.hitbox is NOT the staticpropmgr index).
		// Earliest contact wins.
		Vector hp(tr.endpos.x, tr.endpos.y, tr.endpos.z);
		Vector bmin(hp.x - 8.0f, hp.y - 8.0f, hp.z - 8.0f);
		Vector bmax(hp.x + 8.0f, hp.y + 8.0f, hp.z + 8.0f);
		CUtlVector<ICollideable *> cands;
		g_staticpropmgr->GetAllStaticPropsInAABB(bmin, bmax, &cands);
		ICollideable *best = nullptr;
		float bestFrac = 2.0f;
		for (int i = 0; i < cands.Count(); ++i)
		{
			ICollideable *c = cands[i];
			if (!c)
			{
				continue;
			}
			CGameTrace ct;
			ct.fraction = 1.0f;
			g_enginetrace->ClipRayToCollideable(ray, MASK_SOLID, c, &ct);
			if (ct.fraction < bestFrac)
			{
				bestFrac = ct.fraction;
				best = c;
			}
		}
		if (!best)
		{
			return -1;
		}

		EnsureCollMap();
		auto it = g_collToIndex.find(best);
		return (it != g_collToIndex.end()) ? it->second : -1;
	}

	int ProbeMesh(int idx, char *buf, size_t buflen)
	{
		if (!buf || buflen == 0)
		{
			return 0;
		}
		buf[0] = '\0';
		ICollideable *c = RtProp(idx);
		if (!c)
		{
			return std::snprintf(buf, buflen, "prop %d: no collideable (rtCount=%d)", idx, RtCount());
		}
		const model_t *m = c->GetCollisionModel();
		int solid = (int)c->GetSolid();
		const char *mdl = (m && g_modelinfo) ? g_modelinfo->GetModelName(m) : nullptr;
		vcollide_t *vc = (m && g_modelinfo) ? g_modelinfo->GetVCollide(m) : nullptr;
		int solidCount = vc ? (int)vc->solidCount : -1;
		// Run CreateDebugMesh on the first solid to confirm the full mesh pipeline.
		int vertCount = -1;
		if (vc && vc->solidCount > 0 && vc->solids && vc->solids[0] && g_physcollision)
		{
			Vector *verts = nullptr;
			vertCount = g_physcollision->CreateDebugMesh(vc->solids[0], &verts);
			if (verts)
			{
				g_physcollision->DestroyDebugMesh(vertCount, verts);
			}
		}
		return std::snprintf(buf, buflen,
							 "prop %d: model=%p solid=%d vcollide=%p solidCount=%d solid0verts=%d "
							 "tris=%d model='%s'",
							 idx, (void *)m, solid, (void *)vc, solidCount, vertCount, vertCount > 0 ? vertCount / 3 : 0, mdl ? mdl : "");
	}

	// Collision mesh (world-space triangles), via CreateDebugMesh

	namespace
	{

		void AppendTri(std::vector<float> &out, const Vector &a, const Vector &b, const Vector &c)
		{
			out.push_back(a.x);
			out.push_back(a.y);
			out.push_back(a.z);
			out.push_back(b.x);
			out.push_back(b.y);
			out.push_back(b.z);
			out.push_back(c.x);
			out.push_back(c.y);
			out.push_back(c.z);
		}

		// Build (and cache) prop idx's world-space collision triangles.
		// Returns nullptr only if the prop index is invalid, a prop with no geometry caches an empty list.
		const std::vector<float> *GetPropMesh(int idx)
		{
			auto it = g_meshCache.find(idx);
			if (it != g_meshCache.end())
			{
				return &it->second;
			}
			ICollideable *c = RtProp(idx);
			if (!c)
			{
				return nullptr;
			}

			std::vector<float> mesh;
			const matrix3x4_t &xform = c->CollisionToWorldTransform();
			const model_t *model = c->GetCollisionModel();
			vcollide_t *vc = (model && g_modelinfo) ? g_modelinfo->GetVCollide(model) : nullptr;
			bool built = false;
			if (vc && vc->solidCount > 0 && vc->solids && g_physcollision)
			{
				for (int s = 0; s < vc->solidCount; ++s)
				{
					CPhysCollide *col = vc->solids[s];
					if (!col)
					{
						continue;
					}
					Vector *verts = nullptr;
					int n = g_physcollision->CreateDebugMesh(col, &verts);
					if (verts && n >= 3)
					{
						for (int i = 0; i + 2 < n; i += 3)
						{
							Vector a, b, cc;
							VectorTransform(verts[i], xform, a);
							VectorTransform(verts[i + 1], xform, b);
							VectorTransform(verts[i + 2], xform, cc);
							AppendTri(mesh, a, b, cc);
						}
						built = true;
					}
					if (verts)
					{
						g_physcollision->DestroyDebugMesh(n, verts);
					}
				}
			}
			if (!built)
			{
				// No vcollide (SOLID_BBOX / non-physics): 12 triangles from the local OBB.
				const Vector &mn = c->OBBMins();
				const Vector &mx = c->OBBMaxs();
				Vector lc[8] = {Vector(mn.x, mn.y, mn.z), Vector(mx.x, mn.y, mn.z), Vector(mx.x, mx.y, mn.z), Vector(mn.x, mx.y, mn.z),
								Vector(mn.x, mn.y, mx.z), Vector(mx.x, mn.y, mx.z), Vector(mx.x, mx.y, mx.z), Vector(mn.x, mx.y, mx.z)};
				Vector w[8];
				for (int i = 0; i < 8; ++i)
				{
					VectorTransform(lc[i], xform, w[i]);
				}
				static const int f[12][3] = {{0, 2, 1}, {0, 3, 2}, {4, 5, 6}, {4, 6, 7}, {0, 1, 5}, {0, 5, 4},
											 {1, 2, 6}, {1, 6, 5}, {2, 3, 7}, {2, 7, 6}, {3, 0, 4}, {3, 4, 7}};
				for (int i = 0; i < 12; ++i)
				{
					AppendTri(mesh, w[f[i][0]], w[f[i][1]], w[f[i][2]]);
				}
			}

			auto &stored = g_meshCache[idx];
			stored = std::move(mesh);
			return &stored;
		}

		// Squared distance from p to triangle abc + the closest point (Ericson, RTCD).
		float ClosestPtTriSq(const Vector &p, const Vector &a, const Vector &b, const Vector &c, Vector &out)
		{
			Vector ab = b - a, ac = c - a, ap = p - a;
			float d1 = ab.Dot(ap), d2 = ac.Dot(ap);
			if (d1 <= 0.0f && d2 <= 0.0f)
			{
				out = a;
				return (p - a).LengthSqr();
			}
			Vector bp = p - b;
			float d3 = ab.Dot(bp), d4 = ac.Dot(bp);
			if (d3 >= 0.0f && d4 <= d3)
			{
				out = b;
				return (p - b).LengthSqr();
			}
			float vc = d1 * d4 - d3 * d2;
			if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f)
			{
				float v = d1 / (d1 - d3);
				out = a + ab * v;
				return (p - out).LengthSqr();
			}
			Vector cp = p - c;
			float d5 = ab.Dot(cp), d6 = ac.Dot(cp);
			if (d6 >= 0.0f && d5 <= d6)
			{
				out = c;
				return (p - c).LengthSqr();
			}
			float vb = d5 * d2 - d1 * d6;
			if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f)
			{
				float w = d2 / (d2 - d6);
				out = a + ac * w;
				return (p - out).LengthSqr();
			}
			float va = d3 * d6 - d5 * d4;
			if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f)
			{
				float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
				out = b + (c - b) * w;
				return (p - out).LengthSqr();
			}
			float denom = 1.0f / (va + vb + vc);
			float v = vb * denom, w = vc * denom;
			out = a + ab * v + ac * w;
			return (p - out).LengthSqr();
		}

	} // namespace

	int TriCount(int idx)
	{
		const std::vector<float> *m = GetPropMesh(idx);
		return m ? (int)(m->size() / 9) : 0;
	}

	bool Triangle(int idx, int triIdx, float v0[3], float v1[3], float v2[3])
	{
		const std::vector<float> *m = GetPropMesh(idx);
		if (!m)
		{
			return false;
		}
		if (triIdx < 0 || (size_t)(triIdx + 1) * 9 > m->size())
		{
			return false;
		}
		const float *p = &(*m)[(size_t)triIdx * 9];
		v0[0] = p[0];
		v0[1] = p[1];
		v0[2] = p[2];
		v1[0] = p[3];
		v1[1] = p[4];
		v1[2] = p[5];
		v2[0] = p[6];
		v2[1] = p[7];
		v2[2] = p[8];
		return true;
	}

	float NearestTri(const float pos[3], float maxDist, int &outPropIdx, float outNormal[3], float v0[3], float v1[3], float v2[3])
	{
		outPropIdx = -1;
		if (!g_staticpropmgr || !g_physcollision)
		{
			return -1.0f;
		}
		Vector p(pos[0], pos[1], pos[2]);
		Vector bmin(p.x - maxDist, p.y - maxDist, p.z - maxDist);
		Vector bmax(p.x + maxDist, p.y + maxDist, p.z + maxDist);
		CUtlVector<ICollideable *> cands;
		g_staticpropmgr->GetAllStaticPropsInAABB(bmin, bmax, &cands);
		if (cands.Count() == 0)
		{
			return -1.0f;
		}
		EnsureCollMap();

		float bestSq = maxDist * maxDist;
		bool found = false;
		Vector ba, bb, bc;
		for (int ci = 0; ci < cands.Count(); ++ci)
		{
			auto it = g_collToIndex.find(cands[ci]);
			if (it == g_collToIndex.end())
			{
				continue;
			}
			const std::vector<float> *m = GetPropMesh(it->second);
			if (!m)
			{
				continue;
			}
			size_t triN = m->size() / 9;
			for (size_t t = 0; t < triN; ++t)
			{
				const float *q = &(*m)[t * 9];
				Vector a(q[0], q[1], q[2]), b(q[3], q[4], q[5]), c(q[6], q[7], q[8]);
				Vector cp;
				float d2 = ClosestPtTriSq(p, a, b, c, cp);
				if (d2 < bestSq)
				{
					bestSq = d2;
					found = true;
					outPropIdx = it->second;
					ba = a;
					bb = b;
					bc = c;
				}
			}
		}
		if (!found)
		{
			return -1.0f;
		}
		v0[0] = ba.x;
		v0[1] = ba.y;
		v0[2] = ba.z;
		v1[0] = bb.x;
		v1[1] = bb.y;
		v1[2] = bb.z;
		v2[0] = bc.x;
		v2[1] = bc.y;
		v2[2] = bc.z;
		Vector n = (bb - ba).Cross(bc - ba);
		float len = n.Length();
		if (len > 1e-6f)
		{
			outNormal[0] = n.x / len;
			outNormal[1] = n.y / len;
			outNormal[2] = n.z / len;
		}
		else
		{
			outNormal[0] = outNormal[1] = outNormal[2] = 0.0f;
		}
		return std::sqrt(bestSq);
	}

} // namespace BSPProps
