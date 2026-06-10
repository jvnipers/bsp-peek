#include "bsp_props.h"
#include "bsp_lumps.h"

#include "engine/ivmodelinfo.h"
#include "gametrace.h"      // CGameTrace (trace_t) -> pulls cmodel.h/CBaseTrace
#include "mathlib/vector.h" // Vector, QAngle
#include "vcollide.h"
#include "vphysics_interface.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>

namespace BSPProps {

namespace {

IVModelInfo *g_modelinfo = nullptr;
IPhysicsCollision *g_physcollision = nullptr;

// model path -> vcollide_t* (engine-owned, not freed here).
// Props commonly share a model, so cache per name.
// A cached nullptr means "resolved, no collide".
std::unordered_map<std::string, vcollide_t *> g_vcollideCache;

vcollide_t *GetPropVCollide(int propIdx) {
  char name[256];
  if (BSPLumps::StaticPropModelName(propIdx, name, sizeof(name)) <= 0)
    return nullptr;
  auto it = g_vcollideCache.find(name);
  if (it != g_vcollideCache.end())
    return it->second;
  vcollide_t *vc = nullptr;
  const model_t *m = g_modelinfo->FindOrLoadModel(name);
  if (m)
    vc = g_modelinfo->GetVCollide(m);
  g_vcollideCache[name] = vc;
  return vc;
}

} // namespace

bool Init(IGameConfig *gc, char *err, size_t errLen) {
  g_modelinfo = nullptr;
  g_physcollision = nullptr;
  if (!gc) {
    std::snprintf(err, errLen, "no gameconf");
    return false;
  }
  // The "Addresses" entries resolve to the server-module global variables
  // (IVModelInfo *modelinfo; IPhysicsCollision *physcollision;),
  // so the resolved address IS the variable's location ->
  // deref once for the interface pointer.
  void *addr = nullptr;
  if (gc->GetAddress("modelinfo", &addr) && addr)
    g_modelinfo = *reinterpret_cast<IVModelInfo **>(addr);
  addr = nullptr;
  if (gc->GetAddress("physcollision", &addr) && addr)
    g_physcollision = *reinterpret_cast<IPhysicsCollision **>(addr);
  if (!g_modelinfo) {
    std::snprintf(err, errLen, "'modelinfo' address unavailable (gamedata)");
    return false;
  }
  if (!g_physcollision) {
    std::snprintf(err, errLen,
                  "'physcollision' address unavailable (gamedata)");
    return false;
  }
  return true;
}

bool Ready() { return g_modelinfo && g_physcollision; }

void OnMapClear() { g_vcollideCache.clear(); }

void Shutdown() {
  g_vcollideCache.clear();
  g_modelinfo = nullptr;
  g_physcollision = nullptr;
}

int TraceHull(int propIdx, const float start[3], const float end[3],
              const float mins[3], const float maxs[3], float &outFraction,
              float outEndPos[3], float outNormal[3], bool &outStartSolid) {
  outFraction = 1.0f;
  outStartSolid = false;
  outEndPos[0] = end[0];
  outEndPos[1] = end[1];
  outEndPos[2] = end[2];
  outNormal[0] = outNormal[1] = outNormal[2] = 0.0f;

  if (!Ready())
    return -1;
  vcollide_t *vc = GetPropVCollide(propIdx);
  if (!vc || vc->solidCount == 0 || !vc->solids)
    return -1;

  float po[3], pa[3];
  if (!BSPLumps::StaticPropOrigin(propIdx, po) ||
      !BSPLumps::StaticPropAngles(propIdx, pa))
    return -1;

  Vector vstart(start[0], start[1], start[2]);
  Vector vend(end[0], end[1], end[2]);
  Vector vmins(mins[0], mins[1], mins[2]);
  Vector vmaxs(maxs[0], maxs[1], maxs[2]);
  Vector vorg(po[0], po[1], po[2]);
  QAngle vang(pa[0], pa[1], pa[2]); // QAngle(pitch, yaw, roll)

  // Sweep against each convex solid, keep the earliest contact.
  // OR start-solid across solids (box began inside any piece).
  float bestFrac = 1.0f;
  bool haveHit = false;
  for (int i = 0; i < vc->solidCount; ++i) {
    CPhysCollide *collide = vc->solids[i];
    if (!collide)
      continue;
    CGameTrace tr;
    tr.fraction = 1.0f;
    tr.startsolid = false;
    tr.allsolid = false;
    g_physcollision->TraceBox(vstart, vend, vmins, vmaxs, collide, vorg, vang,
                              &tr);
    if (tr.startsolid)
      outStartSolid = true;
    if (tr.fraction < bestFrac) {
      bestFrac = tr.fraction;
      haveHit = true;
      outEndPos[0] = tr.endpos.x;
      outEndPos[1] = tr.endpos.y;
      outEndPos[2] = tr.endpos.z;
      outNormal[0] = tr.plane.normal.x;
      outNormal[1] = tr.plane.normal.y;
      outNormal[2] = tr.plane.normal.z;
    }
  }
  outFraction = bestFrac;
  return haveHit ? 1 : 0;
}

} // namespace BSPProps
