#ifndef _INCLUDE_BSPPEEK_BSP_PROPS_H_
#define _INCLUDE_BSPPEEK_BSP_PROPS_H_

#include "smsdk_ext.h"
#include <cstddef>

// Static prop collision via VPhysics.
// Uses IVModelInfo (engine) to resolve a prop's model -> vcollide_t, and
// IPhysicsCollision to sweep the player AABB against the prop's CPhysCollide.
// This is the exact engine collision, isolated to a single prop,
// so unrelated world geometry never start-solids the trace.
// Prop identity / origin / angles / model come from the parsed sprp lump.
namespace BSPProps {

// Resolve IVModelInfo + IPhysicsCollision from the server-module globals via
// gamedata addresses ("modelinfo", "physcollision").
// Returns false (and fills err) if either address is unavailable.
bool Init(IGameConfig *gc, char *err, size_t errLen);
// Both interfaces resolved.
bool Ready();
// Drop the per-map model->vcollide cache
// (engine reloads collide on map change).
void OnMapClear();
void Shutdown();

// Sweep the player AABB [mins,maxs] from start to end against static prop
// `propIdx` (sprp index), at the prop's origin/angles. Fills the trace outputs.
//   return: -1 = unavailable (no interfaces / prop has no collide),
//            0 = resolved, no hit, 1 = hit (fraction < 1).
// outStartSolid is set when the box began inside the prop's collision.
int TraceHull(int propIdx, const float start[3], const float end[3],
              const float mins[3], const float maxs[3], float &outFraction,
              float outEndPos[3], float outNormal[3], bool &outStartSolid);

} // namespace BSPProps

#endif
