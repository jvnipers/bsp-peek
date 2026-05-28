#include "natives.h"
#include "bsp_data.h"
#include "bsp_disp.h"
#include "bsp_lumps.h"
#include <IGameHelpers.h>
#include <cmath>
#include <cstring>
#include <sp_vm_api.h>
#include <vector>

static void EnsureLumpsLoaded() {
  if (BSPLumps::Loaded())
    return;
  const char *mapname = gamehelpers->GetCurrentMap();
  if (!mapname || !mapname[0])
    return;
  char bspPath[260];
  smutils->BuildPath(Path_Game, bspPath, sizeof(bspPath), "maps/%s.bsp",
                     mapname);
  char err[256] = {0};
  if (!BSPLumps::LoadFromMap(mapname, bspPath, err, sizeof(err))) {
    smutils->LogError(myself, "Lump load failed for '%s': %s", mapname, err);
  }
}

static void EnsureDispLoaded() {
  const char *mapname = gamehelpers->GetCurrentMap();
  if (!mapname || !mapname[0])
    return;
  char bspPath[260];
  smutils->BuildPath(Path_Game, bspPath, sizeof(bspPath), "maps/%s.bsp",
                     mapname);
  char err[256] = {0};
  if (!BSPDisp::EnsureLoaded(mapname, bspPath, err, sizeof(err))) {
    smutils->LogError(myself, "Displacement load failed for '%s': %s", mapname,
                      err);
  } else if (BSPDisp::DiskCount() > 0) {
    static char s_lastReportedMap[128] = {0};
    if (std::strcmp(s_lastReportedMap, mapname) != 0) {
      smutils->LogMessage(myself, "Loaded %d displacements for map '%s'",
                          BSPDisp::DiskCount(), mapname);
      std::strncpy(s_lastReportedMap, mapname, sizeof(s_lastReportedMap) - 1);
    }
  }
}

static inline void cell_to_float3(IPluginContext *pCtx, cell_t addr,
                                  float out[3]) {
  cell_t *src = nullptr;
  pCtx->LocalToPhysAddr(addr, &src);
  out[0] = sp_ctof(src[0]);
  out[1] = sp_ctof(src[1]);
  out[2] = sp_ctof(src[2]);
}

// Counts
cell_t N_NumBrushes(IPluginContext *, const cell_t *) {
  return BSPData::GetNumBrushes();
}
cell_t N_NumBrushSides(IPluginContext *, const cell_t *) {
  return BSPData::GetNumBrushSides();
}
cell_t N_NumLeaves(IPluginContext *, const cell_t *) {
  return BSPData::GetNumLeaves();
}
cell_t N_NumNodes(IPluginContext *, const cell_t *) {
  return BSPData::GetNumNodes();
}
cell_t N_NumPlanes(IPluginContext *, const cell_t *) {
  return BSPData::GetNumPlanes();
}
cell_t N_NumBoxBrushes(IPluginContext *, const cell_t *) {
  return BSPData::GetNumBoxBrushes();
}
cell_t N_NumCModels(IPluginContext *, const cell_t *) {
  return BSPData::GetNumCModels();
}

// Misc
cell_t N_MapPathName(IPluginContext *pCtx, const cell_t *params) {
  char *buf = nullptr;
  pCtx->LocalToString(params[1], &buf);
  return BSPData::MapPathName(buf, params[2]);
}
cell_t N_EmptyLeaf(IPluginContext *, const cell_t *) {
  return BSPData::EmptyLeaf();
}
cell_t N_SolidLeaf(IPluginContext *, const cell_t *) {
  return BSPData::SolidLeaf();
}

// Point queries
cell_t N_LeafAtPoint(IPluginContext *pCtx, const cell_t *params) {
  float p[3];
  cell_to_float3(pCtx, params[1], p);
  return BSPData::LeafAtPoint(p);
}

cell_t N_PointContents(IPluginContext *pCtx, const cell_t *params) {
  float p[3];
  cell_to_float3(pCtx, params[1], p);
  return BSPData::PointContents(p);
}

// Brush accessors
cell_t N_BrushContents(IPluginContext *pCtx, const cell_t *params) {
  return BSPData::GetBrushContents(params[1]);
}

