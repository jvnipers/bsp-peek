#ifndef _INCLUDE_BSPPEEK_NATIVES_H_
#define _INCLUDE_BSPPEEK_NATIVES_H_

#include "smsdk_ext.h"

// Misc
cell_t N_MapPathName(IPluginContext *pCtx, const cell_t *params);
cell_t N_EmptyLeaf(IPluginContext *pCtx, const cell_t *params);
cell_t N_SolidLeaf(IPluginContext *pCtx, const cell_t *params);

// Debug helpers (no SP wrappers in stable header; informational logs)
cell_t N_DebugDumpCBSP(IPluginContext *pCtx, const cell_t *params);
cell_t N_DebugDumpCBSPPtr(IPluginContext *pCtx, const cell_t *params);

// Counts
cell_t N_NumBrushes(IPluginContext *pCtx, const cell_t *params);
cell_t N_NumBrushSides(IPluginContext *pCtx, const cell_t *params);
cell_t N_NumLeaves(IPluginContext *pCtx, const cell_t *params);
cell_t N_NumNodes(IPluginContext *pCtx, const cell_t *params);
cell_t N_NumPlanes(IPluginContext *pCtx, const cell_t *params);
cell_t N_NumBoxBrushes(IPluginContext *pCtx, const cell_t *params);
cell_t N_NumCModels(IPluginContext *pCtx, const cell_t *params);
cell_t N_NumAreas(IPluginContext *pCtx, const cell_t *params);
cell_t N_NumAreaPortals(IPluginContext *pCtx, const cell_t *params);
cell_t N_NumClusters(IPluginContext *pCtx, const cell_t *params);

// Visibility (PVS)
cell_t N_ClustersVisible(IPluginContext *pCtx, const cell_t *params);
cell_t N_LeavesVisible(IPluginContext *pCtx, const cell_t *params);
cell_t N_VisRowDecompress(IPluginContext *pCtx, const cell_t *params);

// Areas / area portals
cell_t N_AreaInfo(IPluginContext *pCtx, const cell_t *params);
cell_t N_AreaPortalInfo(IPluginContext *pCtx, const cell_t *params);

// Point queries
cell_t N_LeafAtPoint(IPluginContext *pCtx, const cell_t *params);
cell_t N_PointContents(IPluginContext *pCtx, const cell_t *params);

// Brush accessors
cell_t N_BrushContents(IPluginContext *pCtx, const cell_t *params);
cell_t N_BrushBounds(IPluginContext *pCtx, const cell_t *params);
cell_t N_IsBoxBrush(IPluginContext *pCtx, const cell_t *params);
cell_t N_BrushNumSides(IPluginContext *pCtx, const cell_t *params);
cell_t N_BrushSidePlane(IPluginContext *pCtx, const cell_t *params);
cell_t N_BrushSideBevel(IPluginContext *pCtx, const cell_t *params);
cell_t N_BrushSideThin(IPluginContext *pCtx, const cell_t *params);
cell_t N_BrushSideTexInfo(IPluginContext *pCtx, const cell_t *params);

// Leaf accessors
cell_t N_LeafBrushes(IPluginContext *pCtx, const cell_t *params);
cell_t N_LeafContents(IPluginContext *pCtx, const cell_t *params);
cell_t N_LeafCluster(IPluginContext *pCtx, const cell_t *params);
cell_t N_LeafArea(IPluginContext *pCtx, const cell_t *params);
cell_t N_LeafFlags(IPluginContext *pCtx, const cell_t *params);
cell_t N_LeafFirstFace(IPluginContext *pCtx, const cell_t *params);
cell_t N_LeafNumFaces(IPluginContext *pCtx, const cell_t *params);
cell_t N_LeafBounds(IPluginContext *pCtx, const cell_t *params);

// Node accessors
cell_t N_NodePlane(IPluginContext *pCtx, const cell_t *params);
cell_t N_NodeChildren(IPluginContext *pCtx, const cell_t *params);
cell_t N_NodeBounds(IPluginContext *pCtx, const cell_t *params);

// Plane access
cell_t N_PlaneAt(IPluginContext *pCtx, const cell_t *params);
cell_t N_PlaneType(IPluginContext *pCtx, const cell_t *params);

// Box brush (cboxbrush_t)
cell_t N_BoxBrushBounds(IPluginContext *pCtx, const cell_t *params);
cell_t N_BoxBrushOriginalBrush(IPluginContext *pCtx, const cell_t *params);
cell_t N_BoxBrushSurfaceIndex(IPluginContext *pCtx, const cell_t *params);
cell_t N_BoxBrushContents(IPluginContext *pCtx, const cell_t *params);

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

// BSP file lump natives
cell_t N_BSPVersion(IPluginContext *pCtx, const cell_t *params);
cell_t N_BSPRevision(IPluginContext *pCtx, const cell_t *params);
cell_t N_LumpInfo(IPluginContext *pCtx, const cell_t *params);
cell_t N_HasLighting(IPluginContext *pCtx, const cell_t *params);

