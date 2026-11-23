#include "natives.h"
#include "bsp_seam.h"
#include "bsp_overlay.h"
#include <vector>

static cell_t SeamCatch(IPluginContext *pCtx, const cell_t *params, bool head)
{
	float wall[3], normal[3], speed[2], start[6];
	cell_to_float3(pCtx, params[1], wall);
	cell_to_float3(pCtx, params[2], normal);
	int r = BSPSeam::Catch(wall, normal, sp_ctof(params[3]), sp_ctof(params[4]), sp_ctof(params[5]), sp_ctof(params[6]), sp_ctof(params[7]), speed,
						   start, head);
	cell_t *out;
	pCtx->LocalToPhysAddr(params[8], &out);
	out[0] = sp_ftoc(speed[0]);
	out[1] = sp_ftoc(speed[1]);
	pCtx->LocalToPhysAddr(params[9], &out);
	for (int i = 0; i < 6; i++)
	{
		out[i] = sp_ftoc(start[i]);
	}
	return r;
}

cell_t N_SeamCatch(IPluginContext *pCtx, const cell_t *params)
{
	return SeamCatch(pCtx, params, false);
}

cell_t N_HeadSeamCatch(IPluginContext *pCtx, const cell_t *params)
{
	return SeamCatch(pCtx, params, true);
}

cell_t N_TexturebugCatch(IPluginContext *pCtx, const cell_t *params)
{
	float wall[3], normal[3], info[2];
	cell_to_float3(pCtx, params[1], wall);
	cell_to_float3(pCtx, params[2], normal);
	int r = BSPSeam::TexturebugCatch(wall, normal, sp_ctof(params[3]), sp_ctof(params[4]), sp_ctof(params[5]), sp_ctof(params[6]), info);
	cell_t *out;
	pCtx->LocalToPhysAddr(params[7], &out);
	out[0] = sp_ftoc(info[0]);
	out[1] = sp_ftoc(info[1]);
	return r;
}

cell_t N_PixelwalkCatch(IPluginContext *pCtx, const cell_t *params)
{
	float wall[3], normal[3], info[4];
	cell_to_float3(pCtx, params[1], wall);
	cell_to_float3(pCtx, params[2], normal);
	int r = BSPSeam::PixelwalkCatch(wall, normal, sp_ctof(params[3]), info);
	cell_t *out;
	pCtx->LocalToPhysAddr(params[4], &out);
	for (int k = 0; k < 4; ++k)
	{
		out[k] = sp_ftoc(info[k]);
	}
	return r;
}

cell_t N_PixelwalkEnd(IPluginContext *pCtx, const cell_t *params)
{
	float end[3], normal[3], along[3], out[3];
	cell_to_float3(pCtx, params[1], end);
	cell_to_float3(pCtx, params[2], normal);
	cell_to_float3(pCtx, params[3], along);
	int r = BSPSeam::PixelwalkEnd(end, normal, along, sp_ctof(params[4]), out);
	cell_t *o;
	pCtx->LocalToPhysAddr(params[5], &o);
	for (int k = 0; k < 3; ++k)
	{
		o[k] = sp_ftoc(out[k]);
	}
	return r;
}

cell_t N_SeamCandidates(IPluginContext *pCtx, const cell_t *params)
{
	float wall[3], normal[3];
	cell_to_float3(pCtx, params[1], wall);
	cell_to_float3(pCtx, params[2], normal);
	int maxOut = out_len(params[6]);
	if (maxOut <= 0)
	{
		return 0;
	}
	std::vector<float> zs(maxOut);
	int n = BSPSeam::Candidates(wall, normal, sp_ctof(params[3]), sp_ctof(params[4]), zs.data(), maxOut);
	cell_t *out;
	pCtx->LocalToPhysAddr(params[5], &out);
	for (int i = 0; i < n; i++)
	{
		out[i] = sp_ftoc(zs[i]);
	}
	return n;
}

