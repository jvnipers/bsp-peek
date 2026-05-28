#if defined _bsppeek_included
    #endinput
#endif
#define _bsppeek_included

// BSP-Peek - "peek" into engine CCollisionBSPData
// (leaf membership, ordered brush lists, brush type, AABB, BSP node-walk).
// Plus displacement surface queries via engine memory reader (primary)
// with BSP file-parse fallback when engine reader unavailable.
// Requires bsppeek.ext loaded.
// All natives return safe defaults if the ext isn't loaded;
// check BSPPeek_Available() first if you need certainty.

// Source-engine CONTENTS_* constants commonly checked:
#define BSPP_CONTENTS_SOLID      0x1
#define BSPP_CONTENTS_PLAYERCLIP 0x10000
#define BSPP_CONTENTS_GRATE      0x8

// Misc
// Engine's internal map path string. Returns string length.
native int  BSP_MapPathName(char[] buf, int maxlen);

// Counts
native int  BSP_NumBrushes();
native int  BSP_NumBrushSides();
native int  BSP_NumLeaves();
native int  BSP_NumNodes();
native int  BSP_NumPlanes();
native int  BSP_NumBoxBrushes();    // CSGO SIMD box-brush table (cboxbrush_t)
native int  BSP_NumCModels();       // Submodels (func_brush, doors, breakables, etc.)

// Point queries
// O(log N) BSP node-walk; -1 on failure.
native int  BSP_LeafAtPoint(const float pos[3]);

// One-call "what's at this point?" (in solid? water? playerclip?).
// Walks BSP to leaf, returns leaf's contents flags. 0 if outside map.
native int  BSP_PointContents(const float pos[3]);

// Brush accessors
native int  BSP_BrushContents(int brushIdx);
native void BSP_BrushBounds(int brushIdx, float mins[3], float maxs[3]);
// Heuristic only - CSGO splits box brushes into a parallel `cboxbrush_t` table
// so the result can false-negative. Treat as informational, not a hard filter.
native bool BSP_IsBoxBrush(int brushIdx);
native int  BSP_BrushNumSides(int brushIdx);
// Read one side's plane normal + signed distance. sideIdx in [0, BSP_BrushNumSides(brushIdx)).
native bool BSP_BrushSidePlane(int brushIdx, int sideIdx, float normal[3], float &dist);
// Is this side a bevel plane? (0=no, 1=yes, -1=invalid).
native int  BSP_BrushSideBevel(int brushIdx, int sideIdx);
// Is this a thin side? (0=no, 1=yes, -1=invalid).
native int  BSP_BrushSideThin(int brushIdx, int sideIdx);
// Texinfo table index for this side. -1 if invalid.
native int  BSP_BrushSideTexInfo(int brushIdx, int sideIdx);

// Leaf accessors
// Brushes belonging to a leaf in BSP visit order (first SOLID hit wins).
native int  BSP_LeafBrushes(int leaf, int[] buf, int max);

native int  BSP_LeafContents(int leafIdx);
native int  BSP_LeafCluster(int leafIdx);    // PVS cluster index
native int  BSP_LeafArea(int leafIdx);       // area portal grouping (low 9 bits)
native int  BSP_LeafFlags(int leafIdx);      // misc flags (high 7 bits of the same short)
// First leaf-face index in the engine's leaf-face table. -1 if invalid.
native int  BSP_LeafFirstFace(int leafIdx);
// Number of faces belonging to this leaf. -1 if invalid.
native int  BSP_LeafNumFaces(int leafIdx);
// Leaf AABB via BSP tree-walk (splits parent box on axis-aligned planes). Lazy-built.
native bool BSP_LeafBounds(int leafIdx, float mins[3], float maxs[3]);

// Sentinel leaf indices. EmptyLeaf = void leaf, SolidLeaf = fully-solid leaf.
native int  BSP_EmptyLeaf();
native int  BSP_SolidLeaf();

// Node accessors (manual BSP walking; LeafAtPoint usually suffices)
native bool BSP_NodePlane(int nodeIdx, float normal[3], float &dist);
// Negative child encodes a leaf via `leafIdx = -1 - child`.
native bool BSP_NodeChildren(int nodeIdx, int &leftChild, int &rightChild);
// Node AABB via BSP tree-walk (same cache as LeafBounds).
native bool BSP_NodeBounds(int nodeIdx, float mins[3], float maxs[3]);

// Plane table access
// planeIdx in [0, BSP_NumPlanes())
native bool BSP_PlaneAt(int planeIdx, float normal[3], float &dist);
// PLANE_X(0)/Y(1)/Z(2)/ANYX(3)/ANYY(4)/ANYZ(5). -1 if invalid.
native int  BSP_PlaneType(int planeIdx);

