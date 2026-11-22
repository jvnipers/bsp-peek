#if defined _bsppeek_props_included
    #endinput
#endif
#define _bsppeek_props_included

// Runtime static props, world hull traces and surface properties.

// Runtime (post-combine) static props via IStaticPropMgr, the actual props the engine collides (the sprp-lump accessors above are pre-combine).
// Indexed by static-prop-manager index (same as BSP_StaticPropAtRay returns).
// All return 0 / false / -1 when unavailable or idx is out of range.
native int   BSP_RtStaticPropCount();
native bool  BSP_RtStaticPropBounds(int idx, float mins[3], float maxs[3]);    // world AABB
native bool  BSP_RtStaticPropOrigin(int idx, float origin[3]);
native bool  BSP_RtStaticPropAngles(int idx, float angles[3]);
native int   BSP_RtStaticPropSolid(int idx);         // SOLID_*
native int   BSP_RtStaticPropSolidFlags(int idx);    // FSOLID_* bitmask
native int   BSP_RtStaticPropModelName(int idx, char[] buf, int maxlen);
// Static-prop index hit by a ray from start to end (identifies the prop under an aim).
// -1 if the ray hits a brush / nothing.
native int   BSP_StaticPropAtRay(const float start[3], const float end[3]);

// Diagnostic:
// Runs the collision-mesh pipeline (GetCollisionModel -> GetVCollide -> CreateDebugMesh) for runtime prop `idx`,
// writes the model and vcollide pointers, SOLID_* type, solid count, first solid's vertex and triangle counts,
// collision scale and model name into buf.
// Confirms the mesh is reachable (esp. on prop-combine maps). Returns the full text's length, which can exceed maxlen.
native int   BSP_StaticPropProbe(int idx, char[] buf, int maxlen);

// Runtime prop collision mesh, world-space triangles, as the engine traces the prop:
// the first solid of its collision model at its collision scale (CreateDebugMesh, transformed to world);
// a SOLID_BBOX prop's unrotated box; none for a vphysics prop without one; else 12 OBB tris.
// Built + cached per prop on first access, works on prop-combine maps.
native int   BSP_StaticPropTriCount(int idx);
native bool  BSP_StaticPropTri(int idx, int triIdx, float v0[3], float v1[3], float v2[3]);
// Nearest collision triangle of a runtime static prop to pos, within maxDist (must be positive).
// Returns distance (-1.0 if none), sets propIdx (runtime index),
// the normal of v0, v1, v2 by their winding (may face into the prop), and the 3 world-space verts.
native float BSP_StaticPropNearestTri(const float pos[3], float maxDist, int &propIdx,
                                      float normal[3], float v0[3], float v1[3], float v2[3]);

// Sweep an AABB [mins,maxs] from start to end against static prop `idx` (at its origin/angles) via VPhysics,
// isolated to this prop by its model's vcollide.
// Fills fraction (1.0 = no hit), endpos, normal, startSolid.
// Returns: -1 = unavailable (interfaces unresolved or no collision model), 0 = no hit, 1 = hit.
// NOTE: `idx` is the sprp-lump index (as BSP_StaticPropOrigin), not the runtime one.
// By-name resolution, does NOT work for prop-combine (autocombine) models, and ignores the prop's scale.
// Prefer BSP_StaticPropHullSweep, which uses the runtime props.
native int   BSP_StaticPropTraceHull(int idx, const float start[3], const float end[3],
                                     const float mins[3], const float maxs[3],
                                     float &fraction, float endpos[3], float normal[3],
                                     bool &startSolid);

// Sweep an AABB [mins,maxs] from start to end against the runtime static props in the swept volume (IEngineTrace::ClipRayToCollideable).
// Works on prop-combine maps.
// Each prop is clipped independently, so the box can contact several, the contact whose surface (endpos.z + maxs.z) is nearest refZ wins.
// Fills fraction (1.0 = no hit), endpos, normal, startSolid. Returns: -1 = unavailable, 0 = no hit, 1 = hit.
native int   BSP_StaticPropHullSweep(const float start[3], const float end[3],
                                     const float mins[3], const float maxs[3], float refZ,
                                     float &fraction, float endpos[3], float normal[3],
                                     bool &startSolid);

