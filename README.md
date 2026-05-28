# BSP-Peek - SourceMod extension for BSP queries

Exposes engine-internal `CCollisionBSPData` (brushes, leaves, planes, nodes, submodels) and `CDispCollTree` (displacements) + disk-parsed BSP file lumps (entities, texinfo, leaffaces, worldlights, header) to SourceMod plugins.

Currently only supports CS:GO (x86)

MM:S 1.12+ SM 1.12+

For **Natives** see: [bsppeek.inc](/include/bsppeek.inc)

## Build

Produces `build/extension/bsppeek.ext/{platform}-x86/.` containing the ext binary regardless of if Docker or native.

### Docker

```bash
docker compose run --rm build
```

### Native

```bash
mkdir build && cd build
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
