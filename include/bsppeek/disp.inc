#if defined _bsppeek_disp_included
    #endinput
#endif
#define _bsppeek_disp_included

// Displacement surfaces: engine collision trees, BSP file fallback.

// Displacements
// Primary path: reads the CDispCollTree array from engine memory (canonical post-stitching verts).
// Fallback: parses BSP lumps (DISPINFO/DISP_VERTS/FACES) at map load.
// Unified queries use the engine reader when BSP_DispReady(), else fall back to disk automatically

#define BSP_DISP_NO_HIT -1.0e30    // sentinel returned by BSP_DispHeightAt for no XY match

// - Unified queries (engine-first, disk fallback) -

// Highest world-space Z at (x, y) across all displacements that contain the XY query point.
// Returns BSP_DISP_NO_HIT (-1e30) if none.
native float BSP_DispHeightAt(float x, float y);

// Same as BSP_DispHeightAt + returns the matching disp/tree index (-1 if none).
native float BSP_DispHeightAtDebug(float x, float y, int &outIdx);

// Returns surface Z + populates normal[3] with the tri's plane normal at (x,y).
// The normal points out of the surface (down on a ceiling displacement).
// Returns BSP_DISP_NO_HIT if no match.
native float BSP_DispSurfaceNormalAt(float x, float y, float normal[3]);

// True if any disp tri actually contains (x,y).
native bool  BSP_DispIsPointOnDisp(float x, float y);

// Returns ALL Z values at (x,y) (not just max). Useful for stacked disps (e.g., underground shell + ground surface).
// Returns count written.
native int   BSP_DispHeightAtMulti(float x, float y, float[] results, int maxResults);

// Min 3D distance from pos to any displacement collision surface, searching up to maxDist units.
// Returns BSP_DISP_NO_HIT if no disp data / nothing in range. Works on near-vertical disp WALLS.
native float BSP_DispDistToSurface(const float pos[3], float maxDist = 64.0);

// Nearest disp triangle to pos within maxDist.
// Returns the distance (BSP_DISP_NO_HIT if none) and fills the tri's plane normal + 3 world-space verts.
// Reveals the true collision-mesh orientation at a seam (the hull-contact normal is coarse).
native float BSP_DispNearestTri(const float pos[3], float maxDist,
                                float normal[3], float v0[3], float v1[3], float v2[3]);

// Nearest disp tree/face index (per displacement) within maxDist. -1 if none.
native int   BSP_DispTreeIndexAt(const float pos[3], float maxDist);

// - Engine accessors (no fallback, check BSP_DispReady first) -

// True once engine globals have been resolved AND tree count > 0.
native bool  BSP_DispReady();

// Number of CDispCollTree entries (canonical engine count).
native int   BSP_DispCount();

// Engine AABB for disp tree[idx]. false if not ready or out of range.
native bool  BSP_DispGetBounds(int idx, float mins[3], float maxs[3]);

// m_nPower from engine tree[idx]. -1 if unavailable.
native int   BSP_DispGetPower(int idx);

// m_nContents from engine tree[idx] (CONTENTS_* flags). 0 if unavailable.
native int   BSP_DispGetContents(int idx);

// Physics surfaceprop indices of engine tree[idx]: $surfaceprop..$surfaceprop4. false if unavailable.
// alphaProp = $surfacepropalpha: garbage unless the material sets it.
native bool  BSP_DispGetSurfaceProps(int idx, int props[4], int &alphaProp = 0);

// Vertex/triangle counts from engine tree[idx]. -1 if unavailable.
native int   BSP_DispVertCount(int idx);
native int   BSP_DispTriCount(int idx);

// Read a single vertex position from engine tree's m_aVerts buffer.
// false if out of range or engine not ready.
native bool  BSP_DispGetVert(int idx, int vertIdx, float pos[3]);

// Diagnostic: dump tree[idx]'s bbox + power + counts + raw bytes for verification.
native int   BSP_DispDebugInfo(int idx, char[] buf, int maxlen);

// Diagnostic: explain why BSP_DispHeightAt returned its result at (x,y).
native int   BSP_DispDiagnoseQuery(float x, float y, char[] buf, int maxlen);

// - Disk-only explicit access -
// Disk-parsed disp data, separate from engine reader.
// Disk index == engine tree index. Unparsed entries keep their slot (BSP_DispDiskBounds returns false).

native int   BSP_DispDiskCount();
native bool  BSP_DispDiskBounds(int idx, float mins[3], float maxs[3]);
// Diagnostic: dump disk-parsed disp[idx] (bbox + base quad corners + face/power).
native int   BSP_DispDiskDebugInfo(int idx, char[] buf, int maxlen);
