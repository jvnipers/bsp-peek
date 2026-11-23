#include "natives.h"
#include "bsp_data.h"
#include "bsp_lumps.h"
#include "collide.h"
#include <cmath>
#include <vector>

// Debug
cell_t N_DebugDumpCBSP(IPluginContext *, const cell_t *params)
{
	BSPData::DebugDumpCBSP(params[1], params[2]);
	return 0;
}

cell_t N_DebugDumpCBSPPtr(IPluginContext *, const cell_t *params)
{
	BSPData::DebugDumpCBSPPtr(params[1], params[2]);
	return 0;
}

// Counts
cell_t N_NumBrushes(IPluginContext *, const cell_t *)
{
	return BSPData::GetNumBrushes();
}

cell_t N_NumBrushSides(IPluginContext *, const cell_t *)
{
	return BSPData::GetNumBrushSides();
}

cell_t N_NumLeaves(IPluginContext *, const cell_t *)
{
	return BSPData::GetNumLeaves();
}

cell_t N_NumNodes(IPluginContext *, const cell_t *)
{
	return BSPData::GetNumNodes();
}

cell_t N_NumPlanes(IPluginContext *, const cell_t *)
{
	return BSPData::GetNumPlanes();
}

cell_t N_NumBoxBrushes(IPluginContext *, const cell_t *)
{
	return BSPData::GetNumBoxBrushes();
}

cell_t N_NumCModels(IPluginContext *, const cell_t *)
{
	return BSPData::GetNumCModels();
}

cell_t N_NumAreas(IPluginContext *, const cell_t *)
{
	return BSPData::GetNumAreas();
}

cell_t N_NumAreaPortals(IPluginContext *, const cell_t *)
{
	return BSPData::GetNumAreaPortals();
}

cell_t N_NumClusters(IPluginContext *, const cell_t *)
{
	return BSPData::GetNumClusters();
}

// Visibility (PVS)
cell_t N_ClustersVisible(IPluginContext *, const cell_t *params)
{
	return BSPData::ClustersVisible(params[1], params[2]) ? 1 : 0;
}

cell_t N_LeavesVisible(IPluginContext *, const cell_t *params)
{
	return BSPData::LeavesVisible(params[1], params[2]) ? 1 : 0;
}

cell_t N_VisRowDecompress(IPluginContext *pCtx, const cell_t *params)
{
	int cluster = params[1];
	int maxBytes = params[3];
	if (maxBytes <= 0)
	{
		return 0;
	}
	if (maxBytes > 8192)
	{
		maxBytes = 8192;
	}
	uint8_t tmp[8192];
	int n = BSPData::VisRowDecompress(cluster, tmp, maxBytes);
	cell_t *buf = nullptr;
	pCtx->LocalToPhysAddr(params[2], &buf);
	for (int i = 0; i < n; ++i)
	{
		buf[i] = tmp[i];
	}
	return n;
}

// Areas / area portals
cell_t N_AreaInfo(IPluginContext *pCtx, const cell_t *params)
{
	int numPortals = 0, firstPortal = 0;
	bool ok = BSPData::AreaInfo(params[1], numPortals, firstPortal);
	cell_t *outNum = nullptr, *outFirst = nullptr;
	pCtx->LocalToPhysAddr(params[2], &outNum);
	pCtx->LocalToPhysAddr(params[3], &outFirst);
	*outNum = numPortals;
	*outFirst = firstPortal;
	return ok ? 1 : 0;
}

cell_t N_AreaPortalInfo(IPluginContext *pCtx, const cell_t *params)
{
	int portalKey = 0, otherArea = 0, firstClipVert = 0, clipVerts = 0, planenum = 0;
	bool ok = BSPData::AreaPortalInfo(params[1], portalKey, otherArea, firstClipVert, clipVerts, planenum);
	cell_t *o1 = nullptr, *o2 = nullptr, *o3 = nullptr, *o4 = nullptr, *o5 = nullptr;
	pCtx->LocalToPhysAddr(params[2], &o1);
	pCtx->LocalToPhysAddr(params[3], &o2);
	pCtx->LocalToPhysAddr(params[4], &o3);
	pCtx->LocalToPhysAddr(params[5], &o4);
	pCtx->LocalToPhysAddr(params[6], &o5);
	*o1 = portalKey;
	*o2 = otherArea;
	*o3 = firstClipVert;
	*o4 = clipVerts;
	*o5 = planenum;
	return ok ? 1 : 0;
}

