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

// Debug: hex-dump CCollisionBSPData bytes from [startOff, endOff) to SourceMod log.
// Used to RE field offsets. Range clamped server-side.
native void BSP_DebugDumpCBSP(int startOff, int endOff);
// Debug: read pointer at CCollisionBSPData+ptrOff, dump `bytes` bytes from there.
// UNSAFE: bad ptrOff may crash. Use only with known-pointer offsets.
native void BSP_DebugDumpCBSPPtr(int ptrOff, int bytes);

// Counts
native int  BSP_NumBrushes();
native int  BSP_NumBrushSides();
native int  BSP_NumLeaves();
native int  BSP_NumNodes();
native int  BSP_NumPlanes();
native int  BSP_NumBoxBrushes();     // CSGO SIMD box-brush table (cboxbrush_t)
native int  BSP_NumCModels();        // Submodels (func_brush, doors, breakables, etc.)
native int  BSP_NumAreas();          // darea_t count (area-portal groupings)
native int  BSP_NumAreaPortals();    // dareaportal_t count
native int  BSP_NumClusters();       // PVS cluster count (from dvis_t header, 0 if no vis data)

// Visibility (PVS) - compressed per-cluster bit-vectors.
// True if c1 can see c2. Auto-true when c1==c2. False if either idx invalid.
native bool BSP_ClustersVisible(int c1, int c2);
// BSP_LeafCluster(leaf1) + BSP_LeafCluster(leaf2) + ClustersVisible.
native bool BSP_LeavesVisible(int leaf1, int leaf2);
// Decompress cluster's PVS row into buf as a bit-vector.
// Output is (BSP_NumClusters()+7)/8 bytes (or maxBytes, whichever smaller).
// Returns bytes written. Bit `c2` set iff c1 can see c2.
native int  BSP_VisRowDecompress(int cluster, int[] buf, int maxBytes);

// Areas (darea_t) - area-portal grouping.
// Each area is a connected region bounded by func_areaportals. Default area = 0.
// Reads numportals + firstportal (index into map_areaportals). false if invalid.
native bool BSP_AreaInfo(int areaIdx, int &numPortals, int &firstPortal);

// Area portals (dareaportal_t) - one per portal-side. Two portals per door.
// Reads all dareaportal_t fields. false if invalid.
// otherArea = area on the far side. planenum = index into map_planes.
native bool BSP_AreaPortalInfo(int portalIdx, int &portalKey, int &otherArea,
                               int &firstClipVert, int &clipVerts, int &planenum);

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

// Min 3D distance from pos to any displacement collision surface, searching up
// to maxDist units. Returns BSP_DISP_NO_HIT if no disp data / nothing in range.
// Works on near-vertical disp WALLS.
native float BSP_DispDistToSurface(const float pos[3], float maxDist = 64.0);

// Nearest disp triangle to pos within maxDist. Returns the distance (BSP_DISP_NO_HIT
// if none) and fills the tri's plane normal + 3 world-space verts.
// Reveals the true collision-mesh orientation at a seam (the hull-contact normal is coarse).
native float BSP_DispNearestTri(const float pos[3], float maxDist,
                                float normal[3], float v0[3], float v1[3], float v2[3]);

// Nearest disp tree/face index (per displacement) within maxDist; -1 if none.
native int   BSP_DispTreeIndexAt(const float pos[3], float maxDist);

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

// True if the map has baked lighting
// (LUMP_LIGHTING or LUMP_LIGHTING_HDR filelen > 0).
// Engine forces mat_fullbright=1 when both are empty.
native bool  BSP_HasLighting();

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

// Convenience / compound queries

// Entity search: returns first entity index >= startIdx where kv[key]==value.
// Returns -1 if not found. Enumerate by calling again with lastResult+1.
native int   BSP_FindEntityByKeyValue(const char[] key, const char[] value, int startIdx = 0);

// Face vertex at polygon slot [0, BSP_FaceNumEdges(faceIdx)).
// Shorthand for FirstEdge→SurfedgeVertex→VertexPos. false if OOB.
native bool  BSP_FaceVertex(int faceIdx, int slot, float pos[3]);
// Average world-space position of all face vertices. false if face invalid.
native bool  BSP_FaceCentroid(int faceIdx, float centroid[3]);
// Material name for this face (chains TexInfo→TexData→string). Returns length.
// Returns 0 for skip/nodraw/tool faces with no texdata.
native int   BSP_FaceMaterialName(int faceIdx, char[] buf, int maxlen);

// Visibility
// Number of PVS clusters. 0 if map has no vis data.
native int   BSP_VisClusterCount();
// PVS check: is cluster 'other' potentially visible from cluster 'cluster'?
// Uses BSP_LeafCluster to convert leaf -> cluster. false if either OOB or no vis.
native bool  BSP_ClusterVisible(int cluster, int other);
// PVS check by leaf: wraps BSP_LeafCluster(a/b) + BSP_ClusterVisible.
// false if either leaf has no cluster (cluster == -1) or no vis data.
native bool  BSP_LeafVisible(int leafA, int leafB);

// Cubemaps (env_cubemap placements)
native int   BSP_CubemapCount();
// World-space origin (stored as int[3] in BSP; returned as float[3]). false if OOB.
native bool  BSP_CubemapOrigin(int idx, float origin[3]);
// Raw size value. Resolution = 2^(size-1) px per face. 0 = default (engine uses 6 -> 32px). -1 if OOB.
native int   BSP_CubemapSize(int idx);
// Index of the cubemap whose origin is closest to pos. -1 if no cubemaps.
native int   BSP_NearestCubemap(const float pos[3]);

