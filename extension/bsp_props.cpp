#include "bsp_props.h"
#include "bsp_lumps.h"

#include "const.h" // MASK_PLAYERSOLID
#include "engine/IEngineTrace.h"
#include "engine/IStaticPropMgr.h"
#include "engine/ivmodelinfo.h"
#include "gametrace.h" // CGameTrace (trace_t) -> pulls cmodel.h/CBaseTrace (Ray_t)
#include "mathlib/vector.h" // Vector, QAngle
#include "tier1/utlvector.h"
#include "vcollide.h"
#include "vphysics_interface.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>

namespace BSPProps {

namespace {

IVModelInfo *g_modelinfo = nullptr;
IPhysicsCollision *g_physcollision = nullptr;
IEngineTrace *g_enginetrace = nullptr;
IStaticPropMgrServer *g_staticpropmgr = nullptr;

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
  // FindOrLoadModel returns null or (model_t*)-1 when the model can't be loaded
  // by name, e.g. prop-combine placeholders ("models/props/autocombine/...").
  // Calling GetVCollide on that sentinel crashes.
  if (m == nullptr || m == reinterpret_cast<const model_t *>(~uintptr_t(0))) {
    g_vcollideCache[name] = nullptr;
    return nullptr;
  }
  vc = g_modelinfo->GetVCollide(m);
  // Sanity: solidCount is a 15-bit field, reject absurd values / null array so
  // a bogus vcollide can't drive the trace loop off into garbage.
  if (vc && (vc->solidCount == 0 || vc->solidCount > 64 || !vc->solids))
    vc = nullptr;
  g_vcollideCache[name] = vc;
  return vc;
}

} // namespace

// Resolve a server-module global interface pointer via gamedata.
// The address IS the variable's location, so deref once.
template <typename T>
static T *ResolveGlobal(IGameConfig *gc, const char *key) {
  void *addr = nullptr;
  if (gc->GetAddress(key, &addr) && addr)
    return *reinterpret_cast<T **>(addr);
  return nullptr;
}

bool Init(IGameConfig *gc, char *err, size_t errLen) {
  g_modelinfo = nullptr;
  g_physcollision = nullptr;
  g_enginetrace = nullptr;
  g_staticpropmgr = nullptr;
  if (!gc) {
    std::snprintf(err, errLen, "no gameconf");
    return false;
  }
  g_modelinfo = ResolveGlobal<IVModelInfo>(gc, "modelinfo");
  g_physcollision = ResolveGlobal<IPhysicsCollision>(gc, "physcollision");
  g_enginetrace = ResolveGlobal<IEngineTrace>(gc, "enginetrace");
  g_staticpropmgr = ResolveGlobal<IStaticPropMgrServer>(gc, "staticpropmgr");

  // The runtime prop-hull path (enginetrace + staticpropmgr) is what handles
  // autocombine maps.
  // The vcollide path (modelinfo + physcollision) is the by-name fallback.
  //  Succeed if either pair resolved.
  bool sweepReady = g_enginetrace && g_staticpropmgr;
  bool vcollReady = g_modelinfo && g_physcollision;
  if (!sweepReady && !vcollReady) {
    std::snprintf(err, errLen,
                  "no interfaces resolved (modelinfo=%d physcollision=%d "
                  "enginetrace=%d staticpropmgr=%d)",
                  g_modelinfo != nullptr, g_physcollision != nullptr,
                  g_enginetrace != nullptr, g_staticpropmgr != nullptr);
    return false;
  }
  return true;
}

bool Ready() { return g_modelinfo && g_physcollision; }
bool SweepReady() { return g_enginetrace && g_staticpropmgr; }