// Misc
cell_t N_MapPathName(IPluginContext *pCtx, const cell_t *params)
{
	char *buf = nullptr;
	pCtx->LocalToString(params[1], &buf);
	return BSPData::MapPathName(buf, params[2]);
}

cell_t N_EmptyLeaf(IPluginContext *, const cell_t *)
{
	return BSPData::EmptyLeaf();
}

cell_t N_SolidLeaf(IPluginContext *, const cell_t *)
{
	return BSPData::SolidLeaf();
}

// Point queries
cell_t N_LeafAtPoint(IPluginContext *pCtx, const cell_t *params)
{
	float p[3];
	cell_to_float3(pCtx, params[1], p);
	return BSPData::LeafAtPoint(p);
}

cell_t N_PointContents(IPluginContext *pCtx, const cell_t *params)
{
	float p[3];
	cell_to_float3(pCtx, params[1], p);
	return BSPData::PointContents(p);
}

// Brush accessors
cell_t N_BrushContents(IPluginContext *pCtx, const cell_t *params)
{
	return BSPData::GetBrushContents(params[1]);
}

cell_t N_BrushBounds(IPluginContext *pCtx, const cell_t *params)
{
	float mins[3], maxs[3];
	bool ok = BSPData::GetBrushBounds(params[1], mins, maxs);
	float3_to_cell_ok(pCtx, params[2], mins, ok);
	float3_to_cell_ok(pCtx, params[3], maxs, ok);
	return ok ? 1 : 0;
}

cell_t N_IsBoxBrush(IPluginContext *pCtx, const cell_t *params)
{
	return BSPData::IsBoxBrush(params[1]) ? 1 : 0;
}

cell_t N_BrushNumSides(IPluginContext *pCtx, const cell_t *params)
{
	return BSPData::BrushNumSides(params[1]);
}

cell_t N_BrushRaw(IPluginContext *pCtx, const cell_t *params)
{
	int rawNumSides = 0, firstSide = 0, contents = 0;
	bool ok = BSPData::BrushRaw(params[1], rawNumSides, firstSide, contents);
	cell_t *outNum = nullptr, *outFirst = nullptr, *outContents = nullptr;
	pCtx->LocalToPhysAddr(params[2], &outNum);
	pCtx->LocalToPhysAddr(params[3], &outFirst);
	pCtx->LocalToPhysAddr(params[4], &outContents);
	*outNum = ok ? rawNumSides : -1;
	*outFirst = ok ? firstSide : -1;
	*outContents = ok ? contents : 0;
	return ok ? 1 : 0;
}

cell_t N_BrushSidePlane(IPluginContext *pCtx, const cell_t *params)
{
	float normal[3] = {0, 0, 0};
	float dist = 0.0f;
	bool ok = BSPData::BrushSidePlane(params[1], params[2], normal, dist);
	float3_to_cell(pCtx, params[3], normal);
	cell_t *outDist = nullptr;
	pCtx->LocalToPhysAddr(params[4], &outDist);
	*outDist = sp_ftoc(dist);
	return ok ? 1 : 0;
}

cell_t N_BrushSideBevel(IPluginContext *, const cell_t *params)
{
	return BSPData::BrushSideBevel(params[1], params[2]);
}

cell_t N_BrushSideThin(IPluginContext *, const cell_t *params)
{
	return BSPData::BrushSideThin(params[1], params[2]);
}

cell_t N_BrushSideTexData(IPluginContext *, const cell_t *params)
{
	return BSPData::BrushSideTexData(params[1], params[2]);
}

cell_t N_BrushSidePlaneIndex(IPluginContext *, const cell_t *params)
{
	return BSPData::BrushSidePlaneIndex(params[1], params[2]);
}

// Material name for a brush side.
// cbrushside_t.surfaceIndex is already a texdata index.
cell_t N_BrushSideMaterial(IPluginContext *pCtx, const cell_t *params)
{
	EnsureLumpsLoaded();
	int maxlen = out_len(params[4]);
	if (maxlen <= 0)
	{
		return 0;
	}
	std::vector<char> tmp(maxlen);
	tmp[0] = '\0';
	int n = 0;
	int texData = BSPData::BrushSideTexData(params[1], params[2]);
	if (texData >= 0)
	{
		n = BSPLumps::TexDataMaterialName(texData, tmp.data(), maxlen);
	}
	pCtx->StringToLocal(params[3], maxlen, tmp.data());
	return n;
}

