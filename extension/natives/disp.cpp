#include "natives.h"
#include "bsp_disp.h"

// Displacement queries (engine-first, disk fallback)
cell_t N_DispHeightAt(IPluginContext *pCtx, const cell_t *params)
{
	EnsureDispLoaded();
	return sp_ftoc(BSPDisp::HeightAt(sp_ctof(params[1]), sp_ctof(params[2])));
}

cell_t N_DispHeightAtDebug(IPluginContext *pCtx, const cell_t *params)
{
	EnsureDispLoaded();
	int idx = -1;
	float z = BSPDisp::HeightAtDebug(sp_ctof(params[1]), sp_ctof(params[2]), idx);
	cell_t *outIdx = nullptr;
	pCtx->LocalToPhysAddr(params[3], &outIdx);
	*outIdx = idx;
	return sp_ftoc(z);
}

cell_t N_DispSurfaceNormalAt(IPluginContext *pCtx, const cell_t *params)
{
	EnsureDispLoaded();
	float normal[3] = {0, 0, 1};
	float z = BSPDisp::SurfaceNormalAt(sp_ctof(params[1]), sp_ctof(params[2]), normal);
	float3_to_cell(pCtx, params[3], normal);
	return sp_ftoc(z);
}

cell_t N_DispIsPointOnDisp(IPluginContext *pCtx, const cell_t *params)
{
	EnsureDispLoaded();
	return BSPDisp::IsPointOnDisp(sp_ctof(params[1]), sp_ctof(params[2])) ? 1 : 0;
}

cell_t N_DispDistToSurface(IPluginContext *pCtx, const cell_t *params)
{
	EnsureDispLoaded();
	float pos[3];
	cell_to_float3(pCtx, params[1], pos);
	return sp_ftoc(BSPDisp::DistToSurface(pos, sp_ctof(params[2])));
}

cell_t N_DispTreeIndexAt(IPluginContext *pCtx, const cell_t *params)
{
	EnsureDispLoaded();
	float pos[3];
	cell_to_float3(pCtx, params[1], pos);
	return BSPDisp::TreeIndexAt(pos, sp_ctof(params[2]));
}

cell_t N_DispNearestTri(IPluginContext *pCtx, const cell_t *params)
{
	EnsureDispLoaded();
	float pos[3], normal[3], v0[3], v1[3], v2[3];
	cell_to_float3(pCtx, params[1], pos);
	float d = BSPDisp::DistNearestTri(pos, sp_ctof(params[2]), normal, v0, v1, v2);
	float3_to_cell(pCtx, params[3], normal);
	float3_to_cell(pCtx, params[4], v0);
	float3_to_cell(pCtx, params[5], v1);
	float3_to_cell(pCtx, params[6], v2);
	return sp_ftoc(d);
}

cell_t N_DispHeightAtMulti(IPluginContext *pCtx, const cell_t *params)
{
	EnsureDispLoaded();
	int maxResults = params[4];
	if (maxResults <= 0)
	{
		return 0;
	}
	if (maxResults > 256)
	{
		maxResults = 256;
	}
	float tmp[256];
	int n = BSPDisp::HeightAtMulti(sp_ctof(params[1]), sp_ctof(params[2]), tmp, maxResults);
	cell_t *outResults = nullptr;
	pCtx->LocalToPhysAddr(params[3], &outResults);
	for (int i = 0; i < n; ++i)
	{
		outResults[i] = sp_ftoc(tmp[i]);
	}
	return n;
}

// Displacement - engine accessors
cell_t N_DispReady(IPluginContext *, const cell_t *)
{
	return BSPDisp::EngineReady() ? 1 : 0;
}

cell_t N_DispCount(IPluginContext *, const cell_t *)
{
	return BSPDisp::EngineCount();
}

cell_t N_DispGetBounds(IPluginContext *pCtx, const cell_t *params)
{
	float mins[3], maxs[3];
	bool ok = BSPDisp::EngineGetBounds(params[1], mins, maxs);
	float3_to_cell_ok(pCtx, params[2], mins, ok);
	float3_to_cell_ok(pCtx, params[3], maxs, ok);
	return ok ? 1 : 0;
}

cell_t N_DispGetPower(IPluginContext *pCtx, const cell_t *params)
{
	return BSPDisp::EngineGetPower(params[1]);
}

cell_t N_DispGetContents(IPluginContext *pCtx, const cell_t *params)
{
	return BSPDisp::EngineGetContents(params[1]);
}

