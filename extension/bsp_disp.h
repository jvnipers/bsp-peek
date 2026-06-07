#ifndef _INCLUDE_BSPPEEK_BSP_DISP_H_
#define _INCLUDE_BSPPEEK_BSP_DISP_H_

#include "smsdk_ext.h"
#include <cstddef>

// Two backends:
//   Engine - reads CDispCollTree array from engine memory via gamedata sigscan.
//            Gives canonical post-stitching verts. Primary path when available.
//   Disk   - parses BSP file lumps (DISPINFO, DISP_VERTS, FACES, etc.) and
//            builds triangle meshes from scratch.
//            Fallback when engine not ready.

namespace BSPDisp {

bool InitEngine(IGameConfig *gc, char *err, size_t errLen);
bool LoadFromMap(const char *bspPath, char *err, size_t errLen);
bool EnsureLoaded(const char *mapname, const char *bspPath, char *errOut,
                  size_t errLen);
void Clear();
void ShutdownEngine();

// Unified queries (engine-first, disk fallback)
float HeightAt(float x, float y);
float HeightAtDebug(float x, float y, int &outIdx);
float SurfaceNormalAt(float x, float y, float normal[3]);
bool IsPointOnDisp(float x, float y);
int HeightAtMulti(float x, float y, float *results, int maxResults);
// 3D nearest-disp-surface distance (works on near-vertical disp walls).
float DistToSurface(const float pos[3], float maxDist);
// Nearest disp triangle: distance + the tri's normal and 3 world-space verts.
float DistNearestTri(const float pos[3], float maxDist, float normal[3],
                     float v0[3], float v1[3], float v2[3]);
// Nearest disp tree/face index (distinct per displacement). -1 if none in range.
int TreeIndexAt(const float pos[3], float maxDist);

bool EngineReady();
int EngineCount();
float EngineHeightAt(float x, float y);
float EngineHeightAtDebug(float x, float y, int &outIdx);
float EngineDistToSurface(const float pos[3], float maxDist);
float EngineNearestTri(const float pos[3], float maxDist, float outNormal[3],
                       float outV0[3], float outV1[3], float outV2[3]);
int EngineTreeIndexAt(const float pos[3], float maxDist);
int EngineDebugTreeInfo(int idx, char *buf, size_t bufLen);
int EngineDiagnoseQuery(float x, float y, char *buf, size_t bufLen);
bool EngineGetBounds(int idx, float mins[3], float maxs[3]);
int EngineGetPower(int idx);
int EngineGetContents(int idx);
bool EngineGetSurfaceProps(int idx, int props[4]);
int EngineVertCount(int idx);
int EngineTriCount(int idx);
bool EngineGetVert(int idx, int vertIdx, float pos[3]);

int DiskCount();
bool DiskGetBounds(int idx, float mins[3], float maxs[3]);
float DiskHeightAt(float x, float y);
float DiskHeightAtDebug(float x, float y, int &outDispIdx);
int DiskDebugDispInfo(int idx, char *buf, size_t bufLen);

} // namespace BSPDisp

#endif
