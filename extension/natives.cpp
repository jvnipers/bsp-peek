#include "natives.h"
#include "bsp_data.h"
#include "bsp_disp.h"
#include "bsp_lumps.h"
#include "bsp_props.h"
#include <IGameHelpers.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sp_vm_api.h>
#include <vector>

static void EnsureLumpsLoaded()
{
	if (BSPLumps::Loaded())
	{
		return;
	}
	const char *mapname = gamehelpers->GetCurrentMap();
	if (!mapname || !mapname[0])
	{
		return;
	}
	char bspPath[260];
	smutils->BuildPath(Path_Game, bspPath, sizeof(bspPath), "maps/%s.bsp", mapname);
	char err[256] = {0};
	if (!BSPLumps::LoadFromMap(mapname, bspPath, err, sizeof(err)))
	{
		smutils->LogError(myself, "Lump load failed for '%s': %s", mapname, err);
	}
}

static void EnsureDispLoaded()
{
	const char *mapname = gamehelpers->GetCurrentMap();
	if (!mapname || !mapname[0])
	{
		return;
	}
	char bspPath[260];
	smutils->BuildPath(Path_Game, bspPath, sizeof(bspPath), "maps/%s.bsp", mapname);
	char err[256] = {0};
	if (!BSPDisp::EnsureLoaded(mapname, bspPath, err, sizeof(err)))
	{
		smutils->LogError(myself, "Displacement load failed for '%s': %s", mapname, err);
	}
	else if (BSPDisp::DiskCount() > 0)
	{
		static char s_lastReportedMap[128] = {0};
		if (std::strcmp(s_lastReportedMap, mapname) != 0)
		{
			smutils->LogMessage(myself, "Loaded %d displacements for map '%s'", BSPDisp::DiskCount(), mapname);
			std::strncpy(s_lastReportedMap, mapname, sizeof(s_lastReportedMap) - 1);
		}
	}
}

static inline void cell_to_float3(IPluginContext *pCtx, cell_t addr, float out[3])
{
	cell_t *src = nullptr;
	pCtx->LocalToPhysAddr(addr, &src);
	out[0] = sp_ctof(src[0]);
	out[1] = sp_ctof(src[1]);
	out[2] = sp_ctof(src[2]);
}

// Write a float[3] back to a plugin-supplied by-ref cell array.
static inline void float3_to_cell(IPluginContext *pCtx, cell_t addr, const float v[3])
{
	cell_t *out = nullptr;
	pCtx->LocalToPhysAddr(addr, &out);
	for (int i = 0; i < 3; ++i)
	{
		out[i] = sp_ftoc(v[i]);
	}
}

// Like float3_to_cell, but writes zeros when the producing call failed.
static inline void float3_to_cell_ok(IPluginContext *pCtx, cell_t addr, const float v[3], bool ok)
{
	cell_t *out = nullptr;
	pCtx->LocalToPhysAddr(addr, &out);
	for (int i = 0; i < 3; ++i)
	{
		out[i] = sp_ftoc(ok ? v[i] : 0.0f);
	}
}

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

// Aggregate health check across all subsystems.
// Lets us distinguish "map has no data" from "a signature broke after a update"
// (every native silently returns safe defaults in both cases otherwise).
//   bits 0..3 : BSPData (see BSPData::SelfTest)
//   bit 4 (0x10): displacement engine reader ready
//   bit 5 (0x20): static-prop engine interfaces resolved
//   bit 6 (0x40): disk BSP lumps loaded for the current map
cell_t N_SelfTest(IPluginContext *, const cell_t *)
{
	int mask = BSPData::SelfTest();
	if (BSPDisp::EngineReady())
	{
		mask |= 0x10;
	}
	if (BSPProps::Ready())
	{
		mask |= 0x20;
	}
	EnsureLumpsLoaded();
	if (BSPLumps::Loaded())
	{
		mask |= 0x40;
	}
	return mask;
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
	if (!BSPData::GetBrushBounds(params[1], mins, maxs))
	{
		mins[0] = mins[1] = mins[2] = maxs[0] = maxs[1] = maxs[2] = 0.0f;
	}
	float3_to_cell(pCtx, params[2], mins);
	float3_to_cell(pCtx, params[3], maxs);
	return 0;
}

cell_t N_IsBoxBrush(IPluginContext *pCtx, const cell_t *params)
{
	return BSPData::IsBoxBrush(params[1]) ? 1 : 0;
}

