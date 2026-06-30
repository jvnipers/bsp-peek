#ifndef _INCLUDE_BSPPEEK_BSP_LUMPS_H_
#define _INCLUDE_BSPPEEK_BSP_LUMPS_H_

#include "smsdk_ext.h"
#include <cstdint>

// BSP file lump readers. Parses on map load, exposes via accessors.
// Source: BSP file format (LUMP_*).
namespace BSPLumps
{

	// Lifecycle
	void Shutdown();
	// Reloads all lumps from given .bsp path. Idempotent per mapname.
	bool LoadFromMap(const char *mapname, const char *bspPath, char *err, size_t errLen);
	void Clear();
	bool Loaded();

	// BSP header
	int BSPVersion();
	int BSPRevision(); // map revision (vbsp -version)
	// Returns lump file-offset/length/version into out vars. lumpId in [0, 64).
	bool LumpInfo(int lumpId, int &outOffset, int &outLength, int &outVersion);

	// True if the map has baked lighting (LUMP_LIGHTING or LUMP_LIGHTING_HDR filelen > 0).
	// Engine forces mat_fullbright=1 when both are empty.
	bool HasLighting();

	// Entities (LUMP_ENTITIES = 0, raw text)
	int EntityRawLen();
	int EntityRawCopy(char *buf, int maxlen); // returns bytes copied (excl. null)
	int EntityCount();
	int EntityClassname(int idx, char *buf, int maxlen);
	bool EntityOrigin(int idx, float out[3]); // false if "origin" missing
	int EntityKeyValue(int idx, const char *key, char *buf, int maxlen);
	// Brush-model index for entity[idx]:
	// parses its "model" key, and if it is a "*N" brush-model reference returns N (the cmodel/submodel index usable with BSPData::CModel*).
	// Returns -1 if the entity has no "model" key, the value is a studio/.mdl path rather than a "*N" reference, or idx is OOB.
	int EntityModelIndex(int idx);

	// Texinfo (LUMP_TEXINFO = 18) + Texdata (LUMP_TEXDATA = 2)
	int TexInfoCount();
	int TexInfoFlags(int idx);   // SURF_* flags
	int TexInfoTexData(int idx); // texdata index, -1 if invalid
	int TexDataCount();
	int TexDataMaterialName(int texdataIdx, char *buf, int maxlen);
	bool TexDataReflectivity(int texdataIdx, float out[3]);

	// Visibility (LUMP_VISIBILITY = 4)
	// Number of PVS clusters in this map (= numclusters in the vis header).
	int VisClusterCount();
	// PVS test: is cluster 'other' visible from cluster 'cluster'?
	// Decompresses RLE on-the-fly. false on invalid input or no vis data.
	bool ClusterVisible(int cluster, int other);

	// Leaf water data (LUMP_LEAFWATERDATA = 36, dleafwaterdata_t) One entry per distinct body of water.
	// Gives the water surface plane.
	int LeafWaterCount();
	// surfaceZ = water surface height, minZ = bottom, surfaceTexInfo = texinfo idx. false if OOB.
	bool LeafWaterData(int idx, float &surfaceZ, float &minZ, int &surfaceTexInfo);
	// Z-band heuristic: surface Z of the water body whose [minZ,surfaceZ] contains pos.z (lowest qualifying surface).
	// Returns -1e30 if pos is in no water band.
	float WaterSurfaceZAt(const float pos[3]);

	// Cubemaps (LUMP_CUBEMAPS = 42, dcubemapsample_t = 16B)
	int CubemapCount();
	// World-space origin (stored as int[3] in BSP, returned as float[3]). false if OOB.
	bool CubemapOrigin(int idx, float out[3]);
	// Resolution power: actual size = 2^size. 0 = default. -1 if OOB.
	int CubemapSize(int idx);

	// Edges (LUMP_EDGES = 12, dedge_t = 4B: uint16 v[2])
	int EdgeCount();
	// Fills v0 and v1 with the two vertex indices. Returns false if OOB.
	bool EdgeVertices(int idx, int &v0, int &v1);

	// Surfedges (LUMP_SURFEDGES = 13, int32 array)
	// Signed: positive = edge forward (v[0]→v[1]), negative = edge backward.
	int SurfedgeCount();
	int Surfedge(int idx); // raw signed value; 0 if OOB
	// Resolves surfedge sign to the "start" vertex of that directed edge.
	// Use this to enumerate face vertices: loop FaceFirstEdge..+FaceNumEdges.
	int SurfedgeVertex(int idx); // vertex index; -1 if OOB

	// Vertexes (LUMP_VERTEXES = 3, Vector[3] per entry)
	int VertexCount();
	bool VertexPos(int idx, float out[3]);

	// compound queries

