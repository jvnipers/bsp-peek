#ifndef _INCLUDE_BSPPEEK_NATIVES_H_
#define _INCLUDE_BSPPEEK_NATIVES_H_

#include "smsdk_ext.h"

// Counts
cell_t N_NumBrushes(IPluginContext *pCtx, const cell_t *params);
cell_t N_NumBrushSides(IPluginContext *pCtx, const cell_t *params);
cell_t N_NumLeaves(IPluginContext *pCtx, const cell_t *params);
cell_t N_NumNodes(IPluginContext *pCtx, const cell_t *params);
cell_t N_NumPlanes(IPluginContext *pCtx, const cell_t *params);
cell_t N_NumBoxBrushes(IPluginContext *pCtx, const cell_t *params);
cell_t N_NumCModels(IPluginContext *pCtx, const cell_t *params);

// Point queries
cell_t N_LeafAtPoint(IPluginContext *pCtx, const cell_t *params);
cell_t N_PointContents(IPluginContext *pCtx, const cell_t *params);

// Brush accessors
cell_t N_BrushContents(IPluginContext *pCtx, const cell_t *params);
cell_t N_BrushBounds(IPluginContext *pCtx, const cell_t *params);
cell_t N_IsBoxBrush(IPluginContext *pCtx, const cell_t *params);
cell_t N_BrushNumSides(IPluginContext *pCtx, const cell_t *params);
cell_t N_BrushSidePlane(IPluginContext *pCtx, const cell_t *params);

// Leaf accessors
cell_t N_LeafBrushes(IPluginContext *pCtx, const cell_t *params);
cell_t N_LeafContents(IPluginContext *pCtx, const cell_t *params);
cell_t N_LeafCluster(IPluginContext *pCtx, const cell_t *params);
cell_t N_LeafArea(IPluginContext *pCtx, const cell_t *params);
cell_t N_LeafFlags(IPluginContext *pCtx, const cell_t *params);
cell_t N_LeafBounds(IPluginContext *pCtx, const cell_t *params);

// Node accessors
cell_t N_NodePlane(IPluginContext *pCtx, const cell_t *params);
cell_t N_NodeChildren(IPluginContext *pCtx, const cell_t *params);

// Plane access
cell_t N_PlaneAt(IPluginContext *pCtx, const cell_t *params);

// Box brush (cboxbrush_t)
cell_t N_BoxBrushBounds(IPluginContext *pCtx, const cell_t *params);
cell_t N_BoxBrushOriginalBrush(IPluginContext *pCtx, const cell_t *params);
cell_t N_BoxBrushSurfaceIndex(IPluginContext *pCtx, const cell_t *params);

// Submodels (cmodel_t)
cell_t N_CModelBounds(IPluginContext *pCtx, const cell_t *params);
cell_t N_CModelOrigin(IPluginContext *pCtx, const cell_t *params);
cell_t N_CModelHeadnode(IPluginContext *pCtx, const cell_t *params);

// High-level pixelsurf
cell_t N_FindBrushPairAtSeam(IPluginContext *pCtx, const cell_t *params);

// Brush AABB cache
cell_t N_RebuildCache(IPluginContext *pCtx, const cell_t *params);
cell_t N_RebuildCacheAsync(IPluginContext *pCtx, const cell_t *params);
cell_t N_CacheIsBuilding(IPluginContext *pCtx, const cell_t *params);

// Displacement (engine-first, disk fallback)
cell_t N_DispHeightAt(IPluginContext *pCtx, const cell_t *params);
cell_t N_DispHeightAtDebug(IPluginContext *pCtx, const cell_t *params);
cell_t N_DispSurfaceNormalAt(IPluginContext *pCtx, const cell_t *params);
cell_t N_DispIsPointOnDisp(IPluginContext *pCtx, const cell_t *params);
cell_t N_DispHeightAtMulti(IPluginContext *pCtx, const cell_t *params);

// Displacement (engine accessors)
cell_t N_DispReady(IPluginContext *pCtx, const cell_t *params);
cell_t N_DispCount(IPluginContext *pCtx, const cell_t *params);
cell_t N_DispGetBounds(IPluginContext *pCtx, const cell_t *params);
cell_t N_DispGetPower(IPluginContext *pCtx, const cell_t *params);
cell_t N_DispGetContents(IPluginContext *pCtx, const cell_t *params);
cell_t N_DispGetSurfaceProps(IPluginContext *pCtx, const cell_t *params);
cell_t N_DispVertCount(IPluginContext *pCtx, const cell_t *params);
cell_t N_DispTriCount(IPluginContext *pCtx, const cell_t *params);
cell_t N_DispGetVert(IPluginContext *pCtx, const cell_t *params);
cell_t N_DispDebugInfo(IPluginContext *pCtx, const cell_t *params);
cell_t N_DispDiagnoseQuery(IPluginContext *pCtx, const cell_t *params);

// Displacement (disk-only)
cell_t N_DispDiskCount(IPluginContext *pCtx, const cell_t *params);
cell_t N_DispDiskBounds(IPluginContext *pCtx, const cell_t *params);
cell_t N_DispDiskDebugInfo(IPluginContext *pCtx, const cell_t *params);

#endif
