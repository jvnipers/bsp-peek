#ifndef _INCLUDE_BSPPEEK_BSP_PROPS_H_
#define _INCLUDE_BSPPEEK_BSP_PROPS_H_

#include "smsdk_ext.h"
#include <cstddef>

// Static prop collision.
// TraceHull: resolves one prop's collision by model name (IVModelInfo ->
// vcollide_t)
//   and sweeps an AABB against it via IPhysicsCollision.
//   Exact, but fails on prop-combine model names.
// HullSweep: sweeps an AABB against the runtime props in a volume via
// IEngineTrace +
//   IStaticPropMgr, works on prop-combine maps.
// Interfaces are resolved from server-module globals via gamedata addresses.
namespace BSPProps {

// Resolve IVModelInfo + IPhysicsCollision from the server-module globals via
// gamedata addresses ("modelinfo", "physcollision").
// Returns false (and fills err) if either address is unavailable.
bool Init(IGameConfig *gc, char *err, size_t errLen);
// Both interfaces resolved.
bool Ready();
// Fill buf with the resolved interface pointers + their first-deref vtable
// pointers (sanity check that the sigscanned globals are real). Returns length.
int Debug(char *buf, size_t buflen);
// Drop the per-map model->vcollide cache
// (engine reloads collide on map change).
void OnMapClear();
void Shutdown();

// Sweep an AABB [mins,maxs] from start to end against static prop `propIdx`
// (sprp index), at the prop's origin/angles. Fills the trace outputs.
//   return: -1 = unavailable (no interfaces / prop has no collide),
//            0 = no hit, 1 = hit (fraction < 1).
// outStartSolid is set when the box began inside the prop's collision.
// NOTE: by-name vcollide path; does NOT work on prop-combine (autocombine)
// maps.
int TraceHull(int propIdx, const float start[3], const float end[3],
              const float mins[3], const float maxs[3], float &outFraction,
              float outEndPos[3], float outNormal[3], bool &outStartSolid);

// Sweep an AABB [mins,maxs] from start to end against the runtime static props
// in the swept-hull AABB (engine collideables, works on autocombine maps). Each
// prop is clipped independently, so the box can contact several, the contact
// whose surface (endpos.z + maxs.z) is nearest refZ wins.
//   return: -1 = unavailable (enginetrace/staticpropmgr missing),
//            0 = no prop hit, 1 = hit.
int HullSweep(const float start[3], const float end[3], const float mins[3],
              const float maxs[3], float refZ, float &outFraction,
              float outEndPos[3], float outNormal[3], bool &outStartSolid);

// Runtime (post-combine) static props, via IStaticPropMgr + ICollideable.
// These are the actual props the engine collides,
// indexed by the static-prop-manager index (same index PropAtRay returns).
// All return 0/false/-1 when unavailable or idx is out of range.
int RtCount();
bool RtBounds(int idx, float outMins[3], float outMaxs[3]); // world AABB
bool RtOrigin(int idx, float out[3]);
bool RtAngles(int idx, float out[3]);
int RtSolid(int idx);                            // SolidType_t
int RtSolidFlags(int idx);                       // FSOLID_* bitmask
int RtModelName(int idx, char *buf, int maxlen); // returns length
// Static-prop index hit by a ray from start to end, or -1 if the ray hits a
// brush / nothing. Identifies the prop under an aim.
int PropAtRay(const float start[3], const float end[3]);

// Diagnostic:
// Runs the collision-mesh pipeline (GetCollisionModel ->
// GetVCollide -> CreateDebugMesh) for runtime prop `idx`,
// writes model/vcollide/solid/vert counts into buf.
// Confirms the mesh is reachable (esp. on prop-combine maps). Returns length.
int ProbeMesh(int idx, char *buf, size_t buflen);

// Runtime prop collision mesh, world-space triangles
// (CreateDebugMesh transformed by the prop's collision->world matrix).
// Props with no .phy fall back to 12 OBB tris.
// Built + cached per prop on first access.
int TriCount(int idx);
bool Triangle(int idx, int triIdx, float v0[3], float v1[3], float v2[3]);
// Nearest collision triangle to pos across the props within maxDist.
// Returns the distance (-1 if none),
// sets outPropIdx + the tri's plane normal + 3 world verts.
float NearestTri(const float pos[3], float maxDist, int &outPropIdx,
                 float outNormal[3], float v0[3], float v1[3], float v2[3]);

} // namespace BSPProps

#endif