cell_t N_DispGetSurfaceProps(IPluginContext *pCtx, const cell_t *params)
{
	int props[5] = {0, 0, 0, 0, 0};
	bool ok = BSPDisp::EngineGetSurfaceProps(params[1], props);
	cell_t *outProps = nullptr;
	pCtx->LocalToPhysAddr(params[2], &outProps);
	for (int i = 0; i < 4; ++i)
	{
		outProps[i] = props[i];
	}
	// By-ref param, not a 5th array cell: older plugins pass a 4-cell array.
	if (params[0] >= 3)
	{
		cell_t *outAlpha = nullptr;
		pCtx->LocalToPhysAddr(params[3], &outAlpha);
		*outAlpha = props[4];
	}
	return ok ? 1 : 0;
}

cell_t N_DispVertCount(IPluginContext *pCtx, const cell_t *params)
{
	return BSPDisp::EngineVertCount(params[1]);
}

cell_t N_DispTriCount(IPluginContext *pCtx, const cell_t *params)
{
	return BSPDisp::EngineTriCount(params[1]);
}

cell_t N_DispGetVert(IPluginContext *pCtx, const cell_t *params)
{
	float pos[3] = {0, 0, 0};
	bool ok = BSPDisp::EngineGetVert(params[1], params[2], pos);
	float3_to_cell(pCtx, params[3], pos);
	return ok ? 1 : 0;
}

cell_t N_DispDebugInfo(IPluginContext *pCtx, const cell_t *params)
{
	int maxlen = out_len(params[3]);
	if (maxlen <= 0)
	{
		return 0;
	}
	char tmp[1024];
	BSPDisp::EngineDebugTreeInfo(params[1], tmp, sizeof(tmp));
	return string_to_cell(pCtx, params[2], maxlen, tmp);
}

cell_t N_DispDiagnoseQuery(IPluginContext *pCtx, const cell_t *params)
{
	int maxlen = out_len(params[4]);
	if (maxlen <= 0)
	{
		return 0;
	}
	char tmp[512];
	BSPDisp::EngineDiagnoseQuery(sp_ctof(params[1]), sp_ctof(params[2]), tmp, sizeof(tmp));
	return string_to_cell(pCtx, params[3], maxlen, tmp);
}

// Displacement - disk-only (explicit fallback access)
// Disk index == engine tree index.
cell_t N_DispDiskCount(IPluginContext *, const cell_t *)
{
	EnsureDispLoaded();
	return BSPDisp::DiskCount();
}

cell_t N_DispDiskBounds(IPluginContext *pCtx, const cell_t *params)
{
	EnsureDispLoaded();
	float mins[3], maxs[3];
	bool ok = BSPDisp::DiskGetBounds(params[1], mins, maxs);
	float3_to_cell_ok(pCtx, params[2], mins, ok);
	float3_to_cell_ok(pCtx, params[3], maxs, ok);
	return ok ? 1 : 0;
}

cell_t N_DispDiskDebugInfo(IPluginContext *pCtx, const cell_t *params)
{
	EnsureDispLoaded();
	int maxlen = out_len(params[3]);
	if (maxlen <= 0)
	{
		return 0;
	}
	char tmp[512];
	BSPDisp::DiskDebugDispInfo(params[1], tmp, sizeof(tmp));
	return string_to_cell(pCtx, params[2], maxlen, tmp);
}

extern const sp_nativeinfo_t g_DispNatives[] = {
	// Displacement - unified (engine-first, disk fallback)
	{"BSP_DispHeightAt", N_DispHeightAt},
	{"BSP_DispHeightAtDebug", N_DispHeightAtDebug},
	{"BSP_DispSurfaceNormalAt", N_DispSurfaceNormalAt},
	{"BSP_DispIsPointOnDisp", N_DispIsPointOnDisp},
	{"BSP_DispHeightAtMulti", N_DispHeightAtMulti},
	{"BSP_DispDistToSurface", N_DispDistToSurface},
	{"BSP_DispNearestTri", N_DispNearestTri},
	{"BSP_DispTreeIndexAt", N_DispTreeIndexAt},

	// Displacement - engine accessors
	{"BSP_DispReady", N_DispReady},
	{"BSP_DispCount", N_DispCount},
	{"BSP_DispGetBounds", N_DispGetBounds},
	{"BSP_DispGetPower", N_DispGetPower},
	{"BSP_DispGetContents", N_DispGetContents},
	{"BSP_DispGetSurfaceProps", N_DispGetSurfaceProps},
	{"BSP_DispVertCount", N_DispVertCount},
	{"BSP_DispTriCount", N_DispTriCount},
	{"BSP_DispGetVert", N_DispGetVert},
	{"BSP_DispDebugInfo", N_DispDebugInfo},
	{"BSP_DispDiagnoseQuery", N_DispDiagnoseQuery},

	// Displacement - disk-only
	{"BSP_DispDiskCount", N_DispDiskCount},
	{"BSP_DispDiskBounds", N_DispDiskBounds},
	{"BSP_DispDiskDebugInfo", N_DispDiskDebugInfo},

	{nullptr, nullptr},
};
