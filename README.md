# BSP-Peek - SourceMod extension for BSP leaf/brush queries

Exposes engine-internal `CCollisionBSPData` (brushes, leaves, planes, nodes) to SourceMod plugins.

Currently only support CS:GO

## Example usecase (What I built this for)

Filter pixelsurf candidates by checking:

- bottom brush has SOLID contents
- bottom brush listed BEFORE upper brush in their shared BSP leaf
  (the engine visits leaf brushes in BSP order; first-visited wins collision)

## Natives

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