cell_t N_BrushBounds(IPluginContext *pCtx, const cell_t *params) {
  float mins[3], maxs[3];
  if (!BSPData::GetBrushBounds(params[1], mins, maxs))
    mins[0] = mins[1] = mins[2] = maxs[0] = maxs[1] = maxs[2] = 0.0f;
  cell_t *outMins = nullptr, *outMaxs = nullptr;
  pCtx->LocalToPhysAddr(params[2], &outMins);
  pCtx->LocalToPhysAddr(params[3], &outMaxs);
  for (int i = 0; i < 3; ++i) {
    outMins[i] = sp_ftoc(mins[i]);
    outMaxs[i] = sp_ftoc(maxs[i]);
  }
  return 0;
}

cell_t N_IsBoxBrush(IPluginContext *pCtx, const cell_t *params) {
  return BSPData::IsBoxBrush(params[1]) ? 1 : 0;
}

cell_t N_BrushNumSides(IPluginContext *pCtx, const cell_t *params) {
  return BSPData::BrushNumSides(params[1]);
}

cell_t N_BrushSidePlane(IPluginContext *pCtx, const cell_t *params) {
  float normal[3] = {0, 0, 0};
  float dist = 0.0f;
  bool ok = BSPData::BrushSidePlane(params[1], params[2], normal, dist);
  cell_t *outNormal = nullptr, *outDist = nullptr;
  pCtx->LocalToPhysAddr(params[3], &outNormal);
  pCtx->LocalToPhysAddr(params[4], &outDist);
  for (int i = 0; i < 3; ++i)
    outNormal[i] = sp_ftoc(normal[i]);
  *outDist = sp_ftoc(dist);
  return ok ? 1 : 0;
}

cell_t N_BrushSideBevel(IPluginContext *, const cell_t *params) {
  return BSPData::BrushSideBevel(params[1], params[2]);
}
cell_t N_BrushSideThin(IPluginContext *, const cell_t *params) {
  return BSPData::BrushSideThin(params[1], params[2]);
}
cell_t N_BrushSideTexInfo(IPluginContext *, const cell_t *params) {
  return BSPData::BrushSideTexInfo(params[1], params[2]);
}

// Leaf accessors
cell_t N_LeafBrushes(IPluginContext *pCtx, const cell_t *params) {
  int leafIdx = params[1];
  cell_t *buf = nullptr;
  pCtx->LocalToPhysAddr(params[2], &buf);
  int maxOut = params[3];
  if (maxOut <= 0)
    return -1;
  int tmp[1024];
  if (maxOut > 1024)
    maxOut = 1024;
  int n = BSPData::LeafBrushes(leafIdx, tmp, maxOut);
  if (n < 0)
    return -1;
  for (int i = 0; i < n; ++i)
    buf[i] = tmp[i];
  return n;
}

cell_t N_LeafContents(IPluginContext *pCtx, const cell_t *params) {
  return BSPData::LeafContents(params[1]);
}
cell_t N_LeafCluster(IPluginContext *pCtx, const cell_t *params) {
  return BSPData::LeafCluster(params[1]);
}
cell_t N_LeafArea(IPluginContext *pCtx, const cell_t *params) {
  return BSPData::LeafArea(params[1]);
}
cell_t N_LeafFlags(IPluginContext *pCtx, const cell_t *params) {
  return BSPData::LeafFlags(params[1]);
}
cell_t N_LeafFirstFace(IPluginContext *, const cell_t *params) {
  return BSPData::LeafFirstFace(params[1]);
}
cell_t N_LeafNumFaces(IPluginContext *, const cell_t *params) {
  return BSPData::LeafNumFaces(params[1]);
}
cell_t N_LeafBounds(IPluginContext *pCtx, const cell_t *params) {
  float mins[3], maxs[3];
  bool ok = BSPData::LeafBounds(params[1], mins, maxs);
  cell_t *outMins = nullptr, *outMaxs = nullptr;
  pCtx->LocalToPhysAddr(params[2], &outMins);
  pCtx->LocalToPhysAddr(params[3], &outMaxs);
  for (int i = 0; i < 3; ++i) {
    outMins[i] = sp_ftoc(ok ? mins[i] : 0.0f);
    outMaxs[i] = sp_ftoc(ok ? maxs[i] : 0.0f);
  }
  return ok ? 1 : 0;
}