cell_t N_WallEdgePoints(IPluginContext *pCtx, const cell_t *params)
{
	float spacing = sp_ctof(params[1]);
	int first = params[2];
	int maxOut = out_len(params[4]);
	if (!(spacing >= 1.0f) || first < 0 || maxOut < 0)
	{
		return pCtx->ThrowNativeError("spacing %f must be at least 1, first %d and maxOut %d not negative", spacing, first, maxOut);
	}
	std::vector<float> pts((size_t)maxOut * BSPSeam::EDGE_POINT_FLOATS);
	int total = BSPSeam::WallEdgePoints(spacing, first, pts.data(), maxOut);
	int n = (std::min)(maxOut, (std::max)(0, total - first));
	cell_t *out;
	pCtx->LocalToPhysAddr(params[3], &out);
	for (int i = 0; i < n * BSPSeam::EDGE_POINT_FLOATS; i++)
	{
		out[i] = sp_ftoc(pts[i]);
	}
	return total;
}

cell_t N_OverlayStart(IPluginContext *pCtx, const cell_t *params)
{
	cell_t *v, *o;
	pCtx->LocalToPhysAddr(params[1], &v);
	pCtx->LocalToPhysAddr(params[3], &o);
	float values[4], origin[3], spacing = sp_ctof(params[2]);
	if (!(spacing >= 1.0f))
	{
		return pCtx->ThrowNativeError("spacing %f must be at least 1", spacing);
	}
	for (int k = 0; k < 4; ++k)
	{
		values[k] = sp_ctof(v[k]);
	}
	bool hasOrigin = o != pCtx->GetNullRef(SP_NULL_VECTOR);
	for (int k = 0; hasOrigin && k < 3; ++k)
	{
		origin[k] = sp_ctof(o[k]);
	}
	return BSPOverlay::Start(values, spacing, hasOrigin ? origin : nullptr, params[4] != 0);
}

cell_t N_OverlayStep(IPluginContext *pCtx, const cell_t *params)
{
	return BSPOverlay::Step(sp_ctof(params[1]));
}

cell_t N_OverlayKinds(IPluginContext *pCtx, const cell_t *params)
{
	int first = params[1], maxOut = out_len(params[3]);
	if (first < 0 || maxOut < 0)
	{
		return pCtx->ThrowNativeError("first %d and maxKinds %d must not be negative", first, maxOut);
	}
	std::vector<int> kinds(maxOut);
	int total = BSPOverlay::Kinds(first, kinds.data(), maxOut);
	cell_t *out;
	pCtx->LocalToPhysAddr(params[2], &out);
	for (int k = 0; k < maxOut && first + k < total; ++k)
	{
		out[k] = kinds[k];
	}
	return total;
}

cell_t N_OverlayQueue(IPluginContext *pCtx, const cell_t *params)
{
	cell_t *v;
	pCtx->LocalToPhysAddr(params[1], &v);
	float values[4], spacing = sp_ctof(params[2]);
	if (!(spacing >= 1.0f))
	{
		return pCtx->ThrowNativeError("spacing %f must be at least 1", spacing);
	}
	for (int k = 0; k < 4; ++k)
	{
		values[k] = sp_ctof(v[k]);
	}
	BSPOverlay::Queue(values, spacing);
	return 0;
}

cell_t N_OverlayNearest(IPluginContext *pCtx, const cell_t *params)
{
	float origin[3];
	cell_to_float3(pCtx, params[1], origin);
	BSPOverlay::Nearest(origin);
	return 0;
}

extern const sp_nativeinfo_t g_SeamNatives[] = {
	// Unified world trace
	{"BSP_SeamCatch", N_SeamCatch},
	{"BSP_HeadSeamCatch", N_HeadSeamCatch},
	{"BSP_SeamCandidates", N_SeamCandidates},
	{"BSP_TexturebugCatch", N_TexturebugCatch},
	{"BSP_PixelwalkCatch", N_PixelwalkCatch},
	{"BSP_PixelwalkEnd", N_PixelwalkEnd},
	{"BSP_WallEdgePoints", N_WallEdgePoints},
	{"BSP_OverlayStart", N_OverlayStart},
	{"BSP_OverlayStep", N_OverlayStep},
	{"BSP_OverlayKinds", N_OverlayKinds},
	{"BSP_OverlayQueue", N_OverlayQueue},
	{"BSP_OverlayNearest", N_OverlayNearest},

	{nullptr, nullptr},
};
