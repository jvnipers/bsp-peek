# BSP-Peek - SourceMod extension for BSP leaf/brush queries

Exposes engine-internal `CCollisionBSPData` (brushes, leaves, planes, nodes) to SourceMod plugins.

Currently only support CS:GO

## Example usecase (What I built this for)

Filter pixelsurf candidates by checking:

- bottom brush has SOLID contents
- bottom brush listed BEFORE upper brush in their shared BSP leaf
  (the engine visits leaf brushes in BSP order; first-visited wins collision)

## Natives

### Brush / leaf / cache

```sp
native int  BSP_LeafAtPoint(const float pos[3]);
native int  BSP_LeafBrushes(int leaf, int[] buf, int max);   // ordered (BSP order)
native bool BSP_IsBoxBrush(int brushIdx);
native int  BSP_BrushContents(int brushIdx);
native void BSP_BrushBounds(int brushIdx, float mins[3], float maxs[3]);
native int  BSP_NumBrushes();
native int  BSP_NumLeaves();
native bool BSP_FindBrushPairAtSeam(const float samplePos[3], float seamZ,
                                    int &outLowerBrush, int &outUpperBrush);
native int  BSP_RebuildCache();         // synchronous; blocks until done
native int  BSP_RebuildCacheAsync();    // async worker thread; reads block until swap-in
native bool BSP_CacheIsBuilding();      // true between RebuildCacheAsync and swap-in
```

`LeafAtPoint` is a O(log N) BSP node-walk (descends `map_nodes` via signed plane distance, leaf = `-1 - child` per Source convention)

All other natives read directly from the live `CCollisionBSPData` global; the brush table is additionally AABB-cached per-map to avoid repeated plane-pointer walks during seam queries.

### Displacements

Two backends:

- **Engine** (primary): reads `CDispCollTree` array directly from engine process memory via gamedata sigscan. Gives canonical post-stitching verts - matches what `TR_TraceRay` would hit.
- **Disk** (fallback): parses BSP file lumps (`DISPINFO` / `DISP_VERTS` / `FACES`) at map load and builds triangle meshes from scratch. Used when the engine reader can't initialize (missing/wrong gamedata sigs).

Unified queries (`BSP_DispHeightAt` etc.) try engine first, fall back to disk automatically. Engine-only accessors return safe defaults when the reader is unavailable; check `BSP_DispReady()` first if you need certainty.

```sp
#define BSP_DISP_NO_HIT -1.0e30    // sentinel for "no XY match"

// Unified (engine-first, disk fallback)
native float BSP_DispHeightAt(float x, float y);
native float BSP_DispHeightAtDebug(float x, float y, int &outIdx);
native float BSP_DispSurfaceNormalAt(float x, float y, float normal[3]);  // Z + tri plane normal
native bool  BSP_DispIsPointOnDisp(float x, float y);
native int   BSP_DispHeightAtMulti(float x, float y, float[] results, int maxResults);

// Engine accessors (require BSP_DispReady)
native bool  BSP_DispReady();
native int   BSP_DispCount();
native bool  BSP_DispGetBounds(int idx, float mins[3], float maxs[3]);
native int   BSP_DispGetPower(int idx);                   // mesh resolution (2-4)
native int   BSP_DispGetContents(int idx);                // CONTENTS_* flags
native bool  BSP_DispGetSurfaceProps(int idx, int props[4]);
native int   BSP_DispVertCount(int idx);
native int   BSP_DispTriCount(int idx);
native bool  BSP_DispGetVert(int idx, int vertIdx, float pos[3]);
native int   BSP_DispDebugInfo(int idx, char[] buf, int maxlen);
native int   BSP_DispDiagnoseQuery(float x, float y, char[] buf, int maxlen);

// Disk-only (explicit fallback access; indices != engine indices)
native int   BSP_DispDiskCount();
native bool  BSP_DispDiskBounds(int idx, float mins[3], float maxs[3]);
native int   BSP_DispDiskDebugInfo(int idx, char[] buf, int maxlen);
```

Engine reader resolves three globals from gamedata sigscan: `g_DispCollTreeCount`, `g_pDispCollTrees`, `g_pDispBounds` (anchored on `CMod_LoadDispInfo`). Field offsets within `CDispCollTree` (mins/maxs/power, `m_aVerts` / `m_aTris` CUtlVector members) and `CDispCollTri` (3 byte indices + edge flags + plane).

For `HeightAt` queries, AABB-rejects the disp first, then iterates triangles testing XY containment + barycentric Z interpolation. Picks the highest matching Z across all tris (matches a downward trace).

## Build

Produces `build/extension/bsppeek.ext/{platform}-x86/.` containing the ext binary regardless of if Docker or native.

### Docker

```pwsh
docker compose run --rm build
```

### Native

```pwsh
mkdir build; cd build
python ../configure.py --enable-optimize --targets=x86
ambuild
```

Required toolchain:

- **Windows**: Visual Studio Build Tools 2022
- **Linux**: `gcc-multilib g++-multilib libstdc++6:i386`
- Both: Python 3 + AMBuild 2+

## Notes on engine layout

- CSGO `CCollisionBSPData` wraps each pointer/count pair in `CRangeValidatedArray<T>` (= `{T* ptr; int count}`, 8 bytes each).
  Both the standalone `int numX` AND the array's internal count are kept in sync.

- `cbrush_t` is 8 bytes: `int contents` + `ushort numsides` + `ushort firstbrushside`.
  CSGO additionally has a parallel `map_boxbrushes` table for SIMD-optimized box brushes - our `IsBoxBrush` heuristic (numsides==6 + axis-aligned planes) may false-negative because of this.
  The consumer plugin treats `IsBoxBrush` as informational rather than a hard filter.

- `cnode_t` is 12 bytes: `cplane_t* plane` + `int children[2]`.
  Negative child encodes a leaf index via `leafIdx = -1 - child`.