// Edges (LUMP_EDGES = 12, dedge_t = 4B: uint16 v[2])
native int   BSP_EdgeCount();
// Fills v0/v1 with the two vertex indices of edge[idx]. false if OOB.
native bool  BSP_EdgeVertices(int idx, int &v0, int &v1);

// Surfedges (LUMP_SURFEDGES = 13, signed int32 array)
// Positive = edge forward (v[0] -> v[1]); negative = edge backward.
native int   BSP_SurfedgeCount();
// Raw signed surfedge value at index idx. 0 if OOB.
native int   BSP_Surfedge(int idx);
// Resolves surfedge[idx] to the "start" vertex of that directed edge.
// Accounts for sign - use this to enumerate face vertices in winding order:
//   for (int i = 0; i < BSP_FaceNumEdges(f); i++)
//       vertIdx = BSP_SurfedgeVertex(BSP_FaceFirstEdge(f) + i);
// Returns -1 if OOB or bad edge reference.
native int   BSP_SurfedgeVertex(int idx);

// Vertexes (LUMP_VERTEXES = 3)
// Total vertex count in the BSP.
native int   BSP_VertexCount();
// World-space position of vertex[idx]. false if out of range.
native bool  BSP_VertexPos(int idx, float pos[3]);

// Faces (LUMP_FACES = 7, dface_t)
// Note: BSP faces are the renderer's split faces. Use BSP_FaceOrigFace to map
// back to the original editor face. Displacement faces have FaceDispInfo >= 0.
native int   BSP_FaceCount();
// Plane index into BSP plane table (see BSP_PlaneAt). -1 if out of range.
native int   BSP_FacePlaneNum(int idx);
// First index into the surfedge table (for edge/vertex reconstruction). -1 if OOB.
native int   BSP_FaceFirstEdge(int idx);
// Number of surfedges (= polygon vertex count) for this face. -1 if OOB.
native int   BSP_FaceNumEdges(int idx);
// Texinfo index. -1 if skip/nodraw or out of range.
native int   BSP_FaceTexInfo(int idx);
// Dispinfo index if this face is a displacement, -1 otherwise.
native int   BSP_FaceDispInfo(int idx);
// Precomputed face area in world units^2. 0.0 if out of range.
native float BSP_FaceArea(int idx);
// Fills styles[4] with lightstyle indices (255 = unused slot). false if OOB.
native bool  BSP_FaceLightStyles(int idx, int styles[4]);
// Original (pre-split) face index; -1 if this face was never split.
native int   BSP_FaceOrigFace(int idx);
// Byte offset into the lighting lump for this face's lightmap samples; -1 if none.
native int   BSP_FaceLightOfs(int idx);

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

// Static props (sprp game lump in LUMP_GAME_LUMP)
// Parsed at map load from the BSP file's version-stable StaticPropLump_t prefix.
// NOTE: this is the prop PLACEMENT (origin/angles/model), NOT its collision mesh -
// static props collide via VPhysics (the model's .phy), which is not in the BSP.
// Use these to identify which prop a ref sits on and key/cache per-prop.

// Number of static props. 0 if none / ext absent.
native int   BSP_NumStaticProps();
// sprp game-lump version (commonly 10 or 11). 0 if no static props.
native int   BSP_StaticPropVersion();
// World-space placement origin. false if OOB.
native bool  BSP_StaticPropOrigin(int idx, float origin[3]);
// Placement angles (pitch, yaw, roll). false if OOB.
native bool  BSP_StaticPropAngles(int idx, float angles[3]);
// SOLID_* collision type (6 = VPHYSICS, 2 = BBOX, 0 = not solid). -1 if OOB.
native int   BSP_StaticPropSolid(int idx);
// STATIC_PROP_FLAG_* bitmask. -1 if OOB.
native int   BSP_StaticPropFlags(int idx);
// Model path (e.g. "models/props/foo.mdl"). Returns string length.
native int   BSP_StaticPropModelName(int idx, char[] buf, int maxlen);
// BSP leaf indices this prop touches. Returns count written (clamped to max).
native int   BSP_StaticPropLeaves(int idx, int[] buf, int max);
// Index of the prop whose origin is nearest pos within maxDist (<=0 = unlimited). -1 if none.
native int   BSP_NearestStaticProp(const float pos[3], float maxDist = 0.0);

// Sweep the player AABB [mins,maxs] from start to end against static prop `idx`
// (at the prop's origin/angles) using VPhysics - the exact engine collision,
// isolated to this one prop so unrelated world geometry never start-solids it.
// Fills fraction (1.0 = no hit), endpos, surface normal, and startSolid.
// Returns: -1 = unavailable (ext built without metamod / no IPhysicsCollision /
//               prop has no collision model), 0 = resolved no hit, 1 = hit.
// Requires the engine/vphysics interfaces, check BSPPeek_Available()
// for the ext and treat -1 as "fall back to a normal trace".
native int   BSP_StaticPropTraceHull(int idx, const float start[3], const float end[3],
                                     const float mins[3], const float maxs[3],
                                     float &fraction, float endpos[3], float normal[3],
                                     bool &startSolid);

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
