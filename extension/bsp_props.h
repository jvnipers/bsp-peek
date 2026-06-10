#ifndef _INCLUDE_BSPPEEK_BSP_PROPS_H_
#define _INCLUDE_BSPPEEK_BSP_PROPS_H_

#include "smsdk_ext.h"
#include <cstddef>

// Static prop collision.
// TraceHull: resolves one prop's collision by model name (IVModelInfo -> vcollide_t)
//   and sweeps an AABB against it via IPhysicsCollision.
//   Exact, but fails on prop-combine model names.
// HullSweep: sweeps an AABB against the runtime props in a volume via IEngineTrace +
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
// NOTE: by-name vcollide path; does NOT work on prop-combine (autocombine) maps.
int TraceHull(int propIdx, const float start[3], const float end[3],
              const float mins[3], const float maxs[3], float &outFraction,
              float outEndPos[3], float outNormal[3], bool &outStartSolid);

// Sweep an AABB [mins,maxs] from start to end against the runtime static props in
// the swept-hull AABB (engine collideables, works on autocombine maps).
// Each prop is clipped independently, so the box can contact several,
// the contact whose surface (endpos.z + maxs.z) is nearest refZ wins.
//   return: -1 = unavailable (enginetrace/staticpropmgr missing),
//            0 = no prop hit, 1 = hit.
int HullSweep(const float start[3], const float end[3], const float mins[3],
              const float maxs[3], float refZ, float &outFraction,
              float outEndPos[3], float outNormal[3], bool &outStartSolid);

} // namespace BSPProps

#endif
