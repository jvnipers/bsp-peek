#if defined _bsppeek_included
    #endinput
#endif
#define _bsppeek_included

// BSP-Peek - "peek" into engine CCollisionBSPData
// (leaf membership, ordered brush lists, brush type, AABB, BSP node-walk).
// Requires bsppeek.ext loaded.
// All natives return safe defaults if the ext isn't loaded;
// check BSPPeek_Available() first if you need certainty.

// Source-engine CONTENTS_* constants commonly checked:
#define BSPP_CONTENTS_SOLID      0x1
#define BSPP_CONTENTS_PLAYERCLIP 0x10000
#define BSPP_CONTENTS_GRATE      0x8

native int  BSP_LeafAtPoint(const float pos[3]);              // O(log N) BSP node-walk; -1 on failure
native int  BSP_LeafBrushes(int leaf, int[] buf, int max);    // BSP visit order; -1 on bad leafIdx
// Heuristic only - CSGO splits box brushes into a parallel `cboxbrush_t` table
// so the result can false-negative. Treat as informational, not a hard filter.
native bool BSP_IsBoxBrush(int brushIdx);
native int  BSP_BrushContents(int brushIdx);
native void BSP_BrushBounds(int brushIdx, float mins[3], float maxs[3]);
native int  BSP_NumBrushes();
native int  BSP_NumLeaves();

// Brush-pair detection used for pixelsurf filtering.
// Iterates all brushes for any whose XY AABB contains samplePos AND whose top/bottom equals seamZ.
// Returns true if BOTH found. outLower = brush w/ maxs.z == seamZ, outUpper = brush w/ mins.z == seamZ.
native bool BSP_FindBrushPairAtSeam(const float samplePos[3], float seamZ,
                                    int &outLowerBrush, int &outUpperBrush);

// Force a fresh build of the per-map brush AABB cache (synchronous; blocks).
// Returns the brush count after rebuild (0 = ext unable to read BSP data).
native int  BSP_RebuildCache();

// Async cache build - kicks off a worker thread, returns immediately.
// Reads via BSP_FindBrushPairAtSeam block until the worker swaps the cache in.
// Use this in OnMapStart to avoid stalling the server during map load.
// Returns brush count (or 0 if no BSP data).
native int  BSP_RebuildCacheAsync();

// True between BSP_RebuildCacheAsync() and the worker's swap-in.
native bool BSP_CacheIsBuilding();

stock bool  BSPPeek_Available()
{
    return LibraryExists("bsppeek");
}

public Extension __ext_bsppeek = {
    name     = "BSP-Peek",
    file     = "bsppeek.ext",
    autoload = 1,
    required = 0,
};
