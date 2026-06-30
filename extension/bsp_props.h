#ifndef _INCLUDE_BSPPEEK_BSP_PROPS_H_
#define _INCLUDE_BSPPEEK_BSP_PROPS_H_

#include "smsdk_ext.h"
#include <cstddef>

// Static prop collision.
// TraceHull: resolves one prop's collision by model name (IVModelInfo -> vcollide_t) and sweeps an AABB against it via IPhysicsCollision.
// Exact, but fails on prop-combine model names.
// HullSweep: sweeps an AABB against the runtime props in a volume via IEngineTrace + IStaticPropMgr.
// Works on prop-combine maps.
// Interfaces are resolved from server-module globals via gamedata addresses.
namespace BSPProps
{

	// Resolve IVModelInfo + IPhysicsCollision from the server-module globals via gamedata addresses ("modelinfo", "physcollision").
	// Returns false (and fills err) if either address is unavailable.
	bool Init(IGameConfig *gc, char *err, size_t errLen);
	// Both interfaces resolved.
	bool Ready();
	// Fill buf with the resolved interface pointers + their first-deref vtable pointers (sanity check that the sigscanned globals are real).
	// Returns length.
	int Debug(char *buf, size_t buflen);
	// Drop the per-map model->vcollide cache (engine reloads collide on map change).
	void OnMapClear();
	void Shutdown();

	// Sweep an AABB [mins,maxs] from start to end against static prop `propIdx` (sprp index), at the prop's origin/angles.
	// Fills the trace outputs.
	//   return: -1 = unavailable (no interfaces / prop has no collide), 0 = no hit, 1 = hit (fraction < 1).
	// outStartSolid is set when the box began inside the prop's collision.
	// NOTE: by-name vcollide path. Does NOT work on prop-combine (autocombine) maps.
	int TraceHull(int propIdx, const float start[3], const float end[3], const float mins[3], const float maxs[3], float &outFraction,
				  float outEndPos[3], float outNormal[3], bool &outStartSolid);

	// Sweep an AABB [mins,maxs] from start to end against the runtime static props in the swept-hull AABB
	// (engine collideables, works on autocombine maps).
	// Each prop is clipped independently, so the box can contact several, the contact whose surface (endpos.z + maxs.z) is nearest refZ wins.
	//   return: -1 = unavailable (enginetrace/staticpropmgr missing), 0 = no prop hit, 1 = hit.
	int HullSweep(const float start[3], const float end[3], const float mins[3], const float maxs[3], float refZ, float &outFraction,
				  float outEndPos[3], float outNormal[3], bool &outStartSolid);

	// Unified world hull trace: sweep an AABB [mins,maxs] from start to end against the WHOLE collision world via IEngineTrace::TraceRay
	// world brushes, displacements, and static props always participate.
	// Brush-model entities (func_*) participate only when hitEntities is true (dynamic point entities are always excluded).
	// The result matches a real player-hull trace for the given contents `mask` (e.g. MASK_PLAYERSOLID).
	//
	//   outHitType: 0 = nothing, 1 = world/brush (incl. brush entity), 2 = displacement, 3 = static prop.
	//   outSurfName: impact surface material name ("**studio**" for props, "**displacement**" for disps). May be null to skip.
	//   return: -1 = unavailable (enginetrace not resolved), 0 = no hit, 1 = hit (or startsolid).
	int WorldTraceHull(const float start[3], const float end[3], const float mins[3], const float maxs[3], int mask, bool hitEntities,
					   float &outFraction, float outEndPos[3], float outNormal[3], bool &outStartSolid, bool &outAllSolid, int &outContents,
					   int &outDispFlags, float &outPlaneDist, int &outSurfaceProps, int &outSurfaceFlags, int &outHitType, char *outSurfName,
					   int surfNameLen);

	// Surface physics properties (friction / elasticity / movement modifiers).
	// Resolved from the live IPhysicsSurfaceProps singleton in the already-loaded vphysics module.
	// The index is a PHYSICS surfaceprop-database index, the kind returned by WorldTraceHull's surfaceProps,
	// BSP_DispGetSurfaceProps, and BSP_BoxBrushSurfaceIndex. NOT a texdata index.

	// True once the IPhysicsSurfaceProps interface resolved.
	bool SurfacePropsReady();
	// Number of entries in the surfaceprop database (0 if unavailable).
	int SurfacePropCount();
	// Name of surfaceprop[idx] (e.g. "concrete", "ice", "metal"). Returns length.
	int SurfacePropName(int idx, char *buf, int maxlen);
	// Database index for a surfaceprop name. -1 if unavailable / not found.
	int SurfacePropIndex(const char *name);
	// Full physics + movement params for surfaceprop[idx]. false if unavailable / OOB.
	//   friction/elasticity/density/thickness/dampening: surfacephysicsparams_t.
	//   maxSpeedFactor/jumpFactor: movement modifiers (surf ramps, jump pads).
	//   material: char material code, climbable: ladder-surface flag.
	bool SurfacePropData(int idx, float &friction, float &elasticity, float &density, float &thickness, float &dampening, float &maxSpeedFactor,
						 float &jumpFactor, int &material, bool &climbable);

	// Diagnostic: hex+float dump of the live surfacedata_t for surfaceprop[idx] (the struct GetSurfaceData returns). 
	// Used to RE the runtime field layout. (friction=+0 is the known anchor). Returns length written.
	int SurfacePropDump(int idx, char *buf, size_t buflen);

	// Runtime (post-combine) static props, via IStaticPropMgr + ICollideable.
	// These are the actual props the engine collides, indexed by the static-prop-manager index (same index PropAtRay returns).
	// All return 0/false/-1 when unavailable or idx is out of range.
	int RtCount();
	bool RtBounds(int idx, float outMins[3], float outMaxs[3]); // world AABB
	bool RtOrigin(int idx, float out[3]);
	bool RtAngles(int idx, float out[3]);
	int RtSolid(int idx);                            // SolidType_t
	int RtSolidFlags(int idx);                       // FSOLID_* bitmask
	int RtModelName(int idx, char *buf, int maxlen); // returns length
	// Static-prop index hit by a ray from start to end, or -1 if the ray hits a brush / nothing. Identifies the prop under an aim.
	int PropAtRay(const float start[3], const float end[3]);

	// Diagnostic:
	// Runs the collision-mesh pipeline (GetCollisionModel -> GetVCollide -> CreateDebugMesh) for runtime prop `idx`,
	// writes model/vcollide/solid/vert counts into buf.
	// Confirms the mesh is reachable (esp. on prop-combine maps). Returns length.
	int ProbeMesh(int idx, char *buf, size_t buflen);

	// Runtime prop collision mesh, world-space triangles (CreateDebugMesh transformed by the prop's collision->world matrix).
	// Props with no .phy fall back to 12 OBB tris.
	// Built + cached per prop on first access.
	int TriCount(int idx);
	bool Triangle(int idx, int triIdx, float v0[3], float v1[3], float v2[3]);
	// Nearest collision triangle to pos across the props within maxDist.
	// Returns the distance (-1 if none), sets outPropIdx + the tri's plane normal + 3 world verts.
	float NearestTri(const float pos[3], float maxDist, int &outPropIdx, float outNormal[3], float v0[3], float v1[3], float v2[3]);

} // namespace BSPProps

#endif
