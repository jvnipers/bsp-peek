#if defined _bsppeek_included
    #endinput
#endif
#define _bsppeek_included

// BSP-Peek - "peek" into engine CCollisionBSPData
// (leaf membership, ordered brush lists, brush type, AABB, BSP node-walk).
// Plus displacement surface queries via engine memory reader (primary) with BSP file-parse fallback when engine reader unavailable.
// Natives are optional. Calling one while the ext isn't loaded throws, so check BSPPeek_Available() first.

// Source-engine CONTENTS_* constants commonly checked:
#define BSPP_CONTENTS_SOLID        0x1
#define BSPP_CONTENTS_WINDOW       0x2
#define BSPP_CONTENTS_GRATE        0x8
#define BSPP_CONTENTS_WATER        0x20
#define BSPP_CONTENTS_TEAM1        0x800
#define BSPP_CONTENTS_TEAM2        0x1000
#define BSPP_CONTENTS_MOVEABLE     0x4000
#define BSPP_CONTENTS_PLAYERCLIP   0x10000
#define BSPP_CONTENTS_MONSTERCLIP  0x20000
#define BSPP_CONTENTS_MONSTER      0x2000000
#define BSPP_CONTENTS_LADDER       0x20000000
// Player-collidable mask (= engine MASK_PLAYERSOLID).
#define BSPP_MASK_PLAYERCOLLIDE    (BSPP_CONTENTS_SOLID | BSPP_CONTENTS_WINDOW | BSPP_CONTENTS_GRATE | BSPP_CONTENTS_MOVEABLE | BSPP_CONTENTS_PLAYERCLIP | BSPP_CONTENTS_MONSTER)
// Per-team variants: the player mask can also carry TEAM1 (Terrorists) or TEAM2 (CTs).
#define BSPP_MASK_PLAYERCOLLIDE_T  (BSPP_MASK_PLAYERCOLLIDE | BSPP_CONTENTS_TEAM1)
#define BSPP_MASK_PLAYERCOLLIDE_CT (BSPP_MASK_PLAYERCOLLIDE | BSPP_CONTENTS_TEAM2)

// Misc
// Engine's internal map path string. Returns string length.
native int  BSP_MapPathName(char[] buf, int maxlen);

// Health check: bitmask of subsystems that resolved + sanity-checked OK.
// Distinguishes "map has no such data" from "a signature broke after an update".
//   0x01 BSPData base + brush table sane    0x10 displacement engine reader
//   0x02 leaf + node tables present         0x20 static-prop interfaces
//   0x04 cboxbrush_t SIMD table present     0x40 disk BSP lumps loaded
//   0x08 visibility blob present
// Returns 0 when g_BSPData is unresolved or pointing at garbage.
native int  BSP_SelfTest();

// Engine build / patch version from the game dir's steam.inf.
// Returns ServerVersion (0 if unreadable), writes "ServerVersion=N PatchVersion=X ProductName=Y" into buf.
// steam.inf bumps every update, so diff it against known-good build (with BSP_SelfTest) when a signature breaks.
native int  BSP_EngineBuild(char[] buf, int maxlen);

// Debug: hex-dump CCollisionBSPData bytes from [startOff, endOff) to SourceMod log.
// Used to RE field offsets. Range clamped server-side.
native void BSP_DebugDumpCBSP(int startOff, int endOff);
// Debug: read pointer at CCollisionBSPData+ptrOff, dump `bytes` bytes from there.
// UNSAFE: bad ptrOff may crash. Use only with known-pointer offsets.
native void BSP_DebugDumpCBSPPtr(int ptrOff, int bytes);

#include <bsppeek/data>
#include <bsppeek/seam>
#include <bsppeek/disp>
#include <bsppeek/lumps>
#include <bsppeek/props>

stock bool BSPPeek_Available()
{
    return LibraryExists("bsppeek");
}

public Extension __ext_bsppeek = {
    name     = "BSP-Peek",
    file     = "bsppeek.ext",
    autoload = 1,
#if defined REQUIRE_EXTENSIONS
    required = 1,
#else
    required = 0,
#endif
};

