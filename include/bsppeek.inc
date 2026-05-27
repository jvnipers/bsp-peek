#if defined _bsppeek_included
    #endinput
#endif
#define _bsppeek_included

// BSP-Peek - "peek" into engine CCollisionBSPData
// (leaf membership, ordered brush lists, brush type, AABB, BSP node-walk).
// Plus displacement surface queries via engine memory reader (primary)
// with BSP file-parse fallback when engine reader unavailable.
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

// Displacements
// Primary path: reads CDispCollTree array directly from engine memory
//   (canonical post-stitching verts; matches what TR_TraceRay would hit).
// Fallback: parses BSP file lumps (DISPINFO/DISP_VERTS/FACES/etc) at map load.
// Check BSP_DispReady() to know if the engine reader is initialized; if not,
// unified queries fall back to disk-parsed data automatically.

#define BSP_DISP_NO_HIT -1.0e30    // sentinel returned by BSP_DispHeightAt for no XY match

// - Unified queries (engine-first, disk fallback) -

// Highest world-space Z at (x, y) across all displacements that contain
// the XY query point. Returns BSP_DISP_NO_HIT (-1e30) if none.
native float BSP_DispHeightAt(float x, float y);

// Same as BSP_DispHeightAt + returns the matching disp/tree index (-1 if none).
native float BSP_DispHeightAtDebug(float x, float y, int &outIdx);

// Returns surface Z + populates normal[3] with the tri's plane normal at (x,y).
// Returns BSP_DISP_NO_HIT if no match.
native float BSP_DispSurfaceNormalAt(float x, float y, float normal[3]);

// True if any disp tri actually contains (x,y).
native bool  BSP_DispIsPointOnDisp(float x, float y);

// Returns ALL Z values at (x,y) (not just max). Useful for stacked disps
// (e.g., underground shell + ground surface). Returns count written.
native int   BSP_DispHeightAtMulti(float x, float y, float[] results, int maxResults);

// - Engine accessors (no fallback; check BSP_DispReady first) -

// True once engine globals have been resolved AND tree count > 0.
native bool  BSP_DispReady();

// Number of CDispCollTree entries (canonical engine count).
native int   BSP_DispCount();

// Engine AABB for disp tree[idx]. false if not ready or out of range.
native bool  BSP_DispGetBounds(int idx, float mins[3], float maxs[3]);

// Diagnostic: dump tree[idx]'s bbox + power + counts + raw bytes for verification.
native int   BSP_DispDebugInfo(int idx, char[] buf, int maxlen);

// Diagnostic: explain why BSP_DispHeightAt returned its result at (x,y).
native int   BSP_DispDiagnoseQuery(float x, float y, char[] buf, int maxlen);

// m_nPower from engine tree[idx]. -1 if unavailable.
native int   BSP_DispGetPower(int idx);

// m_nContents from engine tree[idx] (CONTENTS_* flags). 0 if unavailable.
native int   BSP_DispGetContents(int idx);

// m_nSurfaceProps[4] (4 shorts) from engine tree[idx]. false if unavailable.
native bool  BSP_DispGetSurfaceProps(int idx, int props[4]);

// Vertex/triangle counts from engine tree[idx]. -1 if unavailable.
native int   BSP_DispVertCount(int idx);
native int   BSP_DispTriCount(int idx);

// Read a single vertex position from engine tree's m_aVerts buffer.
// false if out of range or engine not ready.
native bool  BSP_DispGetVert(int idx, int vertIdx, float pos[3]);

// - Disk-only explicit access -
// Disk-parsed disp data, separate from engine reader.
// NOTE: disk indices do NOT correspond to engine indices.

native int   BSP_DispDiskCount();
native bool  BSP_DispDiskBounds(int idx, float mins[3], float maxs[3]);
// Diagnostic: dump disk-parsed disp[idx] (bbox + base quad corners + face/power).
native int   BSP_DispDiskDebugInfo(int idx, char[] buf, int maxlen);

stock bool   BSPPeek_Available()
{
    return LibraryExists("bsppeek");
}

public Extension __ext_bsppeek = {
    name     = "BSP-Peek",
    file     = "bsppeek.ext",
    autoload = 1,
    required = 0,
};