// Node accessors (manual BSP walking)
cell_t N_NodePlane(IPluginContext *pCtx, const cell_t *params) {
  float normal[3] = {0, 0, 0};
  float dist = 0.0f;
  bool ok = BSPData::NodePlane(params[1], normal, dist);
  cell_t *outNormal = nullptr, *outDist = nullptr;
  pCtx->LocalToPhysAddr(params[2], &outNormal);
  pCtx->LocalToPhysAddr(params[3], &outDist);
  for (int i = 0; i < 3; ++i)
    outNormal[i] = sp_ftoc(normal[i]);
  *outDist = sp_ftoc(dist);
  return ok ? 1 : 0;
}

cell_t N_NodeChildren(IPluginContext *pCtx, const cell_t *params) {
  int left = -1, right = -1;
  bool ok = BSPData::NodeChildren(params[1], left, right);
  cell_t *outLeft = nullptr, *outRight = nullptr;
  pCtx->LocalToPhysAddr(params[2], &outLeft);
  pCtx->LocalToPhysAddr(params[3], &outRight);
  *outLeft = left;
  *outRight = right;
  return ok ? 1 : 0;
}

cell_t N_NodeBounds(IPluginContext *pCtx, const cell_t *params) {
  float mins[3], maxs[3];
  bool ok = BSPData::NodeBounds(params[1], mins, maxs);
  cell_t *outMins = nullptr, *outMaxs = nullptr;
  pCtx->LocalToPhysAddr(params[2], &outMins);
  pCtx->LocalToPhysAddr(params[3], &outMaxs);
  for (int i = 0; i < 3; ++i) {
    outMins[i] = sp_ftoc(ok ? mins[i] : 0.0f);
    outMaxs[i] = sp_ftoc(ok ? maxs[i] : 0.0f);
  }
  return ok ? 1 : 0;
}

// Plane table access
cell_t N_PlaneAt(IPluginContext *pCtx, const cell_t *params) {
  float normal[3] = {0, 0, 0};
  float dist = 0.0f;
  bool ok = BSPData::PlaneAt(params[1], normal, dist);
  cell_t *outNormal = nullptr, *outDist = nullptr;
  pCtx->LocalToPhysAddr(params[2], &outNormal);
  pCtx->LocalToPhysAddr(params[3], &outDist);
  for (int i = 0; i < 3; ++i)
    outNormal[i] = sp_ftoc(normal[i]);
  *outDist = sp_ftoc(dist);
  return ok ? 1 : 0;
}

cell_t N_PlaneType(IPluginContext *, const cell_t *params) {
  return BSPData::PlaneType(params[1]);
}

// Box brush (cboxbrush_t) accessors
cell_t N_BoxBrushBounds(IPluginContext *pCtx, const cell_t *params) {
  float mins[3], maxs[3];
  bool ok = BSPData::BoxBrushBounds(params[1], mins, maxs);
  cell_t *outMins = nullptr, *outMaxs = nullptr;
  pCtx->LocalToPhysAddr(params[2], &outMins);
  pCtx->LocalToPhysAddr(params[3], &outMaxs);
  for (int i = 0; i < 3; ++i) {
    outMins[i] = sp_ftoc(ok ? mins[i] : 0.0f);
    outMaxs[i] = sp_ftoc(ok ? maxs[i] : 0.0f);
  }
  return ok ? 1 : 0;
}

cell_t N_BoxBrushOriginalBrush(IPluginContext *, const cell_t *params) {
  return BSPData::BoxBrushOriginalBrush(params[1]);
}

cell_t N_BoxBrushSurfaceIndex(IPluginContext *pCtx, const cell_t *params) {
  int surf[6] = {0};
  bool ok = BSPData::BoxBrushSurfaceIndex(params[1], surf);
  cell_t *out = nullptr;
  pCtx->LocalToPhysAddr(params[2], &out);
  for (int i = 0; i < 6; ++i)
    out[i] = ok ? surf[i] : 0;
  return ok ? 1 : 0;
}