public void __ext_bsppeek_SetNTVOptional()
{
    MarkNativeAsOptional("BSP_MapPathName");
    MarkNativeAsOptional("BSP_SelfTest");
    MarkNativeAsOptional("BSP_EngineBuild");
    MarkNativeAsOptional("BSP_DebugDumpCBSP");
    MarkNativeAsOptional("BSP_DebugDumpCBSPPtr");
    MarkNativeAsOptional("BSP_NumBrushes");
    MarkNativeAsOptional("BSP_NumBrushSides");
    MarkNativeAsOptional("BSP_NumLeaves");
    MarkNativeAsOptional("BSP_NumNodes");
    MarkNativeAsOptional("BSP_NumPlanes");
    MarkNativeAsOptional("BSP_NumBoxBrushes");
    MarkNativeAsOptional("BSP_NumCModels");
    MarkNativeAsOptional("BSP_NumAreas");
    MarkNativeAsOptional("BSP_NumAreaPortals");
    MarkNativeAsOptional("BSP_NumClusters");
    MarkNativeAsOptional("BSP_ClustersVisible");
    MarkNativeAsOptional("BSP_LeavesVisible");
    MarkNativeAsOptional("BSP_VisRowDecompress");
    MarkNativeAsOptional("BSP_AreaInfo");
    MarkNativeAsOptional("BSP_AreaPortalInfo");
    MarkNativeAsOptional("BSP_LeafAtPoint");
    MarkNativeAsOptional("BSP_PointContents");
    MarkNativeAsOptional("BSP_BrushContents");
    MarkNativeAsOptional("BSP_BrushBounds");
    MarkNativeAsOptional("BSP_IsBoxBrush");
    MarkNativeAsOptional("BSP_BrushNumSides");
    MarkNativeAsOptional("BSP_BrushRaw");
    MarkNativeAsOptional("BSP_BrushSidePlane");
    MarkNativeAsOptional("BSP_BrushSideBevel");
    MarkNativeAsOptional("BSP_BrushSideThin");
    MarkNativeAsOptional("BSP_BrushSideTexData");
    MarkNativeAsOptional("BSP_BrushSidePlaneIndex");
    MarkNativeAsOptional("BSP_BrushSideMaterial");
    MarkNativeAsOptional("BSP_BrushIsBoxAuth");
    MarkNativeAsOptional("BSP_PointInBrush");
    MarkNativeAsOptional("BSP_PointContentsBrushes");
    MarkNativeAsOptional("BSP_BrushColumnSpan");
    MarkNativeAsOptional("BSP_BrushTopZAt");
    MarkNativeAsOptional("BSP_BrushSideWinding");
    MarkNativeAsOptional("BSP_BrushClipBox");
    MarkNativeAsOptional("BSP_BrushSideOrder");
    MarkNativeAsOptional("BSP_SeamOrder");
    MarkNativeAsOptional("BSP_SeamCatch");
    MarkNativeAsOptional("BSP_WallEdgePoints");
    MarkNativeAsOptional("BSP_OverlayStart");
    MarkNativeAsOptional("BSP_OverlayStep");
    MarkNativeAsOptional("BSP_OverlayKinds");
    MarkNativeAsOptional("BSP_OverlayQueue");
    MarkNativeAsOptional("BSP_OverlayNearest");
    MarkNativeAsOptional("BSP_HeadSeamCatch");
    MarkNativeAsOptional("BSP_SeamCandidates");
    MarkNativeAsOptional("BSP_TexturebugCatch");
    MarkNativeAsOptional("BSP_PixelwalkCatch");
    MarkNativeAsOptional("BSP_PixelwalkEnd");
    MarkNativeAsOptional("BSP_LeafBrushes");
    MarkNativeAsOptional("BSP_LeafContents");
    MarkNativeAsOptional("BSP_LeafCluster");
    MarkNativeAsOptional("BSP_LeafArea");
    MarkNativeAsOptional("BSP_LeafFlags");
    MarkNativeAsOptional("BSP_LeafDispListStart");
    MarkNativeAsOptional("BSP_LeafDispCount");
    MarkNativeAsOptional("BSP_LeafBounds");
    MarkNativeAsOptional("BSP_EmptyLeaf");
    MarkNativeAsOptional("BSP_SolidLeaf");
    MarkNativeAsOptional("BSP_NodePlane");
    MarkNativeAsOptional("BSP_NodeChildren");
    MarkNativeAsOptional("BSP_NodeBounds");
    MarkNativeAsOptional("BSP_PlaneAt");
    MarkNativeAsOptional("BSP_PlaneType");
    MarkNativeAsOptional("BSP_BoxBrushBounds");
    MarkNativeAsOptional("BSP_BoxBrushOriginalBrush");
    MarkNativeAsOptional("BSP_BoxBrushSurfaceIndex");
    MarkNativeAsOptional("BSP_BoxBrushContents");
    MarkNativeAsOptional("BSP_CModelBounds");
    MarkNativeAsOptional("BSP_CModelOrigin");
    MarkNativeAsOptional("BSP_CModelHeadnode");
    MarkNativeAsOptional("BSP_FindBrushPairAtSeam");
    MarkNativeAsOptional("BSP_FindBoxBrushPairAtSeam");
    MarkNativeAsOptional("BSP_LeafBrushPairAtSeam");
    MarkNativeAsOptional("BSP_RebuildCache");
    MarkNativeAsOptional("BSP_RebuildCacheAsync");
    MarkNativeAsOptional("BSP_CacheIsBuilding");
    MarkNativeAsOptional("BSP_DispHeightAt");
    MarkNativeAsOptional("BSP_DispHeightAtDebug");
    MarkNativeAsOptional("BSP_DispSurfaceNormalAt");
    MarkNativeAsOptional("BSP_DispIsPointOnDisp");
    MarkNativeAsOptional("BSP_DispHeightAtMulti");
    MarkNativeAsOptional("BSP_DispDistToSurface");
    MarkNativeAsOptional("BSP_DispNearestTri");
    MarkNativeAsOptional("BSP_DispTreeIndexAt");
    MarkNativeAsOptional("BSP_DispReady");
    MarkNativeAsOptional("BSP_DispCount");
    MarkNativeAsOptional("BSP_DispGetBounds");
    MarkNativeAsOptional("BSP_DispGetPower");
    MarkNativeAsOptional("BSP_DispGetContents");
    MarkNativeAsOptional("BSP_DispGetSurfaceProps");
    MarkNativeAsOptional("BSP_DispVertCount");
    MarkNativeAsOptional("BSP_DispTriCount");
    MarkNativeAsOptional("BSP_DispGetVert");
    MarkNativeAsOptional("BSP_DispDebugInfo");
    MarkNativeAsOptional("BSP_DispDiagnoseQuery");
    MarkNativeAsOptional("BSP_DispDiskCount");
    MarkNativeAsOptional("BSP_DispDiskBounds");
    MarkNativeAsOptional("BSP_DispDiskDebugInfo");
    MarkNativeAsOptional("BSP_BSPVersion");
    MarkNativeAsOptional("BSP_BSPRevision");
    MarkNativeAsOptional("BSP_LumpInfo");
    MarkNativeAsOptional("BSP_HasLighting");
    MarkNativeAsOptional("BSP_EntityRawLen");
    MarkNativeAsOptional("BSP_EntityRawCopy");
    MarkNativeAsOptional("BSP_EntityCount");
    MarkNativeAsOptional("BSP_EntityClassname");
    MarkNativeAsOptional("BSP_EntityOrigin");
    MarkNativeAsOptional("BSP_EntityKeyValue");
    MarkNativeAsOptional("BSP_EntityModelIndex");
    MarkNativeAsOptional("BSP_TexInfoCount");
    MarkNativeAsOptional("BSP_TexInfoFlags");
    MarkNativeAsOptional("BSP_TexInfoTexData");
    MarkNativeAsOptional("BSP_TexDataCount");
    MarkNativeAsOptional("BSP_TexDataMaterialName");
    MarkNativeAsOptional("BSP_TexDataReflectivity");
    MarkNativeAsOptional("BSP_FindEntityByKeyValue");
    MarkNativeAsOptional("BSP_FaceVertex");
    MarkNativeAsOptional("BSP_FaceCentroid");
    MarkNativeAsOptional("BSP_FaceMaterialName");
    MarkNativeAsOptional("BSP_VisClusterCount");
    MarkNativeAsOptional("BSP_ClusterVisible");
    MarkNativeAsOptional("BSP_LeafVisible");
    MarkNativeAsOptional("BSP_CubemapCount");
    MarkNativeAsOptional("BSP_CubemapOrigin");
    MarkNativeAsOptional("BSP_CubemapSize");
    MarkNativeAsOptional("BSP_NearestCubemap");
    MarkNativeAsOptional("BSP_EdgeCount");
    MarkNativeAsOptional("BSP_EdgeVertices");
    MarkNativeAsOptional("BSP_SurfedgeCount");
    MarkNativeAsOptional("BSP_Surfedge");
    MarkNativeAsOptional("BSP_SurfedgeVertex");
    MarkNativeAsOptional("BSP_VertexCount");
    MarkNativeAsOptional("BSP_VertexPos");
    MarkNativeAsOptional("BSP_FaceCount");
    MarkNativeAsOptional("BSP_FacePlaneNum");
    MarkNativeAsOptional("BSP_FaceFirstEdge");
    MarkNativeAsOptional("BSP_FaceNumEdges");
    MarkNativeAsOptional("BSP_FaceTexInfo");
    MarkNativeAsOptional("BSP_FaceDispInfo");
    MarkNativeAsOptional("BSP_FaceArea");
    MarkNativeAsOptional("BSP_FaceLightStyles");
    MarkNativeAsOptional("BSP_FaceOrigFace");
    MarkNativeAsOptional("BSP_FaceLightOfs");
    MarkNativeAsOptional("BSP_LeafFacesCount");
    MarkNativeAsOptional("BSP_LeafFaces");
    MarkNativeAsOptional("BSP_WorldlightCount");
    MarkNativeAsOptional("BSP_WorldlightOrigin");
    MarkNativeAsOptional("BSP_WorldlightIntensity");
    MarkNativeAsOptional("BSP_WorldlightNormal");
    MarkNativeAsOptional("BSP_WorldlightType");
    MarkNativeAsOptional("BSP_WorldlightStyle");
    MarkNativeAsOptional("BSP_WorldlightCluster");
    MarkNativeAsOptional("BSP_WorldlightShadowCastOffset");
    MarkNativeAsOptional("BSP_WorldlightStopDot");
    MarkNativeAsOptional("BSP_WorldlightStopDot2");
    MarkNativeAsOptional("BSP_WorldlightExponent");
    MarkNativeAsOptional("BSP_WorldlightRadius");
    MarkNativeAsOptional("BSP_WorldlightConstantAttn");
    MarkNativeAsOptional("BSP_WorldlightLinearAttn");
    MarkNativeAsOptional("BSP_WorldlightQuadraticAttn");
    MarkNativeAsOptional("BSP_WorldlightFlags");
    MarkNativeAsOptional("BSP_WorldlightTexInfo");
    MarkNativeAsOptional("BSP_WorldlightOwner");
    MarkNativeAsOptional("BSP_NumStaticProps");
    MarkNativeAsOptional("BSP_StaticPropVersion");
    MarkNativeAsOptional("BSP_StaticPropOrigin");
    MarkNativeAsOptional("BSP_StaticPropAngles");
    MarkNativeAsOptional("BSP_StaticPropSolid");
    MarkNativeAsOptional("BSP_StaticPropFlags");
    MarkNativeAsOptional("BSP_StaticPropModelName");
    MarkNativeAsOptional("BSP_StaticPropSkin");
    MarkNativeAsOptional("BSP_StaticPropFadeDist");
    MarkNativeAsOptional("BSP_StaticPropForcedFadeScale");
    MarkNativeAsOptional("BSP_StaticPropLightingOrigin");
    MarkNativeAsOptional("BSP_StaticPropFlagsEx");
    MarkNativeAsOptional("BSP_StaticPropLeaves");
    MarkNativeAsOptional("BSP_NearestStaticProp");
    MarkNativeAsOptional("BSP_RtStaticPropCount");
    MarkNativeAsOptional("BSP_RtStaticPropBounds");
    MarkNativeAsOptional("BSP_RtStaticPropOrigin");
    MarkNativeAsOptional("BSP_RtStaticPropAngles");
    MarkNativeAsOptional("BSP_RtStaticPropSolid");
    MarkNativeAsOptional("BSP_RtStaticPropSolidFlags");
    MarkNativeAsOptional("BSP_RtStaticPropModelName");
    MarkNativeAsOptional("BSP_StaticPropAtRay");
    MarkNativeAsOptional("BSP_StaticPropProbe");
    MarkNativeAsOptional("BSP_StaticPropTriCount");
    MarkNativeAsOptional("BSP_StaticPropTri");
    MarkNativeAsOptional("BSP_StaticPropNearestTri");
    MarkNativeAsOptional("BSP_StaticPropTraceHull");
    MarkNativeAsOptional("BSP_StaticPropHullSweep");
    MarkNativeAsOptional("BSP_StaticPropDebug");
    MarkNativeAsOptional("BSP_TraceHull");
    MarkNativeAsOptional("BSP_SurfacePropsReady");
    MarkNativeAsOptional("BSP_SurfacePropCount");
    MarkNativeAsOptional("BSP_SurfacePropName");
    MarkNativeAsOptional("BSP_SurfacePropIndex");
    MarkNativeAsOptional("BSP_SurfacePropData");
    MarkNativeAsOptional("BSP_SurfacePropAddress");
    MarkNativeAsOptional("BSP_SurfacePropDump");
    MarkNativeAsOptional("BSP_LeafWaterCount");
    MarkNativeAsOptional("BSP_LeafWaterData");
    MarkNativeAsOptional("BSP_WaterSurfaceZAt");
    MarkNativeAsOptional("BSP_EntityBrushBounds");
    MarkNativeAsOptional("BSP_TriggerPushVelocity");
    MarkNativeAsOptional("BSP_EntityTargetOrigin");
    MarkNativeAsOptional("BSP_FindLadderBrush");
}
