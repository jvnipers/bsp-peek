#if defined _bsppeek_data_included
    #endinput
#endif
#define _bsppeek_data_included

// The engine's collision BSP (CCollisionBSPData): brushes, leaves, nodes, planes, visibility, areas, submodels.

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
// O(log N) BSP node-walk. -1 on failure.
native int  BSP_LeafAtPoint(const float pos[3]);

// One-call "what's at this point?" (in solid? water? playerclip?).
// Walks BSP to leaf, returns leaf's contents flags. 0 if outside map.
native int  BSP_PointContents(const float pos[3]);

// Brush accessors
native int  BSP_BrushContents(int brushIdx);
// false (zeroed outputs) if brushIdx is invalid.
native bool BSP_BrushBounds(int brushIdx, float mins[3], float maxs[3]);
// Heuristic only - CSGO splits box brushes into a parallel `cboxbrush_t` table so the result can false-negative.
native bool BSP_IsBoxBrush(int brushIdx);
native int  BSP_BrushNumSides(int brushIdx);
// Raw cbrush_t header, UNCLAMPED (diagnostic). rawNumSides==0xFFFF (65535) => box-optimized brush (solid in cboxbrush_t).
// A small count that overruns the side table => wrong offsets.
// Fills rawNumSides/firstSide/contents. Returns false (and -1/-1/0) if brushIdx invalid.
native bool BSP_BrushRaw(int brushIdx, int &rawNumSides, int &firstSide, int &contents);
// Read one side's plane normal + signed distance. sideIdx in [0, BSP_BrushNumSides(brushIdx)).
native bool BSP_BrushSidePlane(int brushIdx, int sideIdx, float normal[3], float &dist);
// Is this side a bevel plane? (0=no, 1=yes, -1=invalid).
native int  BSP_BrushSideBevel(int brushIdx, int sideIdx);
// Is this a thin side? (0=no, 1=yes, -1=invalid).
native int  BSP_BrushSideThin(int brushIdx, int sideIdx);
// TEXDATA index for this side (see BSP_TexDataMaterialName). -1 if invalid or none.
native int  BSP_BrushSideTexData(int brushIdx, int sideIdx);
// Plane-table index for this side (see BSP_PlaneAt).
// -1 if invalid or the side's plane pointer doesn't land on a map_planes entry.
native int  BSP_BrushSidePlaneIndex(int brushIdx, int sideIdx);
// Material name for this side. Returns length, 0 if none or invalid.
native int  BSP_BrushSideMaterial(int brushIdx, int sideIdx, char[] buf, int maxlen);

// Exact box-brush test: true iff brushIdx is a box-optimized cbrush
// (numsides == 0xFFFF, its solid living in the cboxbrush_t table. its firstbrushside field holds the cboxbrush index).
// Unlike BSP_IsBoxBrush (a numsides==6 heuristic), this can't false-negative. O(1), backed by a per-map set built on first call.
native bool BSP_BrushIsBoxAuth(int brushIdx);

// Exact brush geometry / collision (plane-accurate).
// A cbrush_t is a convex polytope = intersection of its sides' half-spaces.
// Solving it directly avoids the derived-AABB blind spot in BSP_FindBrushPairAtSeam.

// True if pos is inside (or on) brushIdx (every side: normal·pos <= dist).
native bool BSP_PointInBrush(int brushIdx, const float pos[3]);

// Brush-accurate contents at pos: walks to the leaf, then ORs the contents of every leaf-brush that actually contains pos.
// More precise than leaf-aggregate BSP_PointContents for thin clip/trigger brushes sharing a mostly-empty leaf.
// 0 if outside the map / no brush contains pos.
native int  BSP_PointContentsBrushes(const float pos[3]);

// Vertical extent of brushIdx at column (x, y), by clipping the line (x,y,t) against every brush plane. Fills zMin/zMax.
// false if the column misses the brush footprint.
// Exact brush top/bottom Z at an XY point (the seam candidate).
native bool BSP_BrushColumnSpan(int brushIdx, float x, float y, float &zMin, float &zMax);
// BSP_BrushColumnSpan returning only the top (zMax). false if no span at (x,y).
native bool BSP_BrushTopZAt(int brushIdx, float x, float y, float &z);

// World-space polygon of brush side sideIdx (canonical brush-face winding).
// Writes up to maxVerts verts into verts[][3]. Returns vert count (0 if degenerate / invalid).
// Returns 0 for box-optimized brushes (sides stripped), use BSP_BoxBrushBounds for those.
native int  BSP_BrushSideWinding(int brushIdx, int sideIdx, float verts[][3], int maxVerts);

// Sweep an AABB [mins,maxs] (relative to its center) from start to end against brushIdx.
// Port of the engine's CM_ClipBoxToBrush / IntersectRayWithBoxBrush (DIST_EPSILON = 1/32),
// so the result matches a real player-hull trace against this one brush.
// mins == maxs == 0 is a point trace (bevel sides skipped).
// Fills fraction (1.0 = no hit), hit plane normal, startSolid.
// Returns -1 = invalid brush, 0 = no contact, 1 = contact (or startSolid).
native int  BSP_BrushClipBox(int brushIdx, const float start[3], const float end[3],
                             const float mins[3], const float maxs[3],
                             float &fraction, float normal[3], bool &startSolid);

// Diagnostic: dump each side of brushIdx in engine iteration order (the order CM_ClipBoxToBrush processes them):
// per side, plane normal/dist, PLANE_* type, axis class, bevel/thin. Returns length.
native int  BSP_BrushSideOrder(int brushIdx, char[] buf, int maxlen);

