#ifndef _INCLUDE_BSPPEEK_NATIVES_H_
#define _INCLUDE_BSPPEEK_NATIVES_H_

#include "smsdk_ext.h"

// Brush/leaf
cell_t N_LeafAtPoint(IPluginContext *pCtx, const cell_t *params);
cell_t N_LeafBrushes(IPluginContext *pCtx, const cell_t *params);
cell_t N_IsBoxBrush(IPluginContext *pCtx, const cell_t *params);
cell_t N_BrushContents(IPluginContext *pCtx, const cell_t *params);
cell_t N_BrushBounds(IPluginContext *pCtx, const cell_t *params);
cell_t N_NumBrushes(IPluginContext *pCtx, const cell_t *params);
cell_t N_NumLeaves(IPluginContext *pCtx, const cell_t *params);
cell_t N_FindBrushPairAtSeam(IPluginContext *pCtx, const cell_t *params);
cell_t N_RebuildCache(IPluginContext *pCtx, const cell_t *params);
cell_t N_RebuildCacheAsync(IPluginContext *pCtx, const cell_t *params);
cell_t N_CacheIsBuilding(IPluginContext *pCtx, const cell_t *params);

// Unified disp (engine-first, disk fallback)
cell_t N_DispHeightAt(IPluginContext *pCtx, const cell_t *params);
cell_t N_DispHeightAtDebug(IPluginContext *pCtx, const cell_t *params);
cell_t N_DispSurfaceNormalAt(IPluginContext *pCtx, const cell_t *params);
cell_t N_DispIsPointOnDisp(IPluginContext *pCtx, const cell_t *params);
cell_t N_DispHeightAtMulti(IPluginContext *pCtx, const cell_t *params);

// Engine accessors
cell_t N_DispReady(IPluginContext *pCtx, const cell_t *params);
cell_t N_DispCount(IPluginContext *pCtx, const cell_t *params);
cell_t N_DispDebugInfo(IPluginContext *pCtx, const cell_t *params);
cell_t N_DispDiagnoseQuery(IPluginContext *pCtx, const cell_t *params);
cell_t N_DispGetBounds(IPluginContext *pCtx, const cell_t *params);
cell_t N_DispGetPower(IPluginContext *pCtx, const cell_t *params);
cell_t N_DispGetContents(IPluginContext *pCtx, const cell_t *params);
cell_t N_DispGetSurfaceProps(IPluginContext *pCtx, const cell_t *params);
cell_t N_DispVertCount(IPluginContext *pCtx, const cell_t *params);
cell_t N_DispTriCount(IPluginContext *pCtx, const cell_t *params);
cell_t N_DispGetVert(IPluginContext *pCtx, const cell_t *params);

// Disk-only
cell_t N_DispDiskCount(IPluginContext *pCtx, const cell_t *params);
cell_t N_DispDiskBounds(IPluginContext *pCtx, const cell_t *params);
cell_t N_DispDiskDebugInfo(IPluginContext *pCtx, const cell_t *params);

#endif