cell_t N_BoxBrushContents(IPluginContext *, const cell_t *params) {
  return BSPData::BoxBrushContents(params[1]);
}

// Submodel (cmodel_t) accessors
cell_t N_CModelBounds(IPluginContext *pCtx, const cell_t *params) {
  float mins[3], maxs[3];
  bool ok = BSPData::CModelBounds(params[1], mins, maxs);
  cell_t *outMins = nullptr, *outMaxs = nullptr;
  pCtx->LocalToPhysAddr(params[2], &outMins);
  pCtx->LocalToPhysAddr(params[3], &outMaxs);
  for (int i = 0; i < 3; ++i) {
    outMins[i] = sp_ftoc(ok ? mins[i] : 0.0f);
    outMaxs[i] = sp_ftoc(ok ? maxs[i] : 0.0f);
  }
  return ok ? 1 : 0;
}

cell_t N_CModelOrigin(IPluginContext *pCtx, const cell_t *params) {
  float origin[3] = {0, 0, 0};
  bool ok = BSPData::CModelOrigin(params[1], origin);
  cell_t *outOrigin = nullptr;
  pCtx->LocalToPhysAddr(params[2], &outOrigin);
  for (int i = 0; i < 3; ++i)
    outOrigin[i] = sp_ftoc(origin[i]);
  return ok ? 1 : 0;
}

cell_t N_CModelHeadnode(IPluginContext *pCtx, const cell_t *params) {
  return BSPData::CModelHeadnode(params[1]);
}