cell_t N_BrushIsBoxAuth(IPluginContext *, const cell_t *params)
{
	return BSPData::BrushIsBoxAuth(params[1]) ? 1 : 0;
}

// Exact brush geometry / collision (plane-accurate)
cell_t N_PointInBrush(IPluginContext *pCtx, const cell_t *params)
{
	float p[3];
	cell_to_float3(pCtx, params[2], p);
	return BSPData::PointInBrush(params[1], p) ? 1 : 0;
}

cell_t N_PointContentsBrushes(IPluginContext *pCtx, const cell_t *params)
{
	float p[3];
	cell_to_float3(pCtx, params[1], p);
	return BSPData::PointContentsBrushes(p);
}

cell_t N_BrushColumnSpan(IPluginContext *pCtx, const cell_t *params)
{
	float zMin = 0.0f, zMax = 0.0f;
	bool ok = BSPData::BrushColumnSpan(params[1], sp_ctof(params[2]), sp_ctof(params[3]), zMin, zMax);
	cell_t *pMin, *pMax;
	pCtx->LocalToPhysAddr(params[4], &pMin);
	pCtx->LocalToPhysAddr(params[5], &pMax);
	*pMin = sp_ftoc(ok ? zMin : 0.0f);
	*pMax = sp_ftoc(ok ? zMax : 0.0f);
	return ok ? 1 : 0;
}

cell_t N_BrushTopZAt(IPluginContext *pCtx, const cell_t *params)
{
	float z = 0.0f;
	bool ok = BSPData::BrushTopZAt(params[1], sp_ctof(params[2]), sp_ctof(params[3]), z);
	cell_t *pZ;
	pCtx->LocalToPhysAddr(params[4], &pZ);
	*pZ = sp_ftoc(ok ? z : 0.0f);
	return ok ? 1 : 0;
}

cell_t N_BrushSideWinding(IPluginContext *pCtx, const cell_t *params)
{
	int maxVerts = params[4];
	if (maxVerts <= 0)
	{
		return 0;
	}
	if (maxVerts > 256) // BrushSideWinding never produces more
	{
		maxVerts = 256;
	}
	float verts[256 * 3];
	int n = BSPData::BrushSideWinding(params[1], params[2], verts, maxVerts);
	cell_t *out = nullptr;
	pCtx->LocalToPhysAddr(params[3], &out);
	// float[][3] starts with one indirection cell per row (absolute address since SM 1.11, relative before).
	bool direct = pCtx->GetRuntime()->UsesDirectArrays();
	for (int i = 0; i < n; ++i)
	{
		// If maxVerts exceeds the plugin's first dimension, cells past it are vert data, not row addresses.
		ucell_t rowAddr = direct ? (ucell_t)out[i] : (ucell_t)params[3] + i * sizeof(cell_t) + (ucell_t)out[i];
		cell_t *row = nullptr;
		if (pCtx->LocalToPhysAddr((cell_t)rowAddr, &row) != SP_ERROR_NONE)
		{
			return pCtx->ThrowNativeError("verts row %d is not a valid array row (maxVerts %d larger than verts?)", i, params[4]);
		}
		for (int k = 0; k < 3; ++k)
		{
			row[k] = sp_ftoc(verts[i * 3 + k]);
		}
	}
	return n;
}

cell_t N_BrushClipBox(IPluginContext *pCtx, const cell_t *params)
{
	float start[3], end[3], mins[3], maxs[3];
	cell_to_float3(pCtx, params[2], start);
	cell_to_float3(pCtx, params[3], end);
	cell_to_float3(pCtx, params[4], mins);
	cell_to_float3(pCtx, params[5], maxs);
	float frac = 1.0f, normal[3] = {0, 0, 0};
	bool startSolid = false;
	int rc = BSPData::BrushClipBox(params[1], start, end, mins, maxs, frac, normal, startSolid);
	cell_t *pFrac, *pSS;
	pCtx->LocalToPhysAddr(params[6], &pFrac);
	pCtx->LocalToPhysAddr(params[8], &pSS);
	*pFrac = sp_ftoc(frac);
	float3_to_cell(pCtx, params[7], normal);
	*pSS = startSolid ? 1 : 0;
	return rc;
}

