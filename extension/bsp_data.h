#ifndef _INCLUDE_BSPPEEK_BSP_DATA_H_
#define _INCLUDE_BSPPEEK_BSP_DATA_H_

#include "smsdk_ext.h"
#include <cstdint>

// BSP collision data accessor (CCollisionBSPData).
// Reads brush/leaf/plane/node tables directly from engine memory via gamedata offsets.
namespace BSPData
{
	bool Init(IGameConfig *gameconf, char *error, size_t maxlen);
	void Shutdown();
	void OnMapStart();

	void *DebugGetBase();
	int DebugGetOff(const char *name);
	// Hex dump of first N box brushes for layout RE. Logs via smutils.
	void DebugDumpBoxBrushes(int maxCount);
	// Hex dump of `g_pBSPData + [startOff, endOff)` for offset RE.
	// Range clamped to [0, 4096] and (end-start) <= 2048.
	void DebugDumpCBSP(int startOff, int endOff);
	// Read a pointer at `g_pBSPData + ptrOff`, hex-dump `bytes` bytes from there.
	// `bytes` clamped to [0, 1024]. No validation, bad inputs may crash.
	void DebugDumpCBSPPtr(int ptrOff, int bytes);

	int MapPathName(char *buf, int maxlen);
	int EmptyLeaf();
	int SolidLeaf();

	// Bitmask of BSPData subsystems that deref + sanity-check OK.
	// Bit 0 (0x1): g_BSPData base resolved + brush count in sane range.
	// Bit 1 (0x2): leaf + node tables present.
	// Bit 2 (0x4): cboxbrush_t SIMD table present.
	// Bit 3 (0x8): visibility blob present.
	// 0 = base unresolved or g_BSPData points at garbage (post-update sig break).
	int SelfTest();

	// Counts
	int GetNumBrushes();
	int GetNumBrushSides();
	int GetNumPlanes();
	int GetNumLeafBrushes();
	int GetNumLeaves();
	int GetNumNodes();
	int GetNumBoxBrushes();
	int GetNumCModels();     // Submodels (func_brush etc.)
	int GetNumAreas();       // Area-portal groupings (engine `numareas`).
	int GetNumAreaPortals(); // dareaportal_t count.
	// Cluster count from dvis_t header. 0 if no vis data.
	int GetNumClusters();

	// Visibility (PVS)
	// True if cluster c1 can see c2. Auto-true when c1==c2.
	// False if either index invalid or no vis data.
	bool ClustersVisible(int c1, int c2);
	// LeafCluster(leaf1) + LeafCluster(leaf2) + ClustersVisible.
	bool LeavesVisible(int leaf1, int leaf2);
	// Decompress cluster's PVS row into outBuf. Returns bytes written.
	// Output is (numclusters+7)/8 bytes (or maxBytes, whichever smaller).
	int VisRowDecompress(int cluster, uint8_t *outBuf, int maxBytes);

	// Read numportals + firstportal for area. false if invalid.
	bool AreaInfo(int areaIdx, int &numPortals, int &firstPortal);

	// Read all dareaportal_t fields. false if invalid.
	bool AreaPortalInfo(int portalIdx, int &portalKey, int &otherArea, int &firstClipVert, int &clipVerts, int &planenum);

	// Point queries
	// O(log N) BSP node-walk via `map_nodes`. Returns leaf index, -1 on failure.
	int LeafAtPoint(const float pos[3]);
	// LeafAtPoint + LeafContents. Returns contents flags, 0 on failure.
	int PointContents(const float pos[3]);

	// Brush accessors
	int GetBrushContents(int brushIdx);
	bool GetBrushBounds(int brushIdx, float mins[3], float maxs[3]);
	// Heuristic: numsides==6 + all 6 plane normals axis-aligned.
	// CSGO splits true box brushes into a parallel `cboxbrush_t` table,
	// so this can false-negative on box-optimized brushes. Treat as informational.
	bool IsBoxBrush(int brushIdx);
	int BrushNumSides(int brushIdx);
	// Raw cbrush_t header (unclamped). rawNumSides==0xFFFF marks a box-optimized brush.
	bool BrushRaw(int brushIdx, int &rawNumSides, int &firstSide, int &contents);
	// Read one side's plane normal + signed distance.
	bool BrushSidePlane(int brushIdx, int sideIdx, float normal[3], float &dist);
	// Bevel plane? (0=no, 1=yes, -1=invalid).
	int BrushSideBevel(int brushIdx, int sideIdx);
	// Thin side? (0=no, 1=yes, -1=invalid).
	int BrushSideThin(int brushIdx, int sideIdx);
	// Texinfo index for this side. -1 if invalid.
	int BrushSideTexInfo(int brushIdx, int sideIdx);
	// Plane table index for this side. -1 if invalid or the pointer doesn't land on a map_planes entry.
	int BrushSidePlaneIndex(int brushIdx, int sideIdx);

