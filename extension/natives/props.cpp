#include "natives.h"
#include "bsp_props.h"
#include <vector>

// Runtime (post-combine) static props (BSPProps via IStaticPropMgr).
cell_t N_RtStaticPropCount(IPluginContext *, const cell_t *)
{
	return BSPProps::RtCount();
}

cell_t N_RtStaticPropBounds(IPluginContext *pCtx, const cell_t *params)
{
	float mins[3] = {0, 0, 0}, maxs[3] = {0, 0, 0};
	bool ok = BSPProps::RtBounds(params[1], mins, maxs);
	float3_to_cell(pCtx, params[2], mins);
	float3_to_cell(pCtx, params[3], maxs);
	return ok ? 1 : 0;
}

cell_t N_RtStaticPropOrigin(IPluginContext *pCtx, const cell_t *params)
{
	float v[3] = {0, 0, 0};
	bool ok = BSPProps::RtOrigin(params[1], v);
	float3_to_cell(pCtx, params[2], v);
	return ok ? 1 : 0;
}

cell_t N_RtStaticPropAngles(IPluginContext *pCtx, const cell_t *params)
{
	float v[3] = {0, 0, 0};
	bool ok = BSPProps::RtAngles(params[1], v);
	float3_to_cell(pCtx, params[2], v);
	return ok ? 1 : 0;
}

cell_t N_RtStaticPropSolid(IPluginContext *, const cell_t *params)
{
	return BSPProps::RtSolid(params[1]);
}

cell_t N_RtStaticPropSolidFlags(IPluginContext *, const cell_t *params)
{
	return BSPProps::RtSolidFlags(params[1]);
}

cell_t N_RtStaticPropModelName(IPluginContext *pCtx, const cell_t *params)
{
	int maxlen = out_len(params[3]);
	if (maxlen <= 0)
	{
		return 0;
	}
	std::vector<char> tmp(maxlen);
	int n = BSPProps::RtModelName(params[1], tmp.data(), maxlen);
	pCtx->StringToLocal(params[2], maxlen, tmp.data());
	return n;
}

cell_t N_StaticPropAtRay(IPluginContext *pCtx, const cell_t *params)
{
	float start[3], end[3];
	cell_to_float3(pCtx, params[1], start);
	cell_to_float3(pCtx, params[2], end);
	return BSPProps::PropAtRay(start, end);
}

cell_t N_StaticPropProbe(IPluginContext *pCtx, const cell_t *params)
{
	int maxlen = out_len(params[3]);
	if (maxlen <= 0)
	{
		return 0;
	}
	std::vector<char> tmp(maxlen);
	int n = BSPProps::ProbeMesh(params[1], tmp.data(), maxlen);
	pCtx->StringToLocal(params[2], maxlen, tmp.data());
	return n;
}

cell_t N_StaticPropTriCount(IPluginContext *, const cell_t *params)
{
	return BSPProps::TriCount(params[1]);
}

cell_t N_StaticPropTri(IPluginContext *pCtx, const cell_t *params)
{
	float v0[3] = {0, 0, 0}, v1[3] = {0, 0, 0}, v2[3] = {0, 0, 0};
	bool ok = BSPProps::Triangle(params[1], params[2], v0, v1, v2);
	float3_to_cell(pCtx, params[3], v0);
	float3_to_cell(pCtx, params[4], v1);
	float3_to_cell(pCtx, params[5], v2);
	return ok ? 1 : 0;
}

cell_t N_StaticPropNearestTri(IPluginContext *pCtx, const cell_t *params)
{
	float pos[3];
	cell_to_float3(pCtx, params[1], pos);
	int propIdx = -1;
	float normal[3] = {0, 0, 0}, v0[3] = {0, 0, 0}, v1[3] = {0, 0, 0}, v2[3] = {0, 0, 0};
	float dist = BSPProps::NearestTri(pos, sp_ctof(params[2]), propIdx, normal, v0, v1, v2);
	cell_t *pProp;
	pCtx->LocalToPhysAddr(params[3], &pProp);
	*pProp = propIdx;
	float3_to_cell(pCtx, params[4], normal);
	float3_to_cell(pCtx, params[5], v0);
	float3_to_cell(pCtx, params[6], v1);
	float3_to_cell(pCtx, params[7], v2);
	return sp_ftoc(dist);
}

cell_t N_StaticPropHullSweep(IPluginContext *pCtx, const cell_t *params)
{
	EnsureLumpsLoaded();
	float start[3], end[3], mins[3], maxs[3];
	cell_to_float3(pCtx, params[1], start);
	cell_to_float3(pCtx, params[2], end);
	cell_to_float3(pCtx, params[3], mins);
	cell_to_float3(pCtx, params[4], maxs);
	float refZ = sp_ctof(params[5]);
	float frac = 1.0f, endpos[3] = {0, 0, 0}, normal[3] = {0, 0, 0};
	bool startSolid = false;
	int rc = BSPProps::HullSweep(start, end, mins, maxs, refZ, frac, endpos, normal, startSolid);
	cell_t *pFrac, *pSS;
	pCtx->LocalToPhysAddr(params[6], &pFrac);
	pCtx->LocalToPhysAddr(params[9], &pSS);
	*pFrac = sp_ftoc(frac);
	float3_to_cell(pCtx, params[7], endpos);
	float3_to_cell(pCtx, params[8], normal);
	*pSS = startSolid ? 1 : 0;
	return rc;
}