cell_t N_BrushNumSides(IPluginContext *pCtx, const cell_t *params)
{
	return BSPData::BrushNumSides(params[1]);
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

cell_t N_BrushSideTexInfo(IPluginContext *, const cell_t *params)
{
	return BSPData::BrushSideTexInfo(params[1], params[2]);
}

cell_t N_BrushSidePlaneIndex(IPluginContext *, const cell_t *params)
{
	return BSPData::BrushSidePlaneIndex(params[1], params[2]);
}

// Material name for a brush side. Chains engine cbrushside_t.texinfo -> disk LUMP_TEXINFO.texdata -> texdata material string.
// Assumes the engine's runtime texinfo index equals the disk LUMP_TEXINFO index
// (true in CSGO: the engine loads texinfo straight from the lump without reordering).
cell_t N_BrushSideMaterial(IPluginContext *pCtx, const cell_t *params)
{
	EnsureLumpsLoaded();
	int maxlen = params[4];
	if (maxlen <= 0)
	{
		return 0;
	}
	std::vector<char> tmp(maxlen);
	tmp[0] = '\0';
	int n = 0;
	int texInfo = BSPData::BrushSideTexInfo(params[1], params[2]);
	if (texInfo >= 0)
	{
		int texData = BSPLumps::TexInfoTexData(texInfo);
		if (texData >= 0)
		{
			n = BSPLumps::TexDataMaterialName(texData, tmp.data(), maxlen);
		}
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
	std::vector<float> verts((size_t)maxVerts * 3, 0.0f);
	int n = BSPData::BrushSideWinding(params[1], params[2], verts.data(), maxVerts);
	cell_t *out = nullptr;
	pCtx->LocalToPhysAddr(params[3], &out);
	for (int i = 0; i < n * 3; ++i)
	{
		out[i] = sp_ftoc(verts[i]);
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

cell_t N_BrushSideOrder(IPluginContext *pCtx, const cell_t *params)
{
	int maxlen = params[3];
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

// Engine build / patch version, parsed from the game dir's steam.inf.
// No engine interface needed. The file is plain text and updates every patch.
// Returns ServerVersion (0 if unreadable), writes a summary line into buf.
cell_t N_EngineBuild(IPluginContext *pCtx, const cell_t *params)
{
	int maxlen = params[2];
	char path[300];
	smutils->BuildPath(Path_Game, path, sizeof(path), "steam.inf");
	int serverVersion = 0;
	char patch[64] = {0};
	char product[64] = {0};
	FILE *f = fopen(path, "rb");
	if (f)
	{
		char line[256];
		while (fgets(line, sizeof(line), f))
		{
			int v;
			if (sscanf(line, "ServerVersion=%d", &v) == 1)
			{
				serverVersion = v;
			}
			else if (!strncmp(line, "PatchVersion=", 13))
			{
				sscanf(line + 13, "%63[^\r\n]", patch);
			}
			else if (!strncmp(line, "ProductName=", 12))
			{
				sscanf(line + 12, "%63[^\r\n]", product);
			}
		}
		fclose(f);
	}
	if (maxlen > 0)
	{
		char buf[256];
		snprintf(buf, sizeof(buf), "ServerVersion=%d PatchVersion=%s ProductName=%s", serverVersion, patch[0] ? patch : "?",
				 product[0] ? product : "?");
		pCtx->StringToLocal(params[1], maxlen, buf);
	}
	return serverVersion;
}

// Leaf accessors
cell_t N_LeafBrushes(IPluginContext *pCtx, const cell_t *params)
{
	int leafIdx = params[1];
	cell_t *buf = nullptr;
	pCtx->LocalToPhysAddr(params[2], &buf);
	int maxOut = params[3];
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

cell_t N_LeafFirstFace(IPluginContext *, const cell_t *params)
{
	return BSPData::LeafFirstFace(params[1]);
}

cell_t N_LeafNumFaces(IPluginContext *, const cell_t *params)
{
	return BSPData::LeafNumFaces(params[1]);
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

cell_t N_FindBoxBrushOverhang(IPluginContext *pCtx, const cell_t *params)
{
	float p[3];
	cell_to_float3(pCtx, params[1], p);
	int boxIdx = -1, face = -1;
	float wallCoord = 0.f, bottomZ = 0.f, height = 0.f;
	bool ok = BSPData::FindBoxBrushOverhang(p, boxIdx, face, wallCoord, bottomZ, height);
	cell_t *o = nullptr;
	pCtx->LocalToPhysAddr(params[2], &o);
	*o = boxIdx;
	pCtx->LocalToPhysAddr(params[3], &o);
	*o = face;
	pCtx->LocalToPhysAddr(params[4], &o);
	*o = sp_ftoc(wallCoord);
	pCtx->LocalToPhysAddr(params[5], &o);
	*o = sp_ftoc(bottomZ);
	pCtx->LocalToPhysAddr(params[6], &o);
	*o = sp_ftoc(height);
	return ok ? 1 : 0;
}

cell_t N_BoxBrushOverhangWindow(IPluginContext *pCtx, const cell_t *params)
{
	float p[3], v[3];
	cell_to_float3(pCtx, params[1], p);
	cell_to_float3(pCtx, params[2], v);
	float hullHeight = sp_ctof(params[3]);
	int boxIdx = -1, face = -1;
	float wallCoord = 0.f, bottomZ = 0.f, height = 0.f;
	float maxVPerp = 0.f, vPerp = 0.f;
	bool ok = BSPData::BoxBrushOverhangWindow(p, v, hullHeight, boxIdx, face, wallCoord, bottomZ, height, maxVPerp, vPerp);
	cell_t *o = nullptr;
	pCtx->LocalToPhysAddr(params[4], &o);
	*o = boxIdx;
	pCtx->LocalToPhysAddr(params[5], &o);
	*o = face;
	pCtx->LocalToPhysAddr(params[6], &o);
	*o = sp_ftoc(wallCoord);
	pCtx->LocalToPhysAddr(params[7], &o);
	*o = sp_ftoc(bottomZ);
	pCtx->LocalToPhysAddr(params[8], &o);
	*o = sp_ftoc(height);
	pCtx->LocalToPhysAddr(params[9], &o);
	*o = sp_ftoc(maxVPerp);
	pCtx->LocalToPhysAddr(params[10], &o);
	*o = sp_ftoc(vPerp);
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
	int props[4] = {0, 0, 0, 0};
	bool ok = BSPDisp::EngineGetSurfaceProps(params[1], props);
	cell_t *outProps = nullptr;
	pCtx->LocalToPhysAddr(params[2], &outProps);
	for (int i = 0; i < 4; ++i)
	{
		outProps[i] = props[i];
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
	int maxlen = params[3];
	if (maxlen <= 0)
	{
		return 0;
	}
	char tmp[1024];
	int n = BSPDisp::EngineDebugTreeInfo(params[1], tmp, sizeof(tmp));
	pCtx->StringToLocal(params[2], maxlen, tmp);
	return n;
}

cell_t N_DispDiagnoseQuery(IPluginContext *pCtx, const cell_t *params)
{
	int maxlen = params[4];
	if (maxlen <= 0)
	{
		return 0;
	}
	char tmp[512];
	int n = BSPDisp::EngineDiagnoseQuery(sp_ctof(params[1]), sp_ctof(params[2]), tmp, sizeof(tmp));
	pCtx->StringToLocal(params[3], maxlen, tmp);
	return n;
}

// Displacement - disk-only (explicit fallback access)
// Disk indices != engine indices.
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
	int maxlen = params[3];
	if (maxlen <= 0)
	{
		return 0;
	}
	char tmp[512];
	int n = BSPDisp::DiskDebugDispInfo(params[1], tmp, sizeof(tmp));
	pCtx->StringToLocal(params[2], maxlen, tmp);
	return n;
}

// BSP file lump natives
cell_t N_BSPVersion(IPluginContext *, const cell_t *)
{
	EnsureLumpsLoaded();
	return BSPLumps::BSPVersion();
}

cell_t N_BSPRevision(IPluginContext *, const cell_t *)
{
	EnsureLumpsLoaded();
	return BSPLumps::BSPRevision();
}

cell_t N_LumpInfo(IPluginContext *pCtx, const cell_t *params)
{
	EnsureLumpsLoaded();
	int ofs = 0, len = 0, ver = 0;
	bool ok = BSPLumps::LumpInfo(params[1], ofs, len, ver);
	cell_t *outOfs, *outLen, *outVer;
	pCtx->LocalToPhysAddr(params[2], &outOfs);
	pCtx->LocalToPhysAddr(params[3], &outLen);
	pCtx->LocalToPhysAddr(params[4], &outVer);
	*outOfs = ofs;
	*outLen = len;
	*outVer = ver;
	return ok ? 1 : 0;
}

cell_t N_HasLighting(IPluginContext *, const cell_t *)
{
	EnsureLumpsLoaded();
	return BSPLumps::HasLighting() ? 1 : 0;
}

cell_t N_EntityRawLen(IPluginContext *, const cell_t *)
{
	EnsureLumpsLoaded();
	return BSPLumps::EntityRawLen();
}

cell_t N_EntityRawCopy(IPluginContext *pCtx, const cell_t *params)
{
	EnsureLumpsLoaded();
	int maxlen = params[2];
	if (maxlen <= 0)
	{
		return 0;
	}
	std::vector<char> tmp(maxlen);
	int n = BSPLumps::EntityRawCopy(tmp.data(), maxlen);
	pCtx->StringToLocal(params[1], maxlen, tmp.data());
	return n;
}

cell_t N_EntityCount(IPluginContext *, const cell_t *)
{
	EnsureLumpsLoaded();
	return BSPLumps::EntityCount();
}

cell_t N_EntityClassname(IPluginContext *pCtx, const cell_t *params)
{
	EnsureLumpsLoaded();
	int maxlen = params[3];
	if (maxlen <= 0)
	{
		return 0;
	}
	std::vector<char> tmp(maxlen);
	int n = BSPLumps::EntityClassname(params[1], tmp.data(), maxlen);
	pCtx->StringToLocal(params[2], maxlen, tmp.data());
	return n;
}

cell_t N_EntityOrigin(IPluginContext *pCtx, const cell_t *params)
{
	EnsureLumpsLoaded();
	float origin[3] = {0, 0, 0};
	bool ok = BSPLumps::EntityOrigin(params[1], origin);
	float3_to_cell(pCtx, params[2], origin);
	return ok ? 1 : 0;
}

cell_t N_EntityKeyValue(IPluginContext *pCtx, const cell_t *params)
{
	EnsureLumpsLoaded();
	int maxlen = params[4];
	if (maxlen <= 0)
	{
		return 0;
	}
	char *key = nullptr;
	pCtx->LocalToString(params[2], &key);
	std::vector<char> tmp(maxlen);
	int n = BSPLumps::EntityKeyValue(params[1], key, tmp.data(), maxlen);
	pCtx->StringToLocal(params[3], maxlen, tmp.data());
	return n;
}

cell_t N_EntityModelIndex(IPluginContext *, const cell_t *params)
{
	EnsureLumpsLoaded();
	return BSPLumps::EntityModelIndex(params[1]);
}

cell_t N_TexInfoCount(IPluginContext *, const cell_t *)
{
	EnsureLumpsLoaded();
	return BSPLumps::TexInfoCount();
}

cell_t N_TexInfoFlags(IPluginContext *, const cell_t *params)
{
	EnsureLumpsLoaded();
	return BSPLumps::TexInfoFlags(params[1]);
}

cell_t N_TexInfoTexData(IPluginContext *, const cell_t *params)
{
	EnsureLumpsLoaded();
	return BSPLumps::TexInfoTexData(params[1]);
}

cell_t N_TexDataCount(IPluginContext *, const cell_t *)
{
	EnsureLumpsLoaded();
	return BSPLumps::TexDataCount();
}

cell_t N_TexDataMaterialName(IPluginContext *pCtx, const cell_t *params)
{
	EnsureLumpsLoaded();
	int maxlen = params[3];
	if (maxlen <= 0)
	{
		return 0;
	}
	std::vector<char> tmp(maxlen);
	int n = BSPLumps::TexDataMaterialName(params[1], tmp.data(), maxlen);
	pCtx->StringToLocal(params[2], maxlen, tmp.data());
	return n;
}

cell_t N_TexDataReflectivity(IPluginContext *pCtx, const cell_t *params)
{
	EnsureLumpsLoaded();
	float refl[3] = {0, 0, 0};
	bool ok = BSPLumps::TexDataReflectivity(params[1], refl);
	float3_to_cell(pCtx, params[2], refl);
	return ok ? 1 : 0;
}

cell_t N_FaceVertex(IPluginContext *pCtx, const cell_t *params)
{
	EnsureLumpsLoaded();
	float v[3] = {0, 0, 0};
	bool ok = BSPLumps::FaceVertex(params[1], params[2], v);
	float3_to_cell(pCtx, params[3], v);
	return ok ? 1 : 0;
}

cell_t N_FaceCentroid(IPluginContext *pCtx, const cell_t *params)
{
	EnsureLumpsLoaded();
	float v[3] = {0, 0, 0};
	bool ok = BSPLumps::FaceCentroid(params[1], v);
	float3_to_cell(pCtx, params[2], v);
	return ok ? 1 : 0;
}

cell_t N_FaceMaterialName(IPluginContext *pCtx, const cell_t *params)
{
	EnsureLumpsLoaded();
	int maxlen = params[3];
	if (maxlen <= 0)
	{
		return 0;
	}
	std::vector<char> tmp(maxlen);
	int n = BSPLumps::FaceMaterialName(params[1], tmp.data(), maxlen);
	pCtx->StringToLocal(params[2], maxlen, tmp.data());
	return n;
}

cell_t N_LeafVisible(IPluginContext *, const cell_t *params)
{
	EnsureLumpsLoaded();
	int ca = BSPData::LeafCluster(params[1]);
	int cb = BSPData::LeafCluster(params[2]);
	if (ca < 0 || cb < 0)
	{
		return 0;
	}
	return BSPLumps::ClusterVisible(ca, cb) ? 1 : 0;
}

cell_t N_NearestCubemap(IPluginContext *pCtx, const cell_t *params)
{
	EnsureLumpsLoaded();
	float pos[3];
	cell_to_float3(pCtx, params[1], pos);
	return BSPLumps::NearestCubemap(pos);
}

cell_t N_FindEntityByKeyValue(IPluginContext *pCtx, const cell_t *params)
{
	EnsureLumpsLoaded();
	char *key = nullptr, *value = nullptr;
	pCtx->LocalToString(params[1], &key);
	pCtx->LocalToString(params[2], &value);
	return BSPLumps::FindEntityByKeyValue(key, value, params[3]);
}

cell_t N_VisClusterCount(IPluginContext *, const cell_t *)
{
	EnsureLumpsLoaded();
	return BSPLumps::VisClusterCount();
}

cell_t N_ClusterVisible(IPluginContext *, const cell_t *params)
{
	EnsureLumpsLoaded();
	return BSPLumps::ClusterVisible(params[1], params[2]) ? 1 : 0;
}

cell_t N_CubemapCount(IPluginContext *, const cell_t *)
{
	EnsureLumpsLoaded();
	return BSPLumps::CubemapCount();
}

cell_t N_CubemapOrigin(IPluginContext *pCtx, const cell_t *params)
{
	EnsureLumpsLoaded();
	float v[3] = {0, 0, 0};
	bool ok = BSPLumps::CubemapOrigin(params[1], v);
	float3_to_cell(pCtx, params[2], v);
	return ok ? 1 : 0;
}

cell_t N_CubemapSize(IPluginContext *, const cell_t *params)
{
	EnsureLumpsLoaded();
	return BSPLumps::CubemapSize(params[1]);
}

cell_t N_EdgeCount(IPluginContext *, const cell_t *)
{
	EnsureLumpsLoaded();
	return BSPLumps::EdgeCount();
}

cell_t N_EdgeVertices(IPluginContext *pCtx, const cell_t *params)
{
	EnsureLumpsLoaded();
	int v0 = -1, v1 = -1;
	bool ok = BSPLumps::EdgeVertices(params[1], v0, v1);
	cell_t *outV0, *outV1;
	pCtx->LocalToPhysAddr(params[2], &outV0);
	pCtx->LocalToPhysAddr(params[3], &outV1);
	*outV0 = v0;
	*outV1 = v1;
	return ok ? 1 : 0;
}

cell_t N_SurfedgeCount(IPluginContext *, const cell_t *)
{
	EnsureLumpsLoaded();
	return BSPLumps::SurfedgeCount();
}

cell_t N_Surfedge(IPluginContext *, const cell_t *params)
{
	EnsureLumpsLoaded();
	return BSPLumps::Surfedge(params[1]);
}

cell_t N_SurfedgeVertex(IPluginContext *, const cell_t *params)
{
	EnsureLumpsLoaded();
	return BSPLumps::SurfedgeVertex(params[1]);
}

cell_t N_VertexCount(IPluginContext *, const cell_t *)
{
	EnsureLumpsLoaded();
	return BSPLumps::VertexCount();
}

cell_t N_VertexPos(IPluginContext *pCtx, const cell_t *params)
{
	EnsureLumpsLoaded();
	float v[3] = {0, 0, 0};
	bool ok = BSPLumps::VertexPos(params[1], v);
	float3_to_cell(pCtx, params[2], v);
	return ok ? 1 : 0;
}

cell_t N_FaceCount(IPluginContext *, const cell_t *)
{
	EnsureLumpsLoaded();
	return BSPLumps::FaceCount();
}

cell_t N_FacePlaneNum(IPluginContext *, const cell_t *params)
{
	EnsureLumpsLoaded();
	return BSPLumps::FacePlaneNum(params[1]);
}

cell_t N_FaceFirstEdge(IPluginContext *, const cell_t *params)
{
	EnsureLumpsLoaded();
	return BSPLumps::FaceFirstEdge(params[1]);
}

cell_t N_FaceNumEdges(IPluginContext *, const cell_t *params)
{
	EnsureLumpsLoaded();
	return BSPLumps::FaceNumEdges(params[1]);
}

cell_t N_FaceTexInfo(IPluginContext *, const cell_t *params)
{
	EnsureLumpsLoaded();
	return BSPLumps::FaceTexInfo(params[1]);
}

cell_t N_FaceDispInfo(IPluginContext *, const cell_t *params)
{
	EnsureLumpsLoaded();
	return BSPLumps::FaceDispInfo(params[1]);
}

cell_t N_FaceArea(IPluginContext *, const cell_t *params)
{
	EnsureLumpsLoaded();
	return sp_ftoc(BSPLumps::FaceArea(params[1]));
}

cell_t N_FaceLightStyles(IPluginContext *pCtx, const cell_t *params)
{
	EnsureLumpsLoaded();
	uint8_t styles[4] = {255, 255, 255, 255};
	bool ok = BSPLumps::FaceLightStyles(params[1], styles);
	cell_t *out;
	pCtx->LocalToPhysAddr(params[2], &out);
	for (int i = 0; i < 4; ++i)
	{
		out[i] = styles[i];
	}
	return ok ? 1 : 0;
}

cell_t N_FaceOrigFace(IPluginContext *, const cell_t *params)
{
	EnsureLumpsLoaded();
	return BSPLumps::FaceOrigFace(params[1]);
}

cell_t N_FaceLightOfs(IPluginContext *, const cell_t *params)
{
	EnsureLumpsLoaded();
	return BSPLumps::FaceLightOfs(params[1]);
}

cell_t N_LeafFacesCount(IPluginContext *, const cell_t *)
{
	EnsureLumpsLoaded();
	return BSPLumps::LeafFacesCount();
}

cell_t N_LeafFaces(IPluginContext *pCtx, const cell_t *params)
{
	EnsureLumpsLoaded();
	int leafIdx = params[1];
	int firstFace = BSPData::LeafFirstFace(leafIdx);
	int numFaces = BSPData::LeafNumFaces(leafIdx);
	if (firstFace < 0 || numFaces <= 0)
	{
		return 0;
	}
	cell_t *buf;
	pCtx->LocalToPhysAddr(params[2], &buf);
	int maxOut = params[3];
	if (maxOut <= 0)
	{
		return 0;
	}
	std::vector<int> tmp(maxOut);
	int n = BSPLumps::LeafFacesRange(firstFace, numFaces, tmp.data(), maxOut);
	for (int i = 0; i < n; ++i)
	{
		buf[i] = tmp[i];
	}
	return n;
}

cell_t N_WorldlightCount(IPluginContext *, const cell_t *)
{
	EnsureLumpsLoaded();
	return BSPLumps::WorldlightCount();
}

cell_t N_WorldlightOrigin(IPluginContext *pCtx, const cell_t *params)
{
	EnsureLumpsLoaded();
	float v[3] = {0, 0, 0};
	bool ok = BSPLumps::WorldlightOrigin(params[1], v);
	float3_to_cell(pCtx, params[2], v);
	return ok ? 1 : 0;
}

cell_t N_WorldlightIntensity(IPluginContext *pCtx, const cell_t *params)
{
	EnsureLumpsLoaded();
	float v[3] = {0, 0, 0};
	bool ok = BSPLumps::WorldlightIntensity(params[1], v);
	float3_to_cell(pCtx, params[2], v);
	return ok ? 1 : 0;
}

cell_t N_WorldlightNormal(IPluginContext *pCtx, const cell_t *params)
{
	EnsureLumpsLoaded();
	float v[3] = {0, 0, 0};
	bool ok = BSPLumps::WorldlightNormal(params[1], v);
	float3_to_cell(pCtx, params[2], v);
	return ok ? 1 : 0;
}

cell_t N_WorldlightType(IPluginContext *, const cell_t *params)
{
	EnsureLumpsLoaded();
	return BSPLumps::WorldlightType(params[1]);
}

cell_t N_WorldlightStyle(IPluginContext *, const cell_t *params)
{
	EnsureLumpsLoaded();
	return BSPLumps::WorldlightStyle(params[1]);
}

cell_t N_WorldlightCluster(IPluginContext *, const cell_t *params)
{
	EnsureLumpsLoaded();
	return BSPLumps::WorldlightCluster(params[1]);
}

cell_t N_WorldlightShadowCastOffset(IPluginContext *pCtx, const cell_t *params)
{
	EnsureLumpsLoaded();
	float v[3] = {0, 0, 0};
	bool ok = BSPLumps::WorldlightShadowCastOffset(params[1], v);
	float3_to_cell(pCtx, params[2], v);
	return ok ? 1 : 0;
}

cell_t N_WorldlightStopDot(IPluginContext *, const cell_t *params)
{
	EnsureLumpsLoaded();
	return sp_ftoc(BSPLumps::WorldlightStopDot(params[1]));
}

cell_t N_WorldlightStopDot2(IPluginContext *, const cell_t *params)
{
	EnsureLumpsLoaded();
	return sp_ftoc(BSPLumps::WorldlightStopDot2(params[1]));
}

cell_t N_WorldlightExponent(IPluginContext *, const cell_t *params)
{
	EnsureLumpsLoaded();
	return sp_ftoc(BSPLumps::WorldlightExponent(params[1]));
}

cell_t N_WorldlightRadius(IPluginContext *, const cell_t *params)
{
	EnsureLumpsLoaded();
	return sp_ftoc(BSPLumps::WorldlightRadius(params[1]));
}

cell_t N_WorldlightConstantAttn(IPluginContext *, const cell_t *params)
{
	EnsureLumpsLoaded();
	return sp_ftoc(BSPLumps::WorldlightConstantAttn(params[1]));
}

cell_t N_WorldlightLinearAttn(IPluginContext *, const cell_t *params)
{
	EnsureLumpsLoaded();
	return sp_ftoc(BSPLumps::WorldlightLinearAttn(params[1]));
}

cell_t N_WorldlightQuadraticAttn(IPluginContext *, const cell_t *params)
{
	EnsureLumpsLoaded();
	return sp_ftoc(BSPLumps::WorldlightQuadraticAttn(params[1]));
}

cell_t N_WorldlightFlags(IPluginContext *, const cell_t *params)
{
	EnsureLumpsLoaded();
	return BSPLumps::WorldlightFlags(params[1]);
}

cell_t N_WorldlightTexInfo(IPluginContext *, const cell_t *params)
{
	EnsureLumpsLoaded();
	return BSPLumps::WorldlightTexInfo(params[1]);
}

cell_t N_WorldlightOwner(IPluginContext *, const cell_t *params)
{
	EnsureLumpsLoaded();
	return BSPLumps::WorldlightOwner(params[1]);
}

// Static props (sprp game lump)
cell_t N_StaticPropCount(IPluginContext *, const cell_t *)
{
	EnsureLumpsLoaded();
	return BSPLumps::StaticPropCount();
}

cell_t N_StaticPropVersion(IPluginContext *, const cell_t *)
{
	EnsureLumpsLoaded();
	return BSPLumps::StaticPropVersion();
}

cell_t N_StaticPropOrigin(IPluginContext *pCtx, const cell_t *params)
{
	EnsureLumpsLoaded();
	float v[3] = {0, 0, 0};
	bool ok = BSPLumps::StaticPropOrigin(params[1], v);
	float3_to_cell(pCtx, params[2], v);
	return ok ? 1 : 0;
}

cell_t N_StaticPropAngles(IPluginContext *pCtx, const cell_t *params)
{
	EnsureLumpsLoaded();
	float v[3] = {0, 0, 0};
	bool ok = BSPLumps::StaticPropAngles(params[1], v);
	float3_to_cell(pCtx, params[2], v);
	return ok ? 1 : 0;
}

cell_t N_StaticPropSolid(IPluginContext *, const cell_t *params)
{
	EnsureLumpsLoaded();
	return BSPLumps::StaticPropSolid(params[1]);
}

cell_t N_StaticPropFlags(IPluginContext *, const cell_t *params)
{
	EnsureLumpsLoaded();
	return BSPLumps::StaticPropFlags(params[1]);
}

cell_t N_StaticPropModelName(IPluginContext *pCtx, const cell_t *params)
{
	EnsureLumpsLoaded();
	int maxlen = params[3];
	if (maxlen <= 0)
	{
		return 0;
	}
	std::vector<char> tmp(maxlen);
	int n = BSPLumps::StaticPropModelName(params[1], tmp.data(), maxlen);
	pCtx->StringToLocal(params[2], maxlen, tmp.data());
	return n;
}

cell_t N_StaticPropSkin(IPluginContext *, const cell_t *params)
{
	EnsureLumpsLoaded();
	return BSPLumps::StaticPropSkin(params[1]);
}

cell_t N_StaticPropFadeDist(IPluginContext *pCtx, const cell_t *params)
{
	EnsureLumpsLoaded();
	float mn = 0.0f, mx = 0.0f;
	bool ok = BSPLumps::StaticPropFadeDist(params[1], mn, mx);
	cell_t *pMin, *pMax;
	pCtx->LocalToPhysAddr(params[2], &pMin);
	pCtx->LocalToPhysAddr(params[3], &pMax);
	*pMin = sp_ftoc(mn);
	*pMax = sp_ftoc(mx);
	return ok ? 1 : 0;
}

cell_t N_StaticPropForcedFadeScale(IPluginContext *, const cell_t *params)
{
	EnsureLumpsLoaded();
	return sp_ftoc(BSPLumps::StaticPropForcedFadeScale(params[1]));
}

cell_t N_StaticPropLightingOrigin(IPluginContext *pCtx, const cell_t *params)
{
	EnsureLumpsLoaded();
	float v[3] = {0, 0, 0};
	bool ok = BSPLumps::StaticPropLightingOrigin(params[1], v);
	float3_to_cell(pCtx, params[2], v);
	return ok ? 1 : 0;
}

cell_t N_StaticPropFlagsEx(IPluginContext *, const cell_t *params)
{
	EnsureLumpsLoaded();
	return BSPLumps::StaticPropFlagsEx(params[1]);
}

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
	int maxlen = params[3];
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
	int maxlen = params[3];
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

cell_t N_StaticPropLeaves(IPluginContext *pCtx, const cell_t *params)
{
	EnsureLumpsLoaded();
	int maxOut = params[3];
	if (maxOut <= 0)
	{
		return 0;
	}
	cell_t *out;
	pCtx->LocalToPhysAddr(params[2], &out);
	std::vector<int> tmp(maxOut);
	int n = BSPLumps::StaticPropLeaves(params[1], tmp.data(), maxOut);
	for (int i = 0; i < n; ++i)
	{
		out[i] = tmp[i];
	}
	return n;
}

cell_t N_NearestStaticProp(IPluginContext *pCtx, const cell_t *params)
{
	EnsureLumpsLoaded();
	float pos[3];
	cell_to_float3(pCtx, params[1], pos);
	return BSPLumps::NearestStaticProp(pos, sp_ctof(params[2]));
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
	int maxlen = params[2];
	if (maxlen <= 0)
	{
		return 0;
	}
	std::vector<char> tmp(maxlen);
	int n = BSPProps::Debug(tmp.data(), maxlen);
	pCtx->StringToLocal(params[1], maxlen, tmp.data());
	return n;
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

	int nameLen = params[19];
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
	int maxlen = params[3];
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
	int maxlen = params[3];
	if (maxlen <= 0)
	{
		return 0;
	}
	std::vector<char> tmp(maxlen);
	int n = BSPProps::SurfacePropDump(params[1], tmp.data(), maxlen);
	pCtx->StringToLocal(params[2], maxlen, tmp.data());
	return n;
}

// Leaf water data
cell_t N_LeafWaterCount(IPluginContext *pCtx, const cell_t *params)
{
	EnsureLumpsLoaded();
	return BSPLumps::LeafWaterCount();
}

cell_t N_LeafWaterData(IPluginContext *pCtx, const cell_t *params)
{
	EnsureLumpsLoaded();
	float surfaceZ, minZ;
	int texinfo;
	bool ok = BSPLumps::LeafWaterData(params[1], surfaceZ, minZ, texinfo);
	cell_t *pSurf, *pMin, *pTex;
	pCtx->LocalToPhysAddr(params[2], &pSurf);
	pCtx->LocalToPhysAddr(params[3], &pMin);
	pCtx->LocalToPhysAddr(params[4], &pTex);
	*pSurf = sp_ftoc(surfaceZ);
	*pMin = sp_ftoc(minZ);
	*pTex = texinfo;
	return ok ? 1 : 0;
}

cell_t N_WaterSurfaceZAt(IPluginContext *pCtx, const cell_t *params)
{
	EnsureLumpsLoaded();
	float pos[3];
	cell_to_float3(pCtx, params[1], pos);
	return sp_ftoc(BSPLumps::WaterSurfaceZAt(pos));
}

// Brush-entity / trigger helpers (composition over entity + cmodel data)

// World AABB of a brush entity: cmodel (model-local) bounds + entity origin.
cell_t N_EntityBrushBounds(IPluginContext *pCtx, const cell_t *params)
{
	EnsureLumpsLoaded();
	int modelIdx = BSPLumps::EntityModelIndex(params[1]);
	float mins[3] = {0, 0, 0}, maxs[3] = {0, 0, 0};
	bool ok = modelIdx >= 0 && BSPData::CModelBounds(modelIdx, mins, maxs);
	if (ok)
	{
		float org[3] = {0, 0, 0};
		BSPLumps::EntityOrigin(params[1], org); // 0 if no "origin" key
		for (int i = 0; i < 3; ++i)
		{
			mins[i] += org[i];
			maxs[i] += org[i];
		}
	}
	float3_to_cell(pCtx, params[2], mins);
	float3_to_cell(pCtx, params[3], maxs);
	return ok ? 1 : 0;
}

// trigger_push applied velocity = AngleVectors(pushdir).forward * speed.
cell_t N_TriggerPushVelocity(IPluginContext *pCtx, const cell_t *params)
{
	EnsureLumpsLoaded();
	float vel[3] = {0, 0, 0};
	char cls[64];
	BSPLumps::EntityClassname(params[1], cls, sizeof(cls));
	if (std::strcmp(cls, "trigger_push") != 0)
	{
		float3_to_cell(pCtx, params[2], vel);
		return 0;
	}
	char buf[64];
	float pitch = 0, yaw = 0, roll = 0;
	if (BSPLumps::EntityKeyValue(params[1], "pushdir", buf, sizeof(buf)) > 0)
	{
		std::sscanf(buf, "%f %f %f", &pitch, &yaw, &roll);
	}
	float speed = 40.0f; // trigger_push default
	if (BSPLumps::EntityKeyValue(params[1], "speed", buf, sizeof(buf)) > 0)
	{
		speed = (float)std::atof(buf);
	}
	// AngleVectors forward (pitch, yaw): matches the engine's push direction.
	const float d2r = 3.14159265358979323846f / 180.0f;
	float sp = std::sin(pitch * d2r), cp = std::cos(pitch * d2r);
	float sy = std::sin(yaw * d2r), cy = std::cos(yaw * d2r);
	vel[0] = cp * cy * speed;
	vel[1] = cp * sy * speed;
	vel[2] = -sp * speed;
	float3_to_cell(pCtx, params[2], vel);
	return 1;
}

// Origin of the entity targeted by entIdx's "target" key (teleport dest etc.).
cell_t N_EntityTargetOrigin(IPluginContext *pCtx, const cell_t *params)
{
	EnsureLumpsLoaded();
	float origin[3] = {0, 0, 0};
	char target[128];
	bool ok = false;
	if (BSPLumps::EntityKeyValue(params[1], "target", target, sizeof(target)) > 0)
	{
		int dest = BSPLumps::FindEntityByKeyValue("targetname", target, 0);
		if (dest >= 0)
		{
			ok = BSPLumps::EntityOrigin(dest, origin);
		}
	}
	float3_to_cell(pCtx, params[2], origin);
	return ok ? 1 : 0;
}

// Next world brush at/after startIdx whose contents include CONTENTS_LADDER.
cell_t N_FindLadderBrush(IPluginContext *pCtx, const cell_t *params)
{
	const int CONTENTS_LADDER = 0x20000000;
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

extern const sp_nativeinfo_t g_BSPNatives[] = {
	// Misc
	{"BSP_MapPathName", N_MapPathName},
	{"BSP_EmptyLeaf", N_EmptyLeaf},
	{"BSP_SolidLeaf", N_SolidLeaf},
	{"BSP_SelfTest", N_SelfTest},
	{"BSP_EngineBuild", N_EngineBuild},

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
	{"BSP_BrushSidePlane", N_BrushSidePlane},
	{"BSP_BrushSideBevel", N_BrushSideBevel},
	{"BSP_BrushSideThin", N_BrushSideThin},
	{"BSP_BrushSideTexInfo", N_BrushSideTexInfo},
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
	{"BSP_BrushSideOrder", N_BrushSideOrder},

	// Leaf accessors
	{"BSP_LeafBrushes", N_LeafBrushes},
	{"BSP_LeafContents", N_LeafContents},
	{"BSP_LeafCluster", N_LeafCluster},
	{"BSP_LeafArea", N_LeafArea},
	{"BSP_LeafFlags", N_LeafFlags},
	{"BSP_LeafFirstFace", N_LeafFirstFace},
	{"BSP_LeafNumFaces", N_LeafNumFaces},
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
	{"BSP_FindBoxBrushOverhang", N_FindBoxBrushOverhang},
	{"BSP_BoxBrushOverhangWindow", N_BoxBrushOverhangWindow},
	{"BSP_LeafBrushPairAtSeam", N_LeafBrushPairAtSeam},

	// Brush cache
	{"BSP_RebuildCache", N_RebuildCache},
	{"BSP_RebuildCacheAsync", N_RebuildCacheAsync},
	{"BSP_CacheIsBuilding", N_CacheIsBuilding},

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

	// BSP file lump natives
	{"BSP_BSPVersion", N_BSPVersion},
	{"BSP_BSPRevision", N_BSPRevision},
	{"BSP_LumpInfo", N_LumpInfo},
	{"BSP_HasLighting", N_HasLighting},

	{"BSP_EntityRawLen", N_EntityRawLen},
	{"BSP_EntityRawCopy", N_EntityRawCopy},
	{"BSP_EntityCount", N_EntityCount},
	{"BSP_EntityClassname", N_EntityClassname},
	{"BSP_EntityOrigin", N_EntityOrigin},
	{"BSP_EntityKeyValue", N_EntityKeyValue},
	{"BSP_EntityModelIndex", N_EntityModelIndex},
	{"BSP_FindEntityByKeyValue", N_FindEntityByKeyValue},

	{"BSP_TexInfoCount", N_TexInfoCount},
	{"BSP_TexInfoFlags", N_TexInfoFlags},
	{"BSP_TexInfoTexData", N_TexInfoTexData},
	{"BSP_TexDataCount", N_TexDataCount},
	{"BSP_TexDataMaterialName", N_TexDataMaterialName},
	{"BSP_TexDataReflectivity", N_TexDataReflectivity},

	{"BSP_VisClusterCount", N_VisClusterCount},
	{"BSP_ClusterVisible", N_ClusterVisible},
	{"BSP_LeafVisible", N_LeafVisible},

	{"BSP_CubemapCount", N_CubemapCount},
	{"BSP_CubemapOrigin", N_CubemapOrigin},
	{"BSP_CubemapSize", N_CubemapSize},
	{"BSP_NearestCubemap", N_NearestCubemap},

	{"BSP_EdgeCount", N_EdgeCount},
	{"BSP_EdgeVertices", N_EdgeVertices},
	{"BSP_SurfedgeCount", N_SurfedgeCount},
	{"BSP_Surfedge", N_Surfedge},
	{"BSP_SurfedgeVertex", N_SurfedgeVertex},

	{"BSP_VertexCount", N_VertexCount},
	{"BSP_VertexPos", N_VertexPos},

	{"BSP_FaceVertex", N_FaceVertex},
	{"BSP_FaceCentroid", N_FaceCentroid},
	{"BSP_FaceMaterialName", N_FaceMaterialName},

	{"BSP_FaceCount", N_FaceCount},
	{"BSP_FacePlaneNum", N_FacePlaneNum},
	{"BSP_FaceFirstEdge", N_FaceFirstEdge},
	{"BSP_FaceNumEdges", N_FaceNumEdges},
	{"BSP_FaceTexInfo", N_FaceTexInfo},
	{"BSP_FaceDispInfo", N_FaceDispInfo},
	{"BSP_FaceArea", N_FaceArea},
	{"BSP_FaceLightStyles", N_FaceLightStyles},
	{"BSP_FaceOrigFace", N_FaceOrigFace},
	{"BSP_FaceLightOfs", N_FaceLightOfs},

	{"BSP_LeafFacesCount", N_LeafFacesCount},
	{"BSP_LeafFaces", N_LeafFaces},

	{"BSP_WorldlightCount", N_WorldlightCount},
	{"BSP_WorldlightOrigin", N_WorldlightOrigin},
	{"BSP_WorldlightIntensity", N_WorldlightIntensity},
	{"BSP_WorldlightNormal", N_WorldlightNormal},
	{"BSP_WorldlightType", N_WorldlightType},
	{"BSP_WorldlightStyle", N_WorldlightStyle},
	{"BSP_WorldlightCluster", N_WorldlightCluster},
	{"BSP_WorldlightShadowCastOffset", N_WorldlightShadowCastOffset},
	{"BSP_WorldlightStopDot", N_WorldlightStopDot},
	{"BSP_WorldlightStopDot2", N_WorldlightStopDot2},
	{"BSP_WorldlightExponent", N_WorldlightExponent},
	{"BSP_WorldlightRadius", N_WorldlightRadius},
	{"BSP_WorldlightConstantAttn", N_WorldlightConstantAttn},
	{"BSP_WorldlightLinearAttn", N_WorldlightLinearAttn},
	{"BSP_WorldlightQuadraticAttn", N_WorldlightQuadraticAttn},
	{"BSP_WorldlightFlags", N_WorldlightFlags},
	{"BSP_WorldlightTexInfo", N_WorldlightTexInfo},
	{"BSP_WorldlightOwner", N_WorldlightOwner},

	{"BSP_NumStaticProps", N_StaticPropCount},
	{"BSP_StaticPropVersion", N_StaticPropVersion},
	{"BSP_StaticPropOrigin", N_StaticPropOrigin},
	{"BSP_StaticPropAngles", N_StaticPropAngles},
	{"BSP_StaticPropSolid", N_StaticPropSolid},
	{"BSP_StaticPropFlags", N_StaticPropFlags},
	{"BSP_StaticPropModelName", N_StaticPropModelName},
	{"BSP_StaticPropSkin", N_StaticPropSkin},
	{"BSP_StaticPropFadeDist", N_StaticPropFadeDist},
	{"BSP_StaticPropForcedFadeScale", N_StaticPropForcedFadeScale},
	{"BSP_StaticPropLightingOrigin", N_StaticPropLightingOrigin},
	{"BSP_StaticPropFlagsEx", N_StaticPropFlagsEx},
	{"BSP_StaticPropLeaves", N_StaticPropLeaves},
	{"BSP_NearestStaticProp", N_NearestStaticProp},

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
	{"BSP_SurfacePropDump", N_SurfacePropDump},

	// Leaf water data
	{"BSP_LeafWaterCount", N_LeafWaterCount},
	{"BSP_LeafWaterData", N_LeafWaterData},
	{"BSP_WaterSurfaceZAt", N_WaterSurfaceZAt},

	// Brush-entity / trigger / ladder helpers
	{"BSP_EntityBrushBounds", N_EntityBrushBounds},
	{"BSP_TriggerPushVelocity", N_TriggerPushVelocity},
	{"BSP_EntityTargetOrigin", N_EntityTargetOrigin},
	{"BSP_FindLadderBrush", N_FindLadderBrush},

	{nullptr, nullptr},
};
