# BSP-Peek - SourceMod extension for BSP queries

Extension to expose engine-internal BSP/Collision data and disk-parsed BSP file lumps to SourceMod plugins.

Engine-internal:

- `CCollisionBSPData` (brushes, brush sides, box brushes, leaves, planes, nodes, submodels, areas, area portals)
- visibility / PVS (clusters, leaf visibility)
- `CDispCollTree` (displacements)
- static props (runtime VPhysics collision)
- world collision tracing (`BSP_TraceHull` - brushes + displacements + props)
- surface physics props / friction (`IPhysicsSurfaceProps`)

Disk-parsed BSP file lumps:

- entities
- texinfo
- texdata (material names, reflectivity)
- planes
- leaffaces
- leaf water
- worldlights
- static props
- vis
- vertexes
- faces
- edges, surfedges
- cubemaps
- header

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
mkdir -p build && cd build
python ../configure.py --enable-optimize --targets=x86
ambuild
```

Required toolchain:

- **Windows**: Visual Studio Build Tools 2019+
- **Linux**: `gcc-multilib g++-multilib libstdc++6:i386`
- Both: Python 3.9+ and AMBuild 2.2+
