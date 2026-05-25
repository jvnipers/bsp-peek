#include "natives.h"
#include "bsp_data.h"
#include <sp_vm_api.h>

static inline void cell_to_float3(IPluginContext *pCtx, cell_t addr,
                                  float out[3]) {
  cell_t *src = nullptr;
  pCtx->LocalToPhysAddr(addr, &src);
  out[0] = sp_ctof(src[0]);
  out[1] = sp_ctof(src[1]);
  out[2] = sp_ctof(src[2]);
}

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
  if (!BSPData::GetBrushBounds(params[1], mins, maxs)) {
    mins[0] = mins[1] = mins[2] = 0.0f;
    maxs[0] = maxs[1] = maxs[2] = 0.0f;
  }
  cell_t *outMins = nullptr;
  cell_t *outMaxs = nullptr;
  pCtx->LocalToPhysAddr(params[2], &outMins);
  pCtx->LocalToPhysAddr(params[3], &outMaxs);
  for (int i = 0; i < 3; ++i) {
    outMins[i] = sp_ftoc(mins[i]);
    outMaxs[i] = sp_ftoc(maxs[i]);
  }
  return 0;
}

cell_t N_NumBrushes(IPluginContext *pCtx, const cell_t *params) {
  return BSPData::GetNumBrushes();
}

cell_t N_NumLeaves(IPluginContext *pCtx, const cell_t *params) {
  return BSPData::GetNumLeaves();
}

cell_t N_RebuildCache(IPluginContext *pCtx, const cell_t *params) {
  BSPData::RebuildCache();
  return BSPData::GetNumBrushes();
}

cell_t N_RebuildCacheAsync(IPluginContext *pCtx, const cell_t *params) {
  BSPData::RebuildCacheAsync();
  return BSPData::GetNumBrushes();
}

cell_t N_CacheIsBuilding(IPluginContext *pCtx, const cell_t *params) {
  return BSPData::CacheIsBuilding() ? 1 : 0;
}

cell_t N_FindBrushPairAtSeam(IPluginContext *pCtx, const cell_t *params) {
  float p[3];
  cell_to_float3(pCtx, params[1], p);
  float seamZ = sp_ctof(params[2]);
  int lower = -1, upper = -1;
  bool ok = BSPData::FindBrushPairAtSeam(p, seamZ, lower, upper);
  cell_t *outLower = nullptr;
  cell_t *outUpper = nullptr;
  pCtx->LocalToPhysAddr(params[3], &outLower);
  pCtx->LocalToPhysAddr(params[4], &outUpper);
  *outLower = lower;
  *outUpper = upper;
  return ok ? 1 : 0;
}

extern const sp_nativeinfo_t g_BSPNatives[] = {
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
    {nullptr, nullptr},
};