cell_t N_EntityRawLen(IPluginContext *pCtx, const cell_t *params);
cell_t N_EntityRawCopy(IPluginContext *pCtx, const cell_t *params);
cell_t N_EntityCount(IPluginContext *pCtx, const cell_t *params);
cell_t N_EntityClassname(IPluginContext *pCtx, const cell_t *params);
cell_t N_EntityOrigin(IPluginContext *pCtx, const cell_t *params);
cell_t N_EntityKeyValue(IPluginContext *pCtx, const cell_t *params);

cell_t N_TexInfoCount(IPluginContext *pCtx, const cell_t *params);
cell_t N_TexInfoFlags(IPluginContext *pCtx, const cell_t *params);
cell_t N_TexInfoTexData(IPluginContext *pCtx, const cell_t *params);
cell_t N_TexDataCount(IPluginContext *pCtx, const cell_t *params);
cell_t N_TexDataMaterialName(IPluginContext *pCtx, const cell_t *params);
cell_t N_TexDataReflectivity(IPluginContext *pCtx, const cell_t *params);

cell_t N_FaceVertex(IPluginContext *pCtx, const cell_t *params);
cell_t N_FaceCentroid(IPluginContext *pCtx, const cell_t *params);
cell_t N_FaceMaterialName(IPluginContext *pCtx, const cell_t *params);
cell_t N_LeafVisible(IPluginContext *pCtx, const cell_t *params);
cell_t N_NearestCubemap(IPluginContext *pCtx, const cell_t *params);
cell_t N_FindEntityByKeyValue(IPluginContext *pCtx, const cell_t *params);

cell_t N_VisClusterCount(IPluginContext *pCtx, const cell_t *params);
cell_t N_ClusterVisible(IPluginContext *pCtx, const cell_t *params);

cell_t N_CubemapCount(IPluginContext *pCtx, const cell_t *params);
cell_t N_CubemapOrigin(IPluginContext *pCtx, const cell_t *params);
cell_t N_CubemapSize(IPluginContext *pCtx, const cell_t *params);

cell_t N_EdgeCount(IPluginContext *pCtx, const cell_t *params);
cell_t N_EdgeVertices(IPluginContext *pCtx, const cell_t *params);
cell_t N_SurfedgeCount(IPluginContext *pCtx, const cell_t *params);
cell_t N_Surfedge(IPluginContext *pCtx, const cell_t *params);
cell_t N_SurfedgeVertex(IPluginContext *pCtx, const cell_t *params);

cell_t N_VertexCount(IPluginContext *pCtx, const cell_t *params);
cell_t N_VertexPos(IPluginContext *pCtx, const cell_t *params);

cell_t N_FaceCount(IPluginContext *pCtx, const cell_t *params);
cell_t N_FacePlaneNum(IPluginContext *pCtx, const cell_t *params);
cell_t N_FaceFirstEdge(IPluginContext *pCtx, const cell_t *params);
cell_t N_FaceNumEdges(IPluginContext *pCtx, const cell_t *params);
cell_t N_FaceTexInfo(IPluginContext *pCtx, const cell_t *params);
cell_t N_FaceDispInfo(IPluginContext *pCtx, const cell_t *params);
cell_t N_FaceArea(IPluginContext *pCtx, const cell_t *params);
cell_t N_FaceLightStyles(IPluginContext *pCtx, const cell_t *params);
cell_t N_FaceOrigFace(IPluginContext *pCtx, const cell_t *params);
cell_t N_FaceLightOfs(IPluginContext *pCtx, const cell_t *params);

cell_t N_LeafFacesCount(IPluginContext *pCtx, const cell_t *params);
cell_t N_LeafFaces(IPluginContext *pCtx, const cell_t *params);

cell_t N_WorldlightCount(IPluginContext *pCtx, const cell_t *params);
cell_t N_WorldlightOrigin(IPluginContext *pCtx, const cell_t *params);
cell_t N_WorldlightIntensity(IPluginContext *pCtx, const cell_t *params);
cell_t N_WorldlightNormal(IPluginContext *pCtx, const cell_t *params);
cell_t N_WorldlightType(IPluginContext *pCtx, const cell_t *params);
cell_t N_WorldlightStyle(IPluginContext *pCtx, const cell_t *params);
cell_t N_WorldlightCluster(IPluginContext *pCtx, const cell_t *params);
cell_t N_WorldlightShadowCastOffset(IPluginContext *pCtx, const cell_t *params);
cell_t N_WorldlightStopDot(IPluginContext *pCtx, const cell_t *params);
cell_t N_WorldlightStopDot2(IPluginContext *pCtx, const cell_t *params);
cell_t N_WorldlightExponent(IPluginContext *pCtx, const cell_t *params);
cell_t N_WorldlightRadius(IPluginContext *pCtx, const cell_t *params);
cell_t N_WorldlightConstantAttn(IPluginContext *pCtx, const cell_t *params);
cell_t N_WorldlightLinearAttn(IPluginContext *pCtx, const cell_t *params);
cell_t N_WorldlightQuadraticAttn(IPluginContext *pCtx, const cell_t *params);
cell_t N_WorldlightFlags(IPluginContext *pCtx, const cell_t *params);
cell_t N_WorldlightTexInfo(IPluginContext *pCtx, const cell_t *params);
cell_t N_WorldlightOwner(IPluginContext *pCtx, const cell_t *params);

#endif
