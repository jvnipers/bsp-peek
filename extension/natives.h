#ifndef _INCLUDE_BSPPEEK_NATIVES_H_
#define _INCLUDE_BSPPEEK_NATIVES_H_

#include "smsdk_ext.h"

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

#endif