// Box brush (cboxbrush_t) - SIMD-optimized axis-aligned brushes.
// idx in [0, BSP_NumBoxBrushes()). VectorAligned mins/maxs (16B each).
native bool BSP_BoxBrushBounds(int idx, float mins[3], float maxs[3]);
// Original cbrush_t index this box brush was derived from. -1 if invalid.
native int  BSP_BoxBrushOriginalBrush(int idx);
// Per-face surface property indices: surf[6] = {-X,+X,-Y,+Y,-Z,+Z}.
native bool BSP_BoxBrushSurfaceIndex(int idx, int surf[6]);
// Contents flags via originalBrush lookup. 0 if invalid.
native int  BSP_BoxBrushContents(int idx);

// Submodels (cmodel_t) - for func_brush, doors, breakables etc.
// idx in [0, BSP_NumCModels()). idx 0 is the world model.
native bool BSP_CModelBounds(int idx, float mins[3], float maxs[3]);
native bool BSP_CModelOrigin(int idx, float origin[3]);
// Root BSP node for this submodel's collision tree. -1 if invalid.
native int  BSP_CModelHeadnode(int idx);

// "High-level" pixelsurf
// Iterates all brushes for any whose XY AABB contains samplePos AND whose top/bottom equals seamZ.
// Returns true if BOTH found. outLower = brush w/ maxs.z == seamZ, outUpper = brush w/ mins.z == seamZ.
native bool BSP_FindBrushPairAtSeam(const float samplePos[3], float seamZ,
                                    int &outLowerBrush, int &outUpperBrush);

// Brush AABB cache
// Force a fresh build of the per-map brush AABB cache (synchronous; blocks).
// Returns the brush count after rebuild (0 = ext unable to read BSP data).
native int  BSP_RebuildCache();

// Async cache build - kicks off a worker thread, returns immediately.
// Reads via BSP_FindBrushPairAtSeam block until the worker swaps the cache in.
// Use this in OnMapStart to avoid stalling the server during map load.
// Returns brush count (or 0 if no BSP data).
native int  BSP_RebuildCacheAsync();

// True between BSP_RebuildCacheAsync() and the worker's swap-in.
native bool BSP_CacheIsBuilding();

// Displacements
// Primary path: reads CDispCollTree array directly from engine memory.
//   (canonical post-stitching verts; matches what TR_TraceRay would hit).
// Fallback: parses BSP file lumps (DISPINFO/DISP_VERTS/FACES) at map load
//   and builds triangle meshes from scratch.
// Check BSP_DispReady() to know if the engine reader is initialized; if not,
// unified queries fall back to disk-parsed data automatically.

#define BSP_DISP_NO_HIT -1.0e30    // sentinel returned by BSP_DispHeightAt for no XY match

// - Unified queries (engine-first, disk fallback) -

// Highest world-space Z at (x, y) across all displacements that contain
// the XY query point. Returns BSP_DISP_NO_HIT (-1e30) if none.
native float BSP_DispHeightAt(float x, float y);

// Same as BSP_DispHeightAt + returns the matching disp/tree index (-1 if none).
native float BSP_DispHeightAtDebug(float x, float y, int &outIdx);

// Returns surface Z + populates normal[3] with the tri's plane normal at (x,y).
// Returns BSP_DISP_NO_HIT if no match.
native float BSP_DispSurfaceNormalAt(float x, float y, float normal[3]);

// True if any disp tri actually contains (x,y).
native bool  BSP_DispIsPointOnDisp(float x, float y);

// Returns ALL Z values at (x,y) (not just max). Useful for stacked disps
// (e.g., underground shell + ground surface). Returns count written.
native int   BSP_DispHeightAtMulti(float x, float y, float[] results, int maxResults);

// - Engine accessors (no fallback; check BSP_DispReady first) -

// True once engine globals have been resolved AND tree count > 0.
native bool  BSP_DispReady();

// Number of CDispCollTree entries (canonical engine count).
native int   BSP_DispCount();

// Engine AABB for disp tree[idx]. false if not ready or out of range.
native bool  BSP_DispGetBounds(int idx, float mins[3], float maxs[3]);

// m_nPower from engine tree[idx]. -1 if unavailable.
native int   BSP_DispGetPower(int idx);

// m_nContents from engine tree[idx] (CONTENTS_* flags). 0 if unavailable.
native int   BSP_DispGetContents(int idx);

// m_nSurfaceProps[4] (4 shorts) from engine tree[idx]. false if unavailable.
native bool  BSP_DispGetSurfaceProps(int idx, int props[4]);

// Vertex/triangle counts from engine tree[idx]. -1 if unavailable.
native int   BSP_DispVertCount(int idx);
native int   BSP_DispTriCount(int idx);

// Read a single vertex position from engine tree's m_aVerts buffer.
// false if out of range or engine not ready.
native bool  BSP_DispGetVert(int idx, int vertIdx, float pos[3]);

