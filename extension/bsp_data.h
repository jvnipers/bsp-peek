#ifndef _INCLUDE_BSPPEEK_BSP_DATA_H_
#define _INCLUDE_BSPPEEK_BSP_DATA_H_

#include "smsdk_ext.h"
#include <cstdint>

// BSP collision data accessor (CCollisionBSPData).
// Reads brush/leaf/plane/node tables directly from engine memory
// via gamedata offsets.
namespace BSPData {
// Lifecycle
bool Init(IGameConfig *gameconf, char *error, size_t maxlen);
void Shutdown();
void OnMapStart();

// Debug accessors (used by extension.cpp load logging)
void *DebugGetBase();
int DebugGetOff(const char *name);
// Raw hex dump of first N box brushes for layout RE. Logs via smutils.
void DebugDumpBoxBrushes(int maxCount);

// Misc accessors
int MapPathName(char *buf, int maxlen);
int EmptyLeaf();
int SolidLeaf();

// Counts
int GetNumBrushes();
int GetNumBrushSides();
int GetNumPlanes();
int GetNumLeafBrushes();
int GetNumLeaves();
int GetNumNodes();
int GetNumBoxBrushes();
int GetNumCModels(); // Submodels (func_brush etc.)

// Point queries
// O(log N) BSP node-walk via `map_nodes`. Returns leaf index, -1 on failure.
int LeafAtPoint(const float pos[3]);
// LeafAtPoint + LeafContents in one call. Returns contents flags, 0 on failure.
int PointContents(const float pos[3]);

// Brush accessors
int GetBrushContents(int brushIdx);
bool GetBrushBounds(int brushIdx, float mins[3], float maxs[3]);
// Heuristic: numsides==6 + all 6 plane normals axis-aligned.
// CSGO splits true box brushes into a parallel `cboxbrush_t` table,
// so this can false-negative on box-optimized brushes.
// For pixelsurfs: treat as informational, not a hard filter.
bool IsBoxBrush(int brushIdx);
int BrushNumSides(int brushIdx);
// Read one side's plane normal + signed distance.
// sideIdx in [0, BrushNumSides(brushIdx)).
bool BrushSidePlane(int brushIdx, int sideIdx, float normal[3], float &dist);
// Is this side a bevel plane? (0=no, 1=yes, -1=invalid).
int BrushSideBevel(int brushIdx, int sideIdx);
// Is this a thin side? (0=no, 1=yes, -1=invalid).
int BrushSideThin(int brushIdx, int sideIdx);
// Texinfo index for this side. -1 if invalid.
int BrushSideTexInfo(int brushIdx, int sideIdx);

// Leaf accessors
// Brushes belonging to a leaf in BSP visit order (first SOLID hit wins).
int LeafBrushes(int leafIdx, int *outBuf, int maxOut);
// Contents flags (CONTENTS_SOLID etc) at cleaf_t+0. 0 if invalid leafIdx.
int LeafContents(int leafIdx);
// PVS cluster index at cleaf_t+4 (short). -1 if invalid.
int LeafCluster(int leafIdx);
// Area portal grouping (low 9 bits of cleaf_t+6 short). -1 if invalid.
int LeafArea(int leafIdx);
// Leaf flags (high 7 bits of cleaf_t+6 short). -1 if invalid.
int LeafFlags(int leafIdx);
// First leaf face index in the leaf-face table. -1 if invalid.
int LeafFirstFace(int leafIdx);
// Number of faces in this leaf. -1 if invalid.
int LeafNumFaces(int leafIdx);
// Leaf AABB via BSP tree-walk. Builds leaf cache lazily on first call.
bool LeafBounds(int leafIdx, float mins[3], float maxs[3]);

// Node accessors (manual BSP walking)
// Splitting plane of node[idx]. Returns false on invalid idx.
bool NodePlane(int nodeIdx, float normal[3], float &dist);
// Children of node[idx]. Negative child = leaf via `leafIdx = -1 - child`.
bool NodeChildren(int nodeIdx, int &leftChild, int &rightChild);
// Node AABB via BSP tree-walk (same cache as LeafBounds).
bool NodeBounds(int nodeIdx, float mins[3], float maxs[3]);

// Plane table access
// planeIdx in [0, GetNumPlanes()).
bool PlaneAt(int planeIdx, float normal[3], float &dist);
// PLANE_X(0)/Y(1)/Z(2)/ANYX(3)/ANYY(4)/ANYZ(5). -1 if invalid.
int PlaneType(int planeIdx);

// Box brush (cboxbrush_t) accessors - SIMD-optimized axis-aligned brushes.
// idx in [0, GetNumBoxBrushes()).
bool BoxBrushBounds(int idx, float mins[3], float maxs[3]);
// Original cbrush_t index this box brush was derived from. -1 if invalid.
int BoxBrushOriginalBrush(int idx);
// Per-face surface property indices (-X,+X,-Y,+Y,-Z,+Z).
// false if invalid.
bool BoxBrushSurfaceIndex(int idx, int outSurf[6]);
// Contents via originalBrush lookup. 0 if invalid.
int BoxBrushContents(int idx);

// Submodel (cmodel_t) accessors - used for func_brush, doors, breakables etc.
bool CModelBounds(int idx, float mins[3], float maxs[3]);
bool CModelOrigin(int idx, float origin[3]);
// Root BSP node for this submodel's collision tree. -1 if invalid.
int CModelHeadnode(int idx);

// "High-level" pixelsurf
// For a sample point and seam Z, search all brushes for:
//   lowerBrush: maxs.z == seamZ AND XY AABB contains samplePos
//   upperBrush: mins.z == seamZ AND XY AABB contains samplePos
// Returns true and fills outLower/outUpper on success.
bool FindBrushPairAtSeam(const float samplePos[3], float seamZ, int &outLower,
                         int &outUpper);

// Brush AABB cache
// Plugin should call once in OnMapStart so the first user-triggered query
// doesn't pay the build cost. Prefer async to avoid stalling map load.
void RebuildCache();

// Builds on worker thread, returns immediately. Reads block until swap-in.
// Calling while a build is in flight is a no-op.
void RebuildCacheAsync();

// True between RebuildCacheAsync() and the worker's swap-in.
bool CacheIsBuilding();

} // namespace BSPData
#endif