cell_t N_SeamOrder(IPluginContext *pCtx, const cell_t *params)
{
	const collide::Bsp *bsp = BSPData::CollideBsp();
	if (!bsp)
	{
		return -1;
	}
	collide::SeamOrderQuery q;
	q.lower = (uint16_t)params[1];
	q.upper = (uint16_t)params[2];
	cell_to_float3(pCtx, params[3], q.origin);
	cell_to_float3(pCtx, params[4], q.along);
	q.alongMin = sp_ctof(params[5]);
	q.alongMax = sp_ctof(params[6]);
	q.speedMax = sp_ctof(params[7]);
	q.maxspeed = sp_ctof(params[8]);
	q.frametime = sp_ctof(params[9]);
	q.stepsize = 18.0f;
	std::vector<collide::SeamOrderCell> cells;
	collide::SeamOrders(*bsp, q, cells);

	int mask = 0;
	float lo[2] = {-1.0f, -1.0f}, hi[2] = {-1.0f, -1.0f};
	for (const collide::SeamOrderCell &c : cells)
	{
		mask |= 1 << (int)c.order;
		int k = c.order == collide::SeamOrder::LowerFirst ? 0 : c.order == collide::SeamOrder::UpperFirst ? 1 : -1;
		if (k >= 0)
		{
			lo[k] = lo[k] < 0.0f ? c.speedMin : std::fmin(lo[k], c.speedMin);
			hi[k] = std::fmax(hi[k], c.speedMax);
		}
	}
	cell_t *out;
	pCtx->LocalToPhysAddr(params[10], &out);
	out[0] = sp_ftoc(lo[0]);
	out[1] = sp_ftoc(hi[0]);
	pCtx->LocalToPhysAddr(params[11], &out);
	out[0] = sp_ftoc(lo[1]);
	out[1] = sp_ftoc(hi[1]);
	return mask;
}

cell_t N_BrushSideOrder(IPluginContext *pCtx, const cell_t *params)
{
	int maxlen = out_len(params[3]);
	if (maxlen <= 0)
	{
		return 0;
	}
	std::vector<char> tmp(maxlen);
	tmp[0] = '\0';
	int n = BSPData::BrushSideOrder(params[1], tmp.data(), maxlen);
	pCtx->StringToLocal(params[2], maxlen, tmp.data());
	return n;
}

// Leaf accessors
cell_t N_LeafBrushes(IPluginContext *pCtx, const cell_t *params)
{
	int leafIdx = params[1];
	cell_t *buf = nullptr;
	pCtx->LocalToPhysAddr(params[2], &buf);
	int maxOut = out_len(params[3]);
	if (maxOut <= 0)
	{
		return -1;
	}
	int tmp[1024];
	if (maxOut > 1024)
	{
		maxOut = 1024;
	}
	int n = BSPData::LeafBrushes(leafIdx, tmp, maxOut);
	if (n < 0)
	{
		return -1;
	}
	for (int i = 0; i < n; ++i)
	{
		buf[i] = tmp[i];
	}
	return n;
}

cell_t N_LeafContents(IPluginContext *pCtx, const cell_t *params)
{
	return BSPData::LeafContents(params[1]);
}

cell_t N_LeafCluster(IPluginContext *pCtx, const cell_t *params)
{
	return BSPData::LeafCluster(params[1]);
}

cell_t N_LeafArea(IPluginContext *pCtx, const cell_t *params)
{
	return BSPData::LeafArea(params[1]);
}

cell_t N_LeafFlags(IPluginContext *pCtx, const cell_t *params)
{
	return BSPData::LeafFlags(params[1]);
}

cell_t N_LeafDispListStart(IPluginContext *, const cell_t *params)
{
	return BSPData::LeafDispListStart(params[1]);
}

cell_t N_LeafDispCount(IPluginContext *, const cell_t *params)
{
	return BSPData::LeafDispCount(params[1]);
}

cell_t N_LeafBounds(IPluginContext *pCtx, const cell_t *params)
{
	float mins[3], maxs[3];
	bool ok = BSPData::LeafBounds(params[1], mins, maxs);
	float3_to_cell_ok(pCtx, params[2], mins, ok);
	float3_to_cell_ok(pCtx, params[3], maxs, ok);
	return ok ? 1 : 0;
}

