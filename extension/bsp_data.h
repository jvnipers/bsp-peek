#ifndef _INCLUDE_BSPPEEK_BSP_DATA_H_
#define _INCLUDE_BSPPEEK_BSP_DATA_H_

#include "smsdk_ext.h"
#include <cstdint>

namespace BSPData {
bool Init(IGameConfig *gameconf, char *error, size_t maxlen);
void Shutdown();
void OnMapStart();

void *DebugGetBase();
int DebugGetOff(const char *name);

int GetNumBrushes();
int GetNumBrushSides();
int GetNumPlanes();
int GetNumLeafBrushes();
int GetNumLeaves();

bool GetBrushBounds(int brushIdx, float mins[3], float maxs[3]);
int GetBrushContents(int brushIdx);
// Heuristic: numsides==6 + all 6 plane normals axis-aligned.
// CSGO splits true box brushes into a parallel `cboxbrush_t` table,
// so this can false-negative on box-optimized brushes.
// For pixelsurfs: Treat as informational, not a hard pixelsurf filter.
bool IsBoxBrush(int brushIdx);

// O(log N) BSP node-walk via `map_nodes` table.
// Returns leaf index, or -1 on failure (out-of-bounds, malformed node tree).
int LeafAtPoint(const float pos[3]);
// Brushes belonging to a leaf in BSP visit order
// (engine processes collision in this order - first SOLID hit wins.
int LeafBrushes(int leafIdx, int *outBuf, int maxOut);

// Force rebuild of the per-map brush AABB cache.
// Plugin should call once in OnMapStart
// so the first user-triggered query doesn't pay the build cost.
// Probably want to use the async version to avoid stalling the server during
// map load.
void RebuildCache();

// Same as RebuildCache() but builds on a worker thread; returns immediately.
// Brush queries during the build see g_brushCacheBuilt=false until the worker
// swaps the fresh cache in. Calling while a build is in flight is a no-op.
void RebuildCacheAsync();

// True between RebuildCacheAsync() and the worker's swap-in.
bool CacheIsBuilding();

// High-level pixelsurf pair finder. For a sample point and seam Z,
// search all brushes for:
//   - lowerBrush: brush whose maxs.z == seamZ and which contains samplePos.xy
//   - upperBrush: brush whose mins.z == seamZ and which contains samplePos.xy
// Returns true and fills outLower/outUpper indices on success.
bool FindBrushPairAtSeam(const float samplePos[3], float seamZ, int &outLower,
                         int &outUpper);
} // namespace BSPData

#endif