// Writes the resolved interface pointers + vtable pointers + static-prop count into buf.
// Confirms the gamedata-resolved globals are real. Returns length.
native int   BSP_StaticPropDebug(char[] buf, int maxlen);

// Unified world hull trace.
// Sweeps an AABB [mins,maxs] against the WHOLE collision world via IEngineTrace::TraceRay (the player's collision path).
// World brushes, displacements, and static props always participate.
// Brush-model entities (func_*) participate only when hitEntities is true.
// Studio-model entities (players, NPCs, prop_*) are never hit.
//   mask         contents mask (e.g. BSPP_MASK_PLAYERCOLLIDE)
//   fraction     1.0 = no contact, else fraction of the sweep completed
//   endpos       hull center at the contact (or `end` if no hit)
//   normal       impact plane normal
//   startSolid   hull began inside solid
//   allSolid     entire sweep was inside solid (plane/normal not meaningful)
//   contents     CONTENTS_* on the far side of the surface hit
//   dispFlags    DISPSURF_FLAG_* (non-zero => hit a displacement surface)
//   planeDist    impact plane distance (normal.pos == planeDist on the surface)
//   surfaceProps surfaceprop index of the hit surface (feed IPhysicsSurfaceProps)
//   surfaceFlags SURF_* flags of the hit surface
//   hitType      0 = nothing, 1 = world/brush (incl. brush entity), 2 = displacement, 3 = static prop
//   surfaceName  impact material name ("**studio**" props, "**displacement**" disps)
// Returns: -1 = unavailable (engine trace not resolved), 0 = no hit, 1 = hit.
native int   BSP_TraceHull(const float start[3], const float end[3],
                           const float mins[3], const float maxs[3], int mask,
                           bool   hitEntities,
                           float &fraction, float endpos[3], float normal[3],
                           bool &startSolid,
                           bool &allSolid = false, int &contents = 0,
                           int &dispFlags = 0, float &planeDist = 0.0,
                           int &surfaceProps = 0, int &surfaceFlags = 0,
                           int &hitType = 0, char[] surfaceName = "", int nameLen = 0);

// Surface physics properties (friction / surfaceprop).
// Resolved from the live IPhysicsSurfaceProps singleton in loaded vphysics.
// `idx` is a PHYSICS surfaceprop-database index (as returned by BSP_TraceHull's surfaceProps, BSP_DispGetSurfaceProps).
// NOT a texdata index (BSP_BrushSideTexData / BSP_BoxBrushSurfaceIndex / BSP_TexDataMaterialName differ).

// True once the IPhysicsSurfaceProps interface resolved (vphysics mapped).
native bool  BSP_SurfacePropsReady();
// Number of entries in the surfaceprop database. 0 if unavailable.
native int   BSP_SurfacePropCount();
// Name of surfaceprop[idx] (e.g. "concrete", "ice", "metal"). Returns length.
native int   BSP_SurfacePropName(int idx, char[] buf, int maxlen);
// Database index for a surfaceprop name (e.g. "ice"). -1 if unavailable / unknown.
native int   BSP_SurfacePropIndex(const char[] name);
// Physics + movement params for surfaceprop[idx]. Returns false if unavailable / OOB.
//   friction       Coulomb friction coefficient (the headline movement value)
//   elasticity     collision restitution
//   density        kg/m^3
//   thickness      sheet-material thickness (inches, 0 = solid)
//   dampening      velocity dampening
//   maxSpeedFactor player max-speed multiplier on this surface (1.0 = normal)
//   jumpFactor     player jump-height multiplier on this surface (1.0 = normal)
//   material       char material code (surfacegameprops_t.material)
//   climbable      true if the surface is flagged as a ladder surface
native bool  BSP_SurfacePropData(int idx, float &friction, float &elasticity,
                                 float &density, float &thickness,
                                 float &dampening, float &maxSpeedFactor,
                                 float &jumpFactor, int &material,
                                 bool &climbable);
// Diagnostic: hex+float dump of the live surfacedata_t for surfaceprop[idx].
// friction is the +0x00 anchor, used to locate the runtime game.* offsets when the SDK struct layout drifts.
// Returns length.
native int   BSP_SurfacePropDump(int idx, char[] buf, int maxlen);