cell_t N_StaticPropDebug(IPluginContext *pCtx, const cell_t *params)
{
	EnsureLumpsLoaded();
	int maxlen = out_len(params[2]);
	if (maxlen <= 0)
	{
		return 0;
	}
	std::vector<char> tmp(maxlen);
	BSPProps::Debug(tmp.data(), maxlen);
	return string_to_cell(pCtx, params[1], maxlen, tmp.data());
}

cell_t N_StaticPropTraceHull(IPluginContext *pCtx, const cell_t *params)
{
	EnsureLumpsLoaded();
	float start[3], end[3], mins[3], maxs[3];
	cell_to_float3(pCtx, params[2], start);
	cell_to_float3(pCtx, params[3], end);
	cell_to_float3(pCtx, params[4], mins);
	cell_to_float3(pCtx, params[5], maxs);
	float frac = 1.0f, endpos[3] = {0, 0, 0}, normal[3] = {0, 0, 0};
	bool startSolid = false;
	int rc = BSPProps::TraceHull(params[1], start, end, mins, maxs, frac, endpos, normal, startSolid);
	cell_t *pFrac, *pSS;
	pCtx->LocalToPhysAddr(params[6], &pFrac);
	pCtx->LocalToPhysAddr(params[9], &pSS);
	*pFrac = sp_ftoc(frac);
	float3_to_cell(pCtx, params[7], endpos);
	float3_to_cell(pCtx, params[8], normal);
	*pSS = startSolid ? 1 : 0;
	return rc;
}

cell_t N_WorldTraceHull(IPluginContext *pCtx, const cell_t *params)
{
	float start[3], end[3], mins[3], maxs[3];
	cell_to_float3(pCtx, params[1], start);
	cell_to_float3(pCtx, params[2], end);
	cell_to_float3(pCtx, params[3], mins);
	cell_to_float3(pCtx, params[4], maxs);
	int mask = params[5];
	bool hitEntities = params[6] != 0;

	float frac = 1.0f, endpos[3] = {0, 0, 0}, normal[3] = {0, 0, 0};
	float planeDist = 0.0f;
	bool startSolid = false, allSolid = false;
	int contents = 0, dispFlags = 0, surfProps = 0, surfFlags = 0, hitType = 0;

	int nameLen = out_len(params[19]);
	std::vector<char> nameBuf(nameLen > 0 ? nameLen : 1);
	nameBuf[0] = '\0';

	int rc = BSPProps::WorldTraceHull(start, end, mins, maxs, mask, hitEntities, frac, endpos, normal, startSolid, allSolid, contents, dispFlags,
									  planeDist, surfProps, surfFlags, hitType, nameBuf.data(), nameLen);

	cell_t *pFrac, *pStartSolid, *pAllSolid, *pContents, *pDispFlags, *pPlaneDist, *pSurfProps, *pSurfFlags, *pHitType;
	pCtx->LocalToPhysAddr(params[7], &pFrac);
	pCtx->LocalToPhysAddr(params[10], &pStartSolid);
	pCtx->LocalToPhysAddr(params[11], &pAllSolid);
	pCtx->LocalToPhysAddr(params[12], &pContents);
	pCtx->LocalToPhysAddr(params[13], &pDispFlags);
	pCtx->LocalToPhysAddr(params[14], &pPlaneDist);
	pCtx->LocalToPhysAddr(params[15], &pSurfProps);
	pCtx->LocalToPhysAddr(params[16], &pSurfFlags);
	pCtx->LocalToPhysAddr(params[17], &pHitType);

	*pFrac = sp_ftoc(frac);
	float3_to_cell(pCtx, params[8], endpos);
	float3_to_cell(pCtx, params[9], normal);
	*pStartSolid = startSolid ? 1 : 0;
	*pAllSolid = allSolid ? 1 : 0;
	*pContents = contents;
	*pDispFlags = dispFlags;
	*pPlaneDist = sp_ftoc(planeDist);
	*pSurfProps = surfProps;
	*pSurfFlags = surfFlags;
	*pHitType = hitType;
	if (nameLen > 0)
	{
		pCtx->StringToLocal(params[18], nameLen, nameBuf.data());
	}

	return rc;
}

cell_t N_SurfacePropsReady(IPluginContext *pCtx, const cell_t *params)
{
	return BSPProps::SurfacePropsReady() ? 1 : 0;
}

cell_t N_SurfacePropCount(IPluginContext *pCtx, const cell_t *params)
{
	return BSPProps::SurfacePropCount();
}