// Node accessors (manual BSP walking)
cell_t N_NodePlane(IPluginContext *pCtx, const cell_t *params)
{
	float normal[3] = {0, 0, 0};
	float dist = 0.0f;
	bool ok = BSPData::NodePlane(params[1], normal, dist);
	float3_to_cell(pCtx, params[2], normal);
	cell_t *outDist = nullptr;
	pCtx->LocalToPhysAddr(params[3], &outDist);
	*outDist = sp_ftoc(dist);
	return ok ? 1 : 0;
}

cell_t N_NodeChildren(IPluginContext *pCtx, const cell_t *params)
{
	int left = -1, right = -1;
	bool ok = BSPData::NodeChildren(params[1], left, right);
	cell_t *outLeft = nullptr, *outRight = nullptr;
	pCtx->LocalToPhysAddr(params[2], &outLeft);
	pCtx->LocalToPhysAddr(params[3], &outRight);
	*outLeft = left;
	*outRight = right;
	return ok ? 1 : 0;
}

cell_t N_NodeBounds(IPluginContext *pCtx, const cell_t *params)
{
	float mins[3], maxs[3];
	bool ok = BSPData::NodeBounds(params[1], mins, maxs);
	float3_to_cell_ok(pCtx, params[2], mins, ok);
	float3_to_cell_ok(pCtx, params[3], maxs, ok);
	return ok ? 1 : 0;
}

// Plane table access
cell_t N_PlaneAt(IPluginContext *pCtx, const cell_t *params)
{
	float normal[3] = {0, 0, 0};
	float dist = 0.0f;
	bool ok = BSPData::PlaneAt(params[1], normal, dist);
	float3_to_cell(pCtx, params[2], normal);
	cell_t *outDist = nullptr;
	pCtx->LocalToPhysAddr(params[3], &outDist);
	*outDist = sp_ftoc(dist);
	return ok ? 1 : 0;
}

cell_t N_PlaneType(IPluginContext *, const cell_t *params)
{
	return BSPData::PlaneType(params[1]);
}

// Box brush (cboxbrush_t) accessors
cell_t N_BoxBrushBounds(IPluginContext *pCtx, const cell_t *params)
{
	float mins[3], maxs[3];
	bool ok = BSPData::BoxBrushBounds(params[1], mins, maxs);
	float3_to_cell_ok(pCtx, params[2], mins, ok);
	float3_to_cell_ok(pCtx, params[3], maxs, ok);
	return ok ? 1 : 0;
}

cell_t N_BoxBrushOriginalBrush(IPluginContext *, const cell_t *params)
{
	return BSPData::BoxBrushOriginalBrush(params[1]);
}

cell_t N_BoxBrushSurfaceIndex(IPluginContext *pCtx, const cell_t *params)
{
	int surf[6] = {0};
	bool ok = BSPData::BoxBrushSurfaceIndex(params[1], surf);
	cell_t *out = nullptr;
	pCtx->LocalToPhysAddr(params[2], &out);
	for (int i = 0; i < 6; ++i)
	{
		out[i] = ok ? surf[i] : 0;
	}
	return ok ? 1 : 0;
}

cell_t N_BoxBrushContents(IPluginContext *, const cell_t *params)
{
	return BSPData::BoxBrushContents(params[1]);
}

// Submodel (cmodel_t) accessors
cell_t N_CModelBounds(IPluginContext *pCtx, const cell_t *params)
{
	float mins[3], maxs[3];
	bool ok = BSPData::CModelBounds(params[1], mins, maxs);
	float3_to_cell_ok(pCtx, params[2], mins, ok);
	float3_to_cell_ok(pCtx, params[3], maxs, ok);
	return ok ? 1 : 0;
}

cell_t N_CModelOrigin(IPluginContext *pCtx, const cell_t *params)
{
	float origin[3] = {0, 0, 0};
	bool ok = BSPData::CModelOrigin(params[1], origin);
	float3_to_cell(pCtx, params[2], origin);
	return ok ? 1 : 0;
}

cell_t N_CModelHeadnode(IPluginContext *pCtx, const cell_t *params)
{
	return BSPData::CModelHeadnode(params[1]);
}