	// True if brushIdx is the originalBrush of some cboxbrush_t entry.
	// Backed by a per-map set built lazily on first call (rebuilt when the box count changes), so lookups are O(1).
	bool BrushIsBoxAuth(int brushIdx);

	// Exact brush geometry / collision (plane-accurate).
	// A cbrush_t is a convex polytope:
	// the intersection of its sides' half-spaces (p is inside when normal.p <= dist for every side).
	// These solve that polytope directly, so they avoid FindBrushPairAtSeam's derived-AABB blind spot for box-optimized / bevel-padded brushes.

	// True if pos lies inside (or on) brush brushIdx.
	bool PointInBrush(int brushIdx, const float pos[3]);

	// Brush-accurate contents at pos: walk to the leaf, then OR the contents of every leaf-brush that actually contains pos.
	// More precise than leaf-aggregate PointContents for thin clip/trigger brushes that share a mostly-empty leaf.
	// 0 if outside the map or no brush contains pos.
	int PointContentsBrushes(const float pos[3]);

	// Vertical extent of brush brushIdx at column (x, y), by clipping the infinite line (x, y, t) against every brush plane.
	// Fills [zMin, zMax]. false if the column misses the footprint or the brush is degenerate.
	bool BrushColumnSpan(int brushIdx, float x, float y, float &zMin, float &zMax);
	// BrushColumnSpan returning only the top (zMax). false if no span at (x, y).
	bool BrushTopZAt(int brushIdx, float x, float y, float &z);

	// World-space polygon of brush side sideIdx, built by clipping the side's plane against all other sides of the brush.
	// Writes up to maxVerts verts into outVerts (flat float[maxVerts*3], CCW about the side normal).
	// Returns vert count (0 if degenerate / invalid).
	int BrushSideWinding(int brushIdx, int sideIdx, float *outVerts, int maxVerts);

	// Sweep an AABB [mins,maxs] (relative to its center) from start to end against brush brushIdx.
	// Direct port of CM_ClipBoxToBrush (DIST_EPSILON = 1/32), so the result matches a real player-hull trace against this one brush.
	// Fills fraction (1.0 = no hit), the hit plane normal, and startSolid.
	// Returns -1 = invalid brush, 0 = no contact, 1 = contact (or startSolid).
	int BrushClipBox(int brushIdx, const float start[3], const float end[3], const float mins[3], const float maxs[3], float &fraction,
					 float normal[3], bool &startSolid);

	// Diagnostic: dump each side of brushIdx in engine iteration order
	//(the order CM_ClipBoxToBrush processes them, which the pixelsurf gate depends on):
	// per side, plane normal/dist, PLANE_* type, axis classification, bevel/thin.
	// Writes into buf, returns length.
	int BrushSideOrder(int brushIdx, char *buf, int maxlen);

	// Leaf accessors
	// Brushes belonging to a leaf in BSP visit order (first SOLID hit wins).
	int LeafBrushes(int leafIdx, int *outBuf, int maxOut);
	// Contents flags (CONTENTS_SOLID etc) at cleaf_t+0. 0 if invalid.
	int LeafContents(int leafIdx);
	// PVS cluster index at cleaf_t+4 (short). -1 if invalid.
	int LeafCluster(int leafIdx);
	// Area portal grouping (low 9 bits of cleaf_t+6 short). -1 if invalid.
	int LeafArea(int leafIdx);
	// Leaf flags (high 7 bits of cleaf_t+6 short). -1 if invalid.
	int LeafFlags(int leafIdx);
	// First leaf face index in the leaf-face table. -1 if invalid.
	int LeafFirstFace(int leafIdx);
	// Number of faces in this leaf. -1 if invalid.
	int LeafNumFaces(int leafIdx);
	// Leaf AABB via BSP tree-walk. Builds leaf cache lazily on first call.
	bool LeafBounds(int leafIdx, float mins[3], float maxs[3]);

	// Node accessors (manual BSP walking)
	// Splitting plane of node[idx]. false on invalid idx.
	bool NodePlane(int nodeIdx, float normal[3], float &dist);
	// Children of node[idx]. Negative child = leaf via `leafIdx = -1 - child`.
	bool NodeChildren(int nodeIdx, int &leftChild, int &rightChild);
	// Node AABB via BSP tree-walk (same cache as LeafBounds).
	bool NodeBounds(int nodeIdx, float mins[3], float maxs[3]);

	// Plane table access
	bool PlaneAt(int planeIdx, float normal[3], float &dist);
	// PLANE_X(0)/Y(1)/Z(2)/ANYX(3)/ANYY(4)/ANYZ(5). -1 if invalid.
	int PlaneType(int planeIdx);