int Debug(char *buf, size_t buflen) {
  if (!buf || buflen == 0)
    return 0;
  // One deref each: a valid interface ptr -> vtable ptr.
  // Garbage here = bad sig.
  uintptr_t mi = reinterpret_cast<uintptr_t>(g_modelinfo);
  uintptr_t pc = reinterpret_cast<uintptr_t>(g_physcollision);
  uintptr_t et = reinterpret_cast<uintptr_t>(g_enginetrace);
  uintptr_t sp = reinterpret_cast<uintptr_t>(g_staticpropmgr);
  uintptr_t etVt = et ? *reinterpret_cast<uintptr_t *>(et) : 0;
  uintptr_t spVt = sp ? *reinterpret_cast<uintptr_t *>(sp) : 0;
  return std::snprintf(
      buf, buflen,
      "modelinfo=0x%zx physcollision=0x%zx | enginetrace=0x%zx vtbl=0x%zx | "
      "staticpropmgr=0x%zx vtbl=0x%zx | props=%d",
      (size_t)mi, (size_t)pc, (size_t)et, (size_t)etVt, (size_t)sp,
      (size_t)spVt, BSPLumps::StaticPropCount());
}

void OnMapClear() { g_vcollideCache.clear(); }

void Shutdown() {
  g_vcollideCache.clear();
  g_modelinfo = nullptr;
  g_physcollision = nullptr;
  g_enginetrace = nullptr;
  g_staticpropmgr = nullptr;
}

int HullSweep(const float start[3], const float end[3], const float mins[3],
              const float maxs[3], float refZ, float &outFraction,
              float outEndPos[3], float outNormal[3], bool &outStartSolid) {
  outFraction = 1.0f;
  outStartSolid = false;
  outEndPos[0] = end[0];
  outEndPos[1] = end[1];
  outEndPos[2] = end[2];
  outNormal[0] = outNormal[1] = outNormal[2] = 0.0f;
  if (!SweepReady())
    return -1;

  Vector vstart(start[0], start[1], start[2]);
  Vector vend(end[0], end[1], end[2]);
  Vector vmins(mins[0], mins[1], mins[2]);
  Vector vmaxs(maxs[0], maxs[1], maxs[2]);

  // Swept-hull AABB: every static prop the moving box could touch.
  Vector boxMin, boxMax;
  for (int i = 0; i < 3; ++i) {
    float lo = (start[i] < end[i] ? start[i] : end[i]) + mins[i];
    float hi = (start[i] > end[i] ? start[i] : end[i]) + maxs[i];
    boxMin[i] = lo;
    boxMax[i] = hi;
  }

  CUtlVector<ICollideable *> props;
  g_staticpropmgr->GetAllStaticPropsInAABB(boxMin, boxMax, &props);
  if (props.Count() == 0)
    return 0;

  Ray_t ray;
  ray.Init(vstart, vend, vmins, vmaxs);

  // Each prop is clipped independently, so the swept box can contact several
  // (stacked / adjacent props). Pick the contact whose surface
  // (endpos.z + maxs.z) is NEAREST refZ, rather than the lowest.
  float bestDelta = 1.0e30f;
  bool haveHit = false;
  for (int i = 0; i < props.Count(); ++i) {
    ICollideable *c = props[i];
    if (!c)
      continue;
    CGameTrace tr;
    tr.fraction = 1.0f;
    tr.startsolid = false;
    tr.allsolid = false;
    g_enginetrace->ClipRayToCollideable(ray, MASK_PLAYERSOLID, c, &tr);
    if (tr.fraction >= 1.0f && !tr.startsolid)
      continue; // this prop wasn't touched
    float surfaceZ = tr.endpos.z + maxs[2];
    float delta = surfaceZ - refZ;
    if (delta < 0.0f)
      delta = -delta;
    if (delta < bestDelta) {
      bestDelta = delta;
      haveHit = true;
      outFraction = tr.fraction;
      outStartSolid = tr.startsolid;
      outEndPos[0] = tr.endpos.x;
      outEndPos[1] = tr.endpos.y;
      outEndPos[2] = tr.endpos.z;
      outNormal[0] = tr.plane.normal.x;
      outNormal[1] = tr.plane.normal.y;
      outNormal[2] = tr.plane.normal.z;
    }
  }
  return haveHit ? 1 : 0;
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