// "High-level" pixelsurf
cell_t N_FindBrushPairAtSeam(IPluginContext *pCtx, const cell_t *params)
{
	float p[3];
	cell_to_float3(pCtx, params[1], p);
	float seamZ = sp_ctof(params[2]);
	int lower = -1, upper = -1;
	bool ok = BSPData::FindBrushPairAtSeam(p, seamZ, lower, upper);
	cell_t *outLower = nullptr, *outUpper = nullptr;
	pCtx->LocalToPhysAddr(params[3], &outLower);
	pCtx->LocalToPhysAddr(params[4], &outUpper);
	*outLower = lower;
	*outUpper = upper;
	return ok ? 1 : 0;
}

cell_t N_FindBoxBrushPairAtSeam(IPluginContext *pCtx, const cell_t *params)
{
	float p[3];
	cell_to_float3(pCtx, params[1], p);
	float seamZ = sp_ctof(params[2]);
	int lower = -1, upper = -1;
	bool ok = BSPData::FindBoxBrushPairAtSeam(p, seamZ, lower, upper);
	cell_t *outLower = nullptr, *outUpper = nullptr;
	pCtx->LocalToPhysAddr(params[3], &outLower);
	pCtx->LocalToPhysAddr(params[4], &outUpper);
	*outLower = lower;
	*outUpper = upper;
	return ok ? 1 : 0;
}

cell_t N_LeafBrushPairAtSeam(IPluginContext *pCtx, const cell_t *params)
{
	float p[3];
	cell_to_float3(pCtx, params[1], p);
	float seamZ = sp_ctof(params[2]);
	int lower = -1, upper = -1, leaf = -1, lowerPos = -1, upperPos = -1;
	bool ok = BSPData::LeafBrushPairAtSeam(p, seamZ, lower, upper, leaf, lowerPos, upperPos);
	cell_t *o = nullptr;
	pCtx->LocalToPhysAddr(params[3], &o);
	*o = lower;
	pCtx->LocalToPhysAddr(params[4], &o);
	*o = upper;
	pCtx->LocalToPhysAddr(params[5], &o);
	*o = leaf;
	pCtx->LocalToPhysAddr(params[6], &o);
	*o = lowerPos;
	pCtx->LocalToPhysAddr(params[7], &o);
	*o = upperPos;
	return ok ? 1 : 0;
}

// Brush AABB cache management
cell_t N_RebuildCache(IPluginContext *, const cell_t *)
{
	BSPData::RebuildCache();
	return BSPData::GetNumBrushes();
}

cell_t N_RebuildCacheAsync(IPluginContext *, const cell_t *)
{
	BSPData::RebuildCacheAsync();
	return BSPData::GetNumBrushes();
}

cell_t N_CacheIsBuilding(IPluginContext *, const cell_t *)
{
	return BSPData::CacheIsBuilding() ? 1 : 0;
}

// Next world brush at/after startIdx whose contents include CONTENTS_LADDER.
cell_t N_FindLadderBrush(IPluginContext *pCtx, const cell_t *params)
{
	int n = BSPData::GetNumBrushes();
	for (int i = params[1] < 0 ? 0 : params[1]; i < n; ++i)
	{
		if (BSPData::GetBrushContents(i) & CONTENTS_LADDER)
		{
			return i;
		}
	}
	return -1;
}