	// Box brush (cboxbrush_t) accessors. SIMD-optimized axis-aligned brushes.
	bool BoxBrushBounds(int idx, float mins[3], float maxs[3]);
	// Original cbrush_t index this box brush was derived from. -1 if invalid.
	int BoxBrushOriginalBrush(int idx);
	// Per-face surface property indices (-X,+X,-Y,+Y,-Z,+Z). false if invalid.
	bool BoxBrushSurfaceIndex(int idx, int outSurf[6]);
	// Contents via originalBrush lookup. 0 if invalid.
	int BoxBrushContents(int idx);

	// Submodel (cmodel_t) accessors. Used for func_brush, doors, breakables etc.
	bool CModelBounds(int idx, float mins[3], float maxs[3]);
	bool CModelOrigin(int idx, float origin[3]);
	// Root BSP node for this submodel's collision tree. -1 if invalid.
	int CModelHeadnode(int idx);

	// "High-level" pixelsurf.
	// For a sample point and seam Z, search all brushes for:
	//   lowerBrush: maxs.z == seamZ AND XY AABB contains samplePos
	//   upperBrush: mins.z == seamZ AND XY AABB contains samplePos
	// Returns true and fills outLower/outUpper on success.
	bool FindBrushPairAtSeam(const float samplePos[3], float seamZ, int &outLower, int &outUpper);

	// Box-table analog of FindBrushPairAtSeam for CSGO box-optimized walls.
	// Box brushes are absent from the leafbrush lump, and multiple cboxbrush_t entries can share one originalBrush index
	// (collapsing in the cbrush-idx cache), so the regular pair finder misses them. Scans cboxbrush_t directly.
	// Returns BOX-TABLE indices (NOT cbrush indices).
	// Caller applies the contents/order policy.
	bool FindBoxBrushPairAtSeam(const float samplePos[3], float seamZ, int &outLower, int &outUpper);

	// Texturebug overhang.
	// Scans cboxbrush_t for a player-collidable (SOLID or PLAYERCLIP) box brush whose XY AABB contains samplePos,
	// whose underside (mins.z) is open to air, and which has >=1 exposed vertical wall face.
	// Returns the BOX-TABLE index plus the hugged wall (face axis+sign, world coord), underside z, and height.
	// Among exposed lateral faces, picks the one whose outward normal points toward samplePos.
	// Among qualifying boxes in the column, returns the one whose bottom  edge is nearest BELOW samplePos.z
	// (the next edge a falling player crosses). If none below, nearest above. false if none.
	bool FindBoxBrushOverhang(const float samplePos[3], int &outBoxIdx, int &outFace, float &outWallCoord, float &outBottomZ, float &outHeight);

	// FindBoxBrushOverhang + the face-selection velocity window (the "texturebug" condition).
	// Tests whether vel satisfies the topside-misreport window for the overhang at playerPos
	// (cmodel.cpp IntersectRayWithBoxBrush Ineq 7/10):
	//   |v_perp| < (|v_z| * DIST_EPSILON) / (H_brush + hullHeight)
	// where v_perp = vel component along the hugged wall's normal axis.
	//   hullHeight : player hull height (72 standing, 54 ducked in CSGO)
	//   outMaxVPerp: window width, max |v_perp| that still triggers the bug
	//   outVPerp   : actual |v_perp| from vel
	// Geometry outputs mirror FindBoxBrushOverhang.
	// Returns true iff overhang found AND v_z < 0 (falling) AND |v_perp| < maxVPerp.
	bool BoxBrushOverhangWindow(const float playerPos[3], const float vel[3], float hullHeight, int &outBoxIdx, int &outFace, float &outWallCoord,
								float &outBottomZ, float &outHeight, float &outMaxVPerp, float &outVPerp);

	// FindBrushPairAtSeam + leaf-visit-order check.
	// Roverhang at playerPos brushes at the seam,
	// then walks to the leaf just below seamZ and finds each brush's position in that leaf's brush list.
	// CSGO inverts leaf-visit order vs TF2, so the lower brush listed first (lowerPos < upperPos) is the pixelsurf-eligible ordering.
	//   outLeaf    : leaf sampled below the seam (-1 if lookup failed)
	//   outLowerPos/outUpperPos: index of lower/upper brush in that leaf's list (-1 if not present)
	// Returns true if both found AND both in the leaf AND lowerPos < upperPos.
	bool LeafBrushPairAtSeam(const float samplePos[3], float seamZ, int &outLower, int &outUpper, int &outLeaf, int &outLowerPos, int &outUpperPos);

	// Brush AABB cache
	// Call once in OnMapStart so the first query doesn't pay the build cost.
	// Prefer async to avoid stalling map load.
	void RebuildCache();

	// Builds on worker thread, returns immediately. Reads block until swap-in.
	// Calling while a build is in flight is a no-op.
	void RebuildCacheAsync();

	// True between RebuildCacheAsync() and the worker's swap-in.
	bool CacheIsBuilding();

} // namespace BSPData
#endif