	// Face vertex at polygon slot [0, FaceNumEdges(faceIdx)).
	// Resolves FirstEdge -> SurfedgeVertex→VertexPos in one call. false if OOB.
	bool FaceVertex(int faceIdx, int slot, float out[3]);
	// Average position of all face vertices. false if face invalid or has no edges.
	bool FaceCentroid(int faceIdx, float out[3]);
	// Material name via FaceTexInfo→TexInfoTexData→TexDataMaterialName chain.
	// Returns string length. 0 if face has no texinfo (skip/nodraw/toolface).
	int FaceMaterialName(int faceIdx, char *buf, int maxlen);
	// Index of cubemap with closest origin to pos. -1 if no cubemaps.
	int NearestCubemap(const float pos[3]);
	// Find first entity at or after startIdx where kv[key]==value. -1 if none.
	int FindEntityByKeyValue(const char *key, const char *value, int startIdx);

	// Faces (LUMP_FACES = 7, dface_t = 56 bytes)
	int FaceCount();
	int FacePlaneNum(int idx);  // uint16 plane index; -1 if OOB
	int FaceFirstEdge(int idx); // first surfedge table index
	int FaceNumEdges(int idx);  // surfedge count for this face
	int FaceTexInfo(int idx);   // texinfo index (-1 = skip face)
	int FaceDispInfo(int idx);  // dispinfo index; -1 if not a displacement
	float FaceArea(int idx);    // precomputed face area
	// 4 lightstyle indices. 255 = unused
	bool FaceLightStyles(int idx, uint8_t out[4]);
	int FaceOrigFace(int idx); // original (pre-split) face; -1 if top-level
	int FaceLightOfs(int idx); // byte offset into lighting lump; -1 = no samples

	// LeafFaces (LUMP_LEAFFACES = 16, uint16 array)
	int LeafFacesCount();
	// Read leaf-face indices for leaf at firstFace..firstFace+numFaces from BSPData.
	// Returns count written (clamped to maxOut).
	// Uses caller-supplied first/num so this is a pure lookup against the leaffaces table.
	int LeafFacesRange(int firstFace, int numFaces, int *outBuf, int maxOut);

	// Worldlights (LUMP_WORLDLIGHTS = 15 LDR, LUMP_WORLDLIGHTS_HDR = 54)
	int WorldlightCount(); // total across both LDR + HDR if present (LDR first)
	// Fills basic fields. Returns false if invalid.
	bool WorldlightOrigin(int idx, float out[3]);
	bool WorldlightIntensity(int idx, float out[3]);
	bool WorldlightNormal(int idx, float out[3]);
	int WorldlightType(int idx);  // emit_t: 0=surface 1=point 2=spot 3=skylight
								  // 4=quake 5=skyambient
	int WorldlightStyle(int idx); // light style index
	int WorldlightCluster(int idx);
	// Extended fields (per dworldlight_t in CSGO, offsets beyond cluster+style)
	bool WorldlightShadowCastOffset(int idx, float out[3]); // +36
	float WorldlightStopDot(int idx);                       // +60 (spotlight cone inner)
	float WorldlightStopDot2(int idx);                      // +64 (spotlight cone outer)
	float WorldlightExponent(int idx);                      // +68
	float WorldlightRadius(int idx);                        // +72
	float WorldlightConstantAttn(int idx);                  // +76
	float WorldlightLinearAttn(int idx);                    // +80
	float WorldlightQuadraticAttn(int idx);                 // +84
	int WorldlightFlags(int idx);                           // +88
	int WorldlightTexInfo(int idx);                         // +92
	int WorldlightOwner(int idx);                           // +96

	// Static props (sprp game lump in LUMP_GAME_LUMP = 35)
	// Parsed at load from the version-stable StaticPropLump_t prefix.
	int StaticPropCount();
	int StaticPropVersion(); // sprp game-lump version (10/11/... ), 0 if none
	bool StaticPropOrigin(int idx, float out[3]);
	bool StaticPropAngles(int idx, float out[3]);            // pitch/yaw/roll
	int StaticPropSolid(int idx);                            // SOLID_* (6=VPHYSICS), -1 if OOB
	int StaticPropFlags(int idx);                            // STATIC_PROP_FLAG_*, -1 if OOB
	int StaticPropModelName(int idx, char *buf, int maxlen); // returns length
	// Fields past the version-stable prefix.
	// 0 / false when the lump version is too old to contain them.
	int StaticPropSkin(int idx);
	bool StaticPropFadeDist(int idx, float &outMin, float &outMax);
	float StaticPropForcedFadeScale(int idx);
	bool StaticPropLightingOrigin(int idx, float out[3]);
	int StaticPropFlagsEx(int idx);
	// BSP leaf indices the prop touches. Returns count written (clamped to maxOut).
	int StaticPropLeaves(int idx, int *outBuf, int maxOut);
	// Nearest prop by origin within maxDist (<=0 = unlimited). -1 if none.
	int NearestStaticProp(const float pos[3], float maxDist);

} // namespace BSPLumps

#endif