extern const sp_nativeinfo_t g_DataNatives[] = {
	// Misc
	{"BSP_MapPathName", N_MapPathName},
	{"BSP_EmptyLeaf", N_EmptyLeaf},
	{"BSP_SolidLeaf", N_SolidLeaf},

	// Debug
	{"BSP_DebugDumpCBSP", N_DebugDumpCBSP},
	{"BSP_DebugDumpCBSPPtr", N_DebugDumpCBSPPtr},

	// Counts
	{"BSP_NumBrushes", N_NumBrushes},
	{"BSP_NumBrushSides", N_NumBrushSides},
	{"BSP_NumLeaves", N_NumLeaves},
	{"BSP_NumNodes", N_NumNodes},
	{"BSP_NumPlanes", N_NumPlanes},
	{"BSP_NumBoxBrushes", N_NumBoxBrushes},
	{"BSP_NumCModels", N_NumCModels},
	{"BSP_NumAreas", N_NumAreas},
	{"BSP_NumAreaPortals", N_NumAreaPortals},
	{"BSP_NumClusters", N_NumClusters},

	// Visibility (PVS)
	{"BSP_ClustersVisible", N_ClustersVisible},
	{"BSP_LeavesVisible", N_LeavesVisible},
	{"BSP_VisRowDecompress", N_VisRowDecompress},

	// Areas / area portals
	{"BSP_AreaInfo", N_AreaInfo},
	{"BSP_AreaPortalInfo", N_AreaPortalInfo},

	// Point queries
	{"BSP_LeafAtPoint", N_LeafAtPoint},
	{"BSP_PointContents", N_PointContents},

	// Brush accessors
	{"BSP_BrushContents", N_BrushContents},
	{"BSP_BrushBounds", N_BrushBounds},
	{"BSP_IsBoxBrush", N_IsBoxBrush},
	{"BSP_BrushNumSides", N_BrushNumSides},
	{"BSP_BrushRaw", N_BrushRaw},
	{"BSP_BrushSidePlane", N_BrushSidePlane},
	{"BSP_BrushSideBevel", N_BrushSideBevel},
	{"BSP_BrushSideThin", N_BrushSideThin},
	{"BSP_BrushSideTexData", N_BrushSideTexData},
	{"BSP_BrushSidePlaneIndex", N_BrushSidePlaneIndex},
	{"BSP_BrushSideMaterial", N_BrushSideMaterial},
	{"BSP_BrushIsBoxAuth", N_BrushIsBoxAuth},

	// Exact brush geometry / collision (plane-accurate)
	{"BSP_PointInBrush", N_PointInBrush},
	{"BSP_PointContentsBrushes", N_PointContentsBrushes},
	{"BSP_BrushColumnSpan", N_BrushColumnSpan},
	{"BSP_BrushTopZAt", N_BrushTopZAt},
	{"BSP_BrushSideWinding", N_BrushSideWinding},
	{"BSP_BrushClipBox", N_BrushClipBox},
	{"BSP_SeamOrder", N_SeamOrder},
	{"BSP_BrushSideOrder", N_BrushSideOrder},

	// Leaf accessors
	{"BSP_LeafBrushes", N_LeafBrushes},
	{"BSP_LeafContents", N_LeafContents},
	{"BSP_LeafCluster", N_LeafCluster},
	{"BSP_LeafArea", N_LeafArea},
	{"BSP_LeafFlags", N_LeafFlags},
	{"BSP_LeafDispListStart", N_LeafDispListStart},
	{"BSP_LeafDispCount", N_LeafDispCount},
	{"BSP_LeafBounds", N_LeafBounds},

	// Node accessors
	{"BSP_NodePlane", N_NodePlane},
	{"BSP_NodeChildren", N_NodeChildren},
	{"BSP_NodeBounds", N_NodeBounds},

	// Plane access
	{"BSP_PlaneAt", N_PlaneAt},
	{"BSP_PlaneType", N_PlaneType},

	// Box brush (cboxbrush_t) accessors
	{"BSP_BoxBrushBounds", N_BoxBrushBounds},
	{"BSP_BoxBrushOriginalBrush", N_BoxBrushOriginalBrush},
	{"BSP_BoxBrushSurfaceIndex", N_BoxBrushSurfaceIndex},
	{"BSP_BoxBrushContents", N_BoxBrushContents},

	// Submodels (cmodel_t)
	{"BSP_CModelBounds", N_CModelBounds},
	{"BSP_CModelOrigin", N_CModelOrigin},
	{"BSP_CModelHeadnode", N_CModelHeadnode},

	// "pixelsurf"
	{"BSP_FindBrushPairAtSeam", N_FindBrushPairAtSeam},
	{"BSP_FindBoxBrushPairAtSeam", N_FindBoxBrushPairAtSeam},
	{"BSP_LeafBrushPairAtSeam", N_LeafBrushPairAtSeam},

	// Brush cache
	{"BSP_RebuildCache", N_RebuildCache},
	{"BSP_RebuildCacheAsync", N_RebuildCacheAsync},
	{"BSP_CacheIsBuilding", N_CacheIsBuilding},

	// Brush-entity / trigger / ladder helpers
	{"BSP_FindLadderBrush", N_FindLadderBrush},

	{nullptr, nullptr},
};
