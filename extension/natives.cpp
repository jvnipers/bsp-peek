#include "natives.h"
#include "bsp_data.h"
#include "bsp_disp.h"
#include <IGameHelpers.h>
#include <cmath>
#include <cstring>
#include <sp_vm_api.h>

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

// Brush/Leaf natives

cell_t N_LeafAtPoint(IPluginContext *pCtx, const cell_t *params) {
  float p[3];
  cell_to_float3(pCtx, params[1], p);
  return BSPData::LeafAtPoint(p);
}

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

cell_t N_IsBoxBrush(IPluginContext *pCtx, const cell_t *params) {
  return BSPData::IsBoxBrush(params[1]) ? 1 : 0;
}

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

cell_t N_NumBrushes(IPluginContext *, const cell_t *) {
  return BSPData::GetNumBrushes();
}
cell_t N_NumLeaves(IPluginContext *, const cell_t *) {
  return BSPData::GetNumLeaves();
}

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

// Unified displacement natives (engine-first, disk fallback)

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

// Engine accessors (no fallback; engine reader required)

cell_t N_DispReady(IPluginContext *, const cell_t *) {
  return BSPDisp::EngineReady() ? 1 : 0;
}
cell_t N_DispCount(IPluginContext *, const cell_t *) {
  return BSPDisp::EngineCount();
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

// Disk-only natives (explicit fallback access; disk indices != engine indices)

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

extern const sp_nativeinfo_t g_BSPNatives[] = {
    // Brush/leaf
    {"BSP_LeafAtPoint", N_LeafAtPoint},
    {"BSP_LeafBrushes", N_LeafBrushes},
    {"BSP_IsBoxBrush", N_IsBoxBrush},
    {"BSP_BrushContents", N_BrushContents},
    {"BSP_BrushBounds", N_BrushBounds},
    {"BSP_NumBrushes", N_NumBrushes},
    {"BSP_NumLeaves", N_NumLeaves},
    {"BSP_FindBrushPairAtSeam", N_FindBrushPairAtSeam},
    {"BSP_RebuildCache", N_RebuildCache},
    {"BSP_RebuildCacheAsync", N_RebuildCacheAsync},
    {"BSP_CacheIsBuilding", N_CacheIsBuilding},
    // Unified disp (engine-first, disk fallback)
    {"BSP_DispHeightAt", N_DispHeightAt},
    {"BSP_DispHeightAtDebug", N_DispHeightAtDebug},
    {"BSP_DispSurfaceNormalAt", N_DispSurfaceNormalAt},
    {"BSP_DispIsPointOnDisp", N_DispIsPointOnDisp},
    {"BSP_DispHeightAtMulti", N_DispHeightAtMulti},
    // Engine accessors (engine reader required)
    {"BSP_DispReady", N_DispReady},
    {"BSP_DispCount", N_DispCount},
    {"BSP_DispDebugInfo", N_DispDebugInfo},
    {"BSP_DispDiagnoseQuery", N_DispDiagnoseQuery},
    {"BSP_DispGetBounds", N_DispGetBounds},
    {"BSP_DispGetPower", N_DispGetPower},
    {"BSP_DispGetContents", N_DispGetContents},
    {"BSP_DispGetSurfaceProps", N_DispGetSurfaceProps},
    {"BSP_DispVertCount", N_DispVertCount},
    {"BSP_DispTriCount", N_DispTriCount},
    {"BSP_DispGetVert", N_DispGetVert},
    // Disk-only explicit access
    {"BSP_DispDiskCount", N_DispDiskCount},
    {"BSP_DispDiskBounds", N_DispDiskBounds},
    {"BSP_DispDiskDebugInfo", N_DispDiskDebugInfo},
    {nullptr, nullptr},
};