// Diagnostic: dump tree[idx]'s bbox + power + counts + raw bytes for verification.
native int   BSP_DispDebugInfo(int idx, char[] buf, int maxlen);

// Diagnostic: explain why BSP_DispHeightAt returned its result at (x,y).
native int   BSP_DispDiagnoseQuery(float x, float y, char[] buf, int maxlen);

// - Disk-only explicit access -
// Disk-parsed disp data, separate from engine reader.
// NOTE: disk indices do NOT correspond to engine indices.

native int   BSP_DispDiskCount();
native bool  BSP_DispDiskBounds(int idx, float mins[3], float maxs[3]);
// Diagnostic: dump disk-parsed disp[idx] (bbox + base quad corners + face/power).
native int   BSP_DispDiskDebugInfo(int idx, char[] buf, int maxlen);

// BSP file lump readers
// Disk-parsed (not live engine memory). Loaded at map start, cached per-map.

// BSP file header
native int   BSP_BSPVersion();
native int   BSP_BSPRevision();    // map revision (vbsp -revision)
// Returns lump fileofs/length/version into out vars. lumpId in [0, 64).
native bool  BSP_LumpInfo(int lumpId, int &fileofs, int &length, int &version);

// Entities (LUMP_ENTITIES, raw KeyValues text)
// Raw text length (no null terminator counted).
native int   BSP_EntityRawLen();
// Copy raw entity text into buf. Returns bytes copied (excl. null).
native int   BSP_EntityRawCopy(char[] buf, int maxlen);
// Number of parsed entity blocks.
native int   BSP_EntityCount();
// "classname" key for entity[idx]. Returns string length.
native int   BSP_EntityClassname(int idx, char[] buf, int maxlen);
// Parsed "origin" key. false if missing.
native bool  BSP_EntityOrigin(int idx, float origin[3]);
// Lookup arbitrary key on entity[idx]. Returns value length, 0 if missing.
native int   BSP_EntityKeyValue(int idx, const char[] key, char[] buf, int maxlen);

// Texinfo / Texdata (LUMP_TEXINFO + LUMP_TEXDATA)
native int   BSP_TexInfoCount();
native int   BSP_TexInfoFlags(int idx);      // SURF_* flags
native int   BSP_TexInfoTexData(int idx);    // texdata index, -1 if invalid
native int   BSP_TexDataCount();
// Material name (e.g. "concrete/concrete_01"). Returns length.
native int   BSP_TexDataMaterialName(int texdataIdx, char[] buf, int maxlen);
native bool  BSP_TexDataReflectivity(int texdataIdx, float refl[3]);

// LeafFaces (LUMP_LEAFFACES, uint16 face index table)
// Total leaf-face entries (sum across all leaves).
native int   BSP_LeafFacesCount();
// Read this leaf's face indices via cleaf_t.firstleafface/numleaffaces.
// Returns count written. Convenience over LeafFirstFace + LeafNumFaces + table.
native int   BSP_LeafFaces(int leafIdx, int[] buf, int max);

// Worldlights (LUMP_WORLDLIGHTS_LDR; falls back to LDR if no HDR; LDR-only if both absent)
native int   BSP_WorldlightCount();
native bool  BSP_WorldlightOrigin(int idx, float origin[3]);
native bool  BSP_WorldlightIntensity(int idx, float intensity[3]);
native bool  BSP_WorldlightNormal(int idx, float normal[3]);
native int   BSP_WorldlightType(int idx);       // emit_t: 0=surface 1=point 2=spotlight 3=skylight 4=quakelight 5=skyambient
native int   BSP_WorldlightStyle(int idx);      // style index for animated lights
native int   BSP_WorldlightCluster(int idx);    // PVS cluster
native bool  BSP_WorldlightShadowCastOffset(int idx, float offset[3]);
native float BSP_WorldlightStopDot(int idx);          // spotlight inner cone cos(angle)
native float BSP_WorldlightStopDot2(int idx);         // spotlight outer cone cos(angle)
native float BSP_WorldlightExponent(int idx);         // spotlight falloff exponent
native float BSP_WorldlightRadius(int idx);           // max effective radius (0 = unlimited)
native float BSP_WorldlightConstantAttn(int idx);     // attenuation: constant term
native float BSP_WorldlightLinearAttn(int idx);       // attenuation: linear term
native float BSP_WorldlightQuadraticAttn(int idx);    // attenuation: quadratic term
native int   BSP_WorldlightFlags(int idx);            // DWL_FLAGS_* bitmask
native int   BSP_WorldlightTexInfo(int idx);          // associated texinfo index for surface lights, -1 if none
native int   BSP_WorldlightOwner(int idx);            // entity index owning this light, -1 if none

stock bool   BSPPeek_Available()
{
    return LibraryExists("bsppeek");
}

public Extension __ext_bsppeek = {
    name     = "BSP-Peek",
    file     = "bsppeek.ext",
    autoload = 1,
    required = 0,
};
