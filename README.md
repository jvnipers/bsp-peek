# BSP-Peek - SourceMod extension for BSP queries

Exposes engine-internal `CCollisionBSPData` (brushes, leaves, planes, nodes, submodels), `CDispCollTree` (displacements) and static props (sprp lump + runtime VPhysics collision) + disk-parsed BSP file lumps (entities, texinfo, leaffaces, worldlights, header) to SourceMod plugins.

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