// "High-level" pixelsurf
cell_t N_FindBrushPairAtSeam(IPluginContext *pCtx, const cell_t *params) {
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

// Brush AABB cache management
cell_t N_RebuildCache(IPluginContext *, const cell_t *) {
  BSPData::RebuildCache();
  return BSPData::GetNumBrushes();
}
cell_t N_RebuildCacheAsync(IPluginContext *, const cell_t *) {
  BSPData::RebuildCacheAsync();
  return BSPData::GetNumBrushes();
}
cell_t N_CacheIsBuilding(IPluginContext *, const cell_t *) {
  return BSPData::CacheIsBuilding() ? 1 : 0;
}

// Displacement queries (engine-first, disk fallback)
cell_t N_DispHeightAt(IPluginContext *pCtx, const cell_t *params) {
  EnsureDispLoaded();
  return sp_ftoc(BSPDisp::HeightAt(sp_ctof(params[1]), sp_ctof(params[2])));
}

cell_t N_DispHeightAtDebug(IPluginContext *pCtx, const cell_t *params) {
  EnsureDispLoaded();
  int idx = -1;
  float z = BSPDisp::HeightAtDebug(sp_ctof(params[1]), sp_ctof(params[2]), idx);
  cell_t *outIdx = nullptr;
  pCtx->LocalToPhysAddr(params[3], &outIdx);
  *outIdx = idx;
  return sp_ftoc(z);
}

cell_t N_DispSurfaceNormalAt(IPluginContext *pCtx, const cell_t *params) {
  EnsureDispLoaded();
  float normal[3] = {0, 0, 1};
  float z =
      BSPDisp::SurfaceNormalAt(sp_ctof(params[1]), sp_ctof(params[2]), normal);
  cell_t *outNormal = nullptr;
  pCtx->LocalToPhysAddr(params[3], &outNormal);
  for (int i = 0; i < 3; ++i)
    outNormal[i] = sp_ftoc(normal[i]);
  return sp_ftoc(z);
}

cell_t N_DispIsPointOnDisp(IPluginContext *pCtx, const cell_t *params) {
  EnsureDispLoaded();
  return BSPDisp::IsPointOnDisp(sp_ctof(params[1]), sp_ctof(params[2])) ? 1 : 0;
}

cell_t N_DispHeightAtMulti(IPluginContext *pCtx, const cell_t *params) {
  EnsureDispLoaded();
  int maxResults = params[4];
  if (maxResults <= 0)
    return 0;
  if (maxResults > 256)
    maxResults = 256;
  float tmp[256];
  int n = BSPDisp::HeightAtMulti(sp_ctof(params[1]), sp_ctof(params[2]), tmp,
                                 maxResults);
  cell_t *outResults = nullptr;
  pCtx->LocalToPhysAddr(params[3], &outResults);
  for (int i = 0; i < n; ++i)
    outResults[i] = sp_ftoc(tmp[i]);
  return n;
}

// Displacement - engine accessors (engine reader required)
cell_t N_DispReady(IPluginContext *, const cell_t *) {
  return BSPDisp::EngineReady() ? 1 : 0;
}
cell_t N_DispCount(IPluginContext *, const cell_t *) {
  return BSPDisp::EngineCount();
}

cell_t N_DispGetBounds(IPluginContext *pCtx, const cell_t *params) {
  float mins[3], maxs[3];
  bool ok = BSPDisp::EngineGetBounds(params[1], mins, maxs);
  cell_t *outMins = nullptr, *outMaxs = nullptr;
  pCtx->LocalToPhysAddr(params[2], &outMins);
  pCtx->LocalToPhysAddr(params[3], &outMaxs);
  for (int i = 0; i < 3; ++i) {
    outMins[i] = sp_ftoc(ok ? mins[i] : 0.0f);
    outMaxs[i] = sp_ftoc(ok ? maxs[i] : 0.0f);
  }
  return ok ? 1 : 0;
}

cell_t N_DispGetPower(IPluginContext *pCtx, const cell_t *params) {
  return BSPDisp::EngineGetPower(params[1]);
}

cell_t N_DispGetContents(IPluginContext *pCtx, const cell_t *params) {
  return BSPDisp::EngineGetContents(params[1]);
}

cell_t N_DispGetSurfaceProps(IPluginContext *pCtx, const cell_t *params) {
  int props[4] = {0, 0, 0, 0};
  bool ok = BSPDisp::EngineGetSurfaceProps(params[1], props);
  cell_t *outProps = nullptr;
  pCtx->LocalToPhysAddr(params[2], &outProps);
  for (int i = 0; i < 4; ++i)
    outProps[i] = props[i];
  return ok ? 1 : 0;
}

cell_t N_DispVertCount(IPluginContext *pCtx, const cell_t *params) {
  return BSPDisp::EngineVertCount(params[1]);
}
cell_t N_DispTriCount(IPluginContext *pCtx, const cell_t *params) {
  return BSPDisp::EngineTriCount(params[1]);
}

cell_t N_DispGetVert(IPluginContext *pCtx, const cell_t *params) {
  float pos[3] = {0, 0, 0};
  bool ok = BSPDisp::EngineGetVert(params[1], params[2], pos);
  cell_t *outPos = nullptr;
  pCtx->LocalToPhysAddr(params[3], &outPos);
  for (int i = 0; i < 3; ++i)
    outPos[i] = sp_ftoc(pos[i]);
  return ok ? 1 : 0;
}

cell_t N_DispDebugInfo(IPluginContext *pCtx, const cell_t *params) {
  int maxlen = params[3];
  if (maxlen <= 0)
    return 0;
  char tmp[1024];
  int n = BSPDisp::EngineDebugTreeInfo(params[1], tmp, sizeof(tmp));
  pCtx->StringToLocal(params[2], maxlen, tmp);
  return n;
}

cell_t N_DispDiagnoseQuery(IPluginContext *pCtx, const cell_t *params) {
  int maxlen = params[4];
  if (maxlen <= 0)
    return 0;
  char tmp[512];
  int n = BSPDisp::EngineDiagnoseQuery(sp_ctof(params[1]), sp_ctof(params[2]),
                                       tmp, sizeof(tmp));
  pCtx->StringToLocal(params[3], maxlen, tmp);
  return n;
}

// Displacement - disk-only (explicit fallback access)
// Disk indices != engine indices.
cell_t N_DispDiskCount(IPluginContext *, const cell_t *) {
  EnsureDispLoaded();
  return BSPDisp::DiskCount();
}

cell_t N_DispDiskBounds(IPluginContext *pCtx, const cell_t *params) {
  EnsureDispLoaded();
  float mins[3], maxs[3];
  bool ok = BSPDisp::DiskGetBounds(params[1], mins, maxs);
  cell_t *outMins = nullptr, *outMaxs = nullptr;
  pCtx->LocalToPhysAddr(params[2], &outMins);
  pCtx->LocalToPhysAddr(params[3], &outMaxs);
  for (int i = 0; i < 3; ++i) {
    outMins[i] = sp_ftoc(ok ? mins[i] : 0.0f);
    outMaxs[i] = sp_ftoc(ok ? maxs[i] : 0.0f);
  }
  return ok ? 1 : 0;
}

cell_t N_DispDiskDebugInfo(IPluginContext *pCtx, const cell_t *params) {
  EnsureDispLoaded();
  int maxlen = params[3];
  if (maxlen <= 0)
    return 0;
  char tmp[512];
  int n = BSPDisp::DiskDebugDispInfo(params[1], tmp, sizeof(tmp));
  pCtx->StringToLocal(params[2], maxlen, tmp);
  return n;
}

// BSP file lump natives
cell_t N_BSPVersion(IPluginContext *, const cell_t *) {
  EnsureLumpsLoaded();
  return BSPLumps::BSPVersion();
}

cell_t N_BSPRevision(IPluginContext *, const cell_t *) {
  EnsureLumpsLoaded();
  return BSPLumps::BSPRevision();
}

cell_t N_LumpInfo(IPluginContext *pCtx, const cell_t *params) {
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

cell_t N_EntityRawLen(IPluginContext *, const cell_t *) {
  EnsureLumpsLoaded();
  return BSPLumps::EntityRawLen();
}

cell_t N_EntityRawCopy(IPluginContext *pCtx, const cell_t *params) {
  EnsureLumpsLoaded();
  int maxlen = params[2];
  if (maxlen <= 0)
    return 0;
  std::vector<char> tmp(maxlen);
  int n = BSPLumps::EntityRawCopy(tmp.data(), maxlen);
  pCtx->StringToLocal(params[1], maxlen, tmp.data());
  return n;
}

cell_t N_EntityCount(IPluginContext *, const cell_t *) {
  EnsureLumpsLoaded();
  return BSPLumps::EntityCount();
}

cell_t N_EntityClassname(IPluginContext *pCtx, const cell_t *params) {
  EnsureLumpsLoaded();
  int maxlen = params[3];
  if (maxlen <= 0)
    return 0;
  std::vector<char> tmp(maxlen);
  int n = BSPLumps::EntityClassname(params[1], tmp.data(), maxlen);
  pCtx->StringToLocal(params[2], maxlen, tmp.data());
  return n;
}

cell_t N_EntityOrigin(IPluginContext *pCtx, const cell_t *params) {
  EnsureLumpsLoaded();
  float origin[3] = {0, 0, 0};
  bool ok = BSPLumps::EntityOrigin(params[1], origin);
  cell_t *out;
  pCtx->LocalToPhysAddr(params[2], &out);
  for (int i = 0; i < 3; ++i)
    out[i] = sp_ftoc(origin[i]);
  return ok ? 1 : 0;
}

cell_t N_EntityKeyValue(IPluginContext *pCtx, const cell_t *params) {
  EnsureLumpsLoaded();
  int maxlen = params[4];
  if (maxlen <= 0)
    return 0;
  char *key = nullptr;
  pCtx->LocalToString(params[2], &key);
  std::vector<char> tmp(maxlen);
  int n = BSPLumps::EntityKeyValue(params[1], key, tmp.data(), maxlen);
  pCtx->StringToLocal(params[3], maxlen, tmp.data());
  return n;
}

cell_t N_TexInfoCount(IPluginContext *, const cell_t *) {
  EnsureLumpsLoaded();
  return BSPLumps::TexInfoCount();
}
cell_t N_TexInfoFlags(IPluginContext *, const cell_t *params) {
  EnsureLumpsLoaded();
  return BSPLumps::TexInfoFlags(params[1]);
}
cell_t N_TexInfoTexData(IPluginContext *, const cell_t *params) {
  EnsureLumpsLoaded();
  return BSPLumps::TexInfoTexData(params[1]);
}
cell_t N_TexDataCount(IPluginContext *, const cell_t *) {
  EnsureLumpsLoaded();
  return BSPLumps::TexDataCount();
}

cell_t N_TexDataMaterialName(IPluginContext *pCtx, const cell_t *params) {
  EnsureLumpsLoaded();
  int maxlen = params[3];
  if (maxlen <= 0)
    return 0;
  std::vector<char> tmp(maxlen);
  int n = BSPLumps::TexDataMaterialName(params[1], tmp.data(), maxlen);
  pCtx->StringToLocal(params[2], maxlen, tmp.data());
  return n;
}

cell_t N_TexDataReflectivity(IPluginContext *pCtx, const cell_t *params) {
  EnsureLumpsLoaded();
  float refl[3] = {0, 0, 0};
  bool ok = BSPLumps::TexDataReflectivity(params[1], refl);
  cell_t *out;
  pCtx->LocalToPhysAddr(params[2], &out);
  for (int i = 0; i < 3; ++i)
    out[i] = sp_ftoc(refl[i]);
  return ok ? 1 : 0;
}

cell_t N_LeafFacesCount(IPluginContext *, const cell_t *) {
  EnsureLumpsLoaded();
  return BSPLumps::LeafFacesCount();
}

cell_t N_LeafFaces(IPluginContext *pCtx, const cell_t *params) {
  EnsureLumpsLoaded();
  int leafIdx = params[1];
  int firstFace = BSPData::LeafFirstFace(leafIdx);
  int numFaces = BSPData::LeafNumFaces(leafIdx);
  if (firstFace < 0 || numFaces <= 0)
    return 0;
  cell_t *buf;
  pCtx->LocalToPhysAddr(params[2], &buf);
  int maxOut = params[3];
  if (maxOut <= 0)
    return 0;
  std::vector<int> tmp(maxOut);
  int n = BSPLumps::LeafFacesRange(firstFace, numFaces, tmp.data(), maxOut);
  for (int i = 0; i < n; ++i)
    buf[i] = tmp[i];
  return n;
}

cell_t N_WorldlightCount(IPluginContext *, const cell_t *) {
  EnsureLumpsLoaded();
  return BSPLumps::WorldlightCount();
}

cell_t N_WorldlightOrigin(IPluginContext *pCtx, const cell_t *params) {
  EnsureLumpsLoaded();
  float v[3] = {0, 0, 0};
  bool ok = BSPLumps::WorldlightOrigin(params[1], v);
  cell_t *out;
  pCtx->LocalToPhysAddr(params[2], &out);
  for (int i = 0; i < 3; ++i)
    out[i] = sp_ftoc(v[i]);
  return ok ? 1 : 0;
}

cell_t N_WorldlightIntensity(IPluginContext *pCtx, const cell_t *params) {
  EnsureLumpsLoaded();
  float v[3] = {0, 0, 0};
  bool ok = BSPLumps::WorldlightIntensity(params[1], v);
  cell_t *out;
  pCtx->LocalToPhysAddr(params[2], &out);
  for (int i = 0; i < 3; ++i)
    out[i] = sp_ftoc(v[i]);
  return ok ? 1 : 0;
}

cell_t N_WorldlightNormal(IPluginContext *pCtx, const cell_t *params) {
  EnsureLumpsLoaded();
  float v[3] = {0, 0, 0};
  bool ok = BSPLumps::WorldlightNormal(params[1], v);
  cell_t *out;
  pCtx->LocalToPhysAddr(params[2], &out);
  for (int i = 0; i < 3; ++i)
    out[i] = sp_ftoc(v[i]);
  return ok ? 1 : 0;
}

cell_t N_WorldlightType(IPluginContext *, const cell_t *params) {
  EnsureLumpsLoaded();
  return BSPLumps::WorldlightType(params[1]);
}
cell_t N_WorldlightStyle(IPluginContext *, const cell_t *params) {
  EnsureLumpsLoaded();
  return BSPLumps::WorldlightStyle(params[1]);
}
cell_t N_WorldlightCluster(IPluginContext *, const cell_t *params) {
  EnsureLumpsLoaded();
  return BSPLumps::WorldlightCluster(params[1]);
}

cell_t N_WorldlightShadowCastOffset(IPluginContext *pCtx,
                                    const cell_t *params) {
  EnsureLumpsLoaded();
  float v[3] = {0, 0, 0};
  bool ok = BSPLumps::WorldlightShadowCastOffset(params[1], v);
  cell_t *out;
  pCtx->LocalToPhysAddr(params[2], &out);
  for (int i = 0; i < 3; ++i)
    out[i] = sp_ftoc(v[i]);
  return ok ? 1 : 0;
}

cell_t N_WorldlightStopDot(IPluginContext *, const cell_t *params) {
  EnsureLumpsLoaded();
  return sp_ftoc(BSPLumps::WorldlightStopDot(params[1]));
}
cell_t N_WorldlightStopDot2(IPluginContext *, const cell_t *params) {
  EnsureLumpsLoaded();
  return sp_ftoc(BSPLumps::WorldlightStopDot2(params[1]));
}
cell_t N_WorldlightExponent(IPluginContext *, const cell_t *params) {
  EnsureLumpsLoaded();
  return sp_ftoc(BSPLumps::WorldlightExponent(params[1]));
}
cell_t N_WorldlightRadius(IPluginContext *, const cell_t *params) {
  EnsureLumpsLoaded();
  return sp_ftoc(BSPLumps::WorldlightRadius(params[1]));
}
cell_t N_WorldlightConstantAttn(IPluginContext *, const cell_t *params) {
  EnsureLumpsLoaded();
  return sp_ftoc(BSPLumps::WorldlightConstantAttn(params[1]));
}
cell_t N_WorldlightLinearAttn(IPluginContext *, const cell_t *params) {
  EnsureLumpsLoaded();
  return sp_ftoc(BSPLumps::WorldlightLinearAttn(params[1]));
}
cell_t N_WorldlightQuadraticAttn(IPluginContext *, const cell_t *params) {
  EnsureLumpsLoaded();
  return sp_ftoc(BSPLumps::WorldlightQuadraticAttn(params[1]));
}
cell_t N_WorldlightFlags(IPluginContext *, const cell_t *params) {
  EnsureLumpsLoaded();
  return BSPLumps::WorldlightFlags(params[1]);
}
cell_t N_WorldlightTexInfo(IPluginContext *, const cell_t *params) {
  EnsureLumpsLoaded();
  return BSPLumps::WorldlightTexInfo(params[1]);
}
cell_t N_WorldlightOwner(IPluginContext *, const cell_t *params) {
  EnsureLumpsLoaded();
  return BSPLumps::WorldlightOwner(params[1]);
}

extern const sp_nativeinfo_t g_BSPNatives[] = {
    // Misc
    {"BSP_MapPathName", N_MapPathName},
    {"BSP_EmptyLeaf", N_EmptyLeaf},
    {"BSP_SolidLeaf", N_SolidLeaf},

    // Counts
    {"BSP_NumBrushes", N_NumBrushes},
    {"BSP_NumBrushSides", N_NumBrushSides},
    {"BSP_NumLeaves", N_NumLeaves},
    {"BSP_NumNodes", N_NumNodes},
    {"BSP_NumPlanes", N_NumPlanes},
    {"BSP_NumBoxBrushes", N_NumBoxBrushes},
    {"BSP_NumCModels", N_NumCModels},

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

    // High-level pixelsurf
    {"BSP_FindBrushPairAtSeam", N_FindBrushPairAtSeam},

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

    {"BSP_EntityRawLen", N_EntityRawLen},
    {"BSP_EntityRawCopy", N_EntityRawCopy},
    {"BSP_EntityCount", N_EntityCount},
    {"BSP_EntityClassname", N_EntityClassname},
    {"BSP_EntityOrigin", N_EntityOrigin},
    {"BSP_EntityKeyValue", N_EntityKeyValue},

    {"BSP_TexInfoCount", N_TexInfoCount},
    {"BSP_TexInfoFlags", N_TexInfoFlags},
    {"BSP_TexInfoTexData", N_TexInfoTexData},
    {"BSP_TexDataCount", N_TexDataCount},
    {"BSP_TexDataMaterialName", N_TexDataMaterialName},
    {"BSP_TexDataReflectivity", N_TexDataReflectivity},

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

    {nullptr, nullptr},
};