// Order bits returned by BSP_SeamOrder (1 << value).
#define BSPP_SEAM_LOWER_FIRST   0
#define BSPP_SEAM_UPPER_FIRST   1
#define BSPP_SEAM_LOWER_MISSING 2
#define BSPP_SEAM_UPPER_MISSING 3
#define BSPP_SEAM_BOTH_MISSING  4

// Every order player movement can test lowerBrush and upperBrush in (the first one wins a fraction-0 tie),
// for a standing-hull origin on origin + t * along, t in [alongMin, alongMax], at any speed up to speedMax.
// lowerFirst / upperFirst: speed range where that order occurs, -1.0 if never.
// Returns a mask of 1 << BSPP_SEAM_*, -1 when the collision tree is unreadable.
native int BSP_SeamOrder(int lowerBrush, int upperBrush, const float origin[3], const float along[3],
                         float alongMin, float alongMax, float speedMax, float maxspeed, float frametime,
                         float lowerFirst[2], float upperFirst[2]);

// True if the brush-accurate contents at pos include CONTENTS_LADDER.
stock bool BSP_IsLadder(const float pos[3])
{
    return (BSP_PointContentsBrushes(pos) & BSPP_CONTENTS_LADDER) != 0;
}

// Leaf accessors
// Brushes belonging to a leaf in BSP visit order (first SOLID hit wins).
native int  BSP_LeafBrushes(int leaf, int[] buf, int max);

native int  BSP_LeafContents(int leafIdx);
native int  BSP_LeafCluster(int leafIdx);    // PVS cluster index
native int  BSP_LeafArea(int leafIdx);       // area portal grouping (low 9 bits)
native int  BSP_LeafFlags(int leafIdx);      // misc flags (high 7 bits of the same short)
// First entry of this leaf's run in the engine's displacement list. -1 if invalid.
native int  BSP_LeafDispListStart(int leafIdx);
// Number of displacements touching this leaf. -1 if invalid.
native int  BSP_LeafDispCount(int leafIdx);
// Leaf AABB via BSP tree-walk (splits parent box on axis-aligned planes). Lazy-built.
// World tree only: brush-entity submodel leaves/nodes return false (also BSP_NodeBounds).
native bool BSP_LeafBounds(int leafIdx, float mins[3], float maxs[3]);

// Sentinel leaf indices. EmptyLeaf = void leaf, SolidLeaf = fully-solid leaf.
native int  BSP_EmptyLeaf();
native int  BSP_SolidLeaf();

// Node accessors (manual BSP walking, LeafAtPoint usually suffices)
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
// Per-face TEXDATA indices: surf[6] = {-X,-Y,-Z,+X,+Y,+Z}. Not a physics surfaceprop index.
native bool BSP_BoxBrushSurfaceIndex(int idx, int surf[6]);
// Contents flags via originalBrush lookup. 0 if invalid.
native int  BSP_BoxBrushContents(int idx);

// Submodels (cmodel_t) - for func_brush, doors, breakables etc.
// idx in [0, BSP_NumCModels()). idx 0 is the world model.
// Bounds are model-local, padded 1 unit per side by the engine.
native bool BSP_CModelBounds(int idx, float mins[3], float maxs[3]);
native bool BSP_CModelOrigin(int idx, float origin[3]);
// Root BSP node for this submodel's collision tree. -1 if invalid.
native int  BSP_CModelHeadnode(int idx);

// "High-level" pixelsurf. Searches all brushes whose XY AABB contains samplePos.
// outLower = brush w/ maxs.z == seamZ, outUpper = brush w/ mins.z == seamZ.
// Returns true if both found.
native bool BSP_FindBrushPairAtSeam(const float samplePos[3], float seamZ,
                                    int &outLowerBrush, int &outUpperBrush);

// Box-table analog of BSP_FindBrushPairAtSeam for CSGO box-optimized walls. Scans cboxbrush_t directly.
// outLower/outUpper are BOX-table indices (use BSP_BoxBrush* accessors):
// outLower w/ maxs.z == seamZ, outUpper w/ mins.z == seamZ. true if both found.
native bool BSP_FindBoxBrushPairAtSeam(const float samplePos[3], float seamZ,
                                       int &outLowerBox, int &outUpperBox);

// BSP_FindBrushPairAtSeam + the leaf-visit-order gate in one call.
// Resolves the lower/upper brushes at the seam, then samples the leaf just below seamZ and reports each brush's position in that leaf's brush list.
// CSGO inverts leaf-visit order vs TF2, so lowerPos < upperPos (engine visits the bottom brush first) is the pixelsurf-eligible ordering.
//   outLeaf = leaf sampled below the seam (-1 if lookup failed)
//   outLowerPos/outUpperPos = brush position within that leaf (-1 if absent)
// Returns true iff both brushes found AND in the leaf AND lowerPos < upperPos.
native bool BSP_LeafBrushPairAtSeam(const float samplePos[3], float seamZ,
                                    int &outLowerBrush, int &outUpperBrush,
                                    int &outLeaf, int &outLowerPos, int &outUpperPos);

// Brush AABB cache
// Force a fresh build of the per-map brush AABB cache (synchronous, blocks).
// Returns the brush count after rebuild (0 = ext unable to read BSP data).
native int  BSP_RebuildCache();

// Async cache build - kicks off a worker thread, returns immediately.
// A BSP_FindBrushPairAtSeam call before swap-in builds the cache itself.
// Use this in OnMapStart to avoid stalling the server during map load.
// Returns brush count (or 0 if no BSP data).
native int  BSP_RebuildCacheAsync();

// True between BSP_RebuildCacheAsync() and the worker's swap-in.
native bool BSP_CacheIsBuilding();