cell_t N_SurfacePropName(IPluginContext *pCtx, const cell_t *params)
{
	int maxlen = out_len(params[3]);
	if (maxlen <= 0)
	{
		return 0;
	}
	std::vector<char> tmp(maxlen);
	int n = BSPProps::SurfacePropName(params[1], tmp.data(), maxlen);
	pCtx->StringToLocal(params[2], maxlen, tmp.data());
	return n;
}

cell_t N_SurfacePropIndex(IPluginContext *pCtx, const cell_t *params)
{
	char *name = nullptr;
	pCtx->LocalToString(params[1], &name);
	return BSPProps::SurfacePropIndex(name);
}

cell_t N_SurfacePropAddress(IPluginContext *pCtx, const cell_t *params)
{
	return static_cast<cell_t>(reinterpret_cast<uintptr_t>(BSPProps::SurfacePropAddress(params[1])));
}

cell_t N_SurfacePropData(IPluginContext *pCtx, const cell_t *params)
{
	float friction, elasticity, density, thickness, dampening, maxSpeed, jump;
	int material;
	bool climbable;
	bool ok = BSPProps::SurfacePropData(params[1], friction, elasticity, density, thickness, dampening, maxSpeed, jump, material, climbable);
	cell_t *pFric, *pElas, *pDens, *pThick, *pDamp, *pSpeed, *pJump, *pMat, *pClimb;
	pCtx->LocalToPhysAddr(params[2], &pFric);
	pCtx->LocalToPhysAddr(params[3], &pElas);
	pCtx->LocalToPhysAddr(params[4], &pDens);
	pCtx->LocalToPhysAddr(params[5], &pThick);
	pCtx->LocalToPhysAddr(params[6], &pDamp);
	pCtx->LocalToPhysAddr(params[7], &pSpeed);
	pCtx->LocalToPhysAddr(params[8], &pJump);
	pCtx->LocalToPhysAddr(params[9], &pMat);
	pCtx->LocalToPhysAddr(params[10], &pClimb);
	*pFric = sp_ftoc(friction);
	*pElas = sp_ftoc(elasticity);
	*pDens = sp_ftoc(density);
	*pThick = sp_ftoc(thickness);
	*pDamp = sp_ftoc(dampening);
	*pSpeed = sp_ftoc(maxSpeed);
	*pJump = sp_ftoc(jump);
	*pMat = material;
	*pClimb = climbable ? 1 : 0;
	return ok ? 1 : 0;
}

cell_t N_SurfacePropDump(IPluginContext *pCtx, const cell_t *params)
{
	int maxlen = out_len(params[3]);
	if (maxlen <= 0)
	{
		return 0;
	}
	std::vector<char> tmp(maxlen);
	BSPProps::SurfacePropDump(params[1], tmp.data(), maxlen);
	return string_to_cell(pCtx, params[2], maxlen, tmp.data());
}

extern const sp_nativeinfo_t g_PropNatives[] = {
	// Runtime (post-combine) static props (engine)
	{"BSP_RtStaticPropCount", N_RtStaticPropCount},
	{"BSP_RtStaticPropBounds", N_RtStaticPropBounds},
	{"BSP_RtStaticPropOrigin", N_RtStaticPropOrigin},
	{"BSP_RtStaticPropAngles", N_RtStaticPropAngles},
	{"BSP_RtStaticPropSolid", N_RtStaticPropSolid},
	{"BSP_RtStaticPropSolidFlags", N_RtStaticPropSolidFlags},
	{"BSP_RtStaticPropModelName", N_RtStaticPropModelName},
	{"BSP_StaticPropAtRay", N_StaticPropAtRay},
	{"BSP_StaticPropProbe", N_StaticPropProbe},
	{"BSP_StaticPropTriCount", N_StaticPropTriCount},
	{"BSP_StaticPropTri", N_StaticPropTri},
	{"BSP_StaticPropNearestTri", N_StaticPropNearestTri},

	// Prop collision queries (engine)
	{"BSP_StaticPropTraceHull", N_StaticPropTraceHull},
	{"BSP_StaticPropHullSweep", N_StaticPropHullSweep},
	{"BSP_StaticPropDebug", N_StaticPropDebug},

	// Unified world trace
	{"BSP_TraceHull", N_WorldTraceHull},

	// Surface physics properties (friction / surfaceprop)
	{"BSP_SurfacePropsReady", N_SurfacePropsReady},
	{"BSP_SurfacePropCount", N_SurfacePropCount},
	{"BSP_SurfacePropName", N_SurfacePropName},
	{"BSP_SurfacePropIndex", N_SurfacePropIndex},
	{"BSP_SurfacePropData", N_SurfacePropData},
	{"BSP_SurfacePropAddress", N_SurfacePropAddress},
	{"BSP_SurfacePropDump", N_SurfacePropDump},

	{nullptr, nullptr},
};
