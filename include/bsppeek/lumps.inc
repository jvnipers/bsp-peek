#if defined _bsppeek_lumps_included
    #endinput
#endif
#define _bsppeek_lumps_included

// BSP file lumps read from disk: entities, texinfo, faces, lights, static prop placements, water, brush entities.

// BSP file lump readers
// Disk-parsed (not live engine memory). Loaded at map start, cached per-map.

// BSP file header
native int   BSP_BSPVersion();
native int   BSP_BSPRevision();    // map revision (vbsp -revision)
// Returns lump fileofs/length/version into out vars. lumpId in [0, 64).
native bool  BSP_LumpInfo(int lumpId, int &fileofs, int &length, int &version);

// True if the map has baked lighting (LUMP_LIGHTING or LUMP_LIGHTING_HDR filelen > 0).
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
// Lookup key (case-insensitive) on entity[idx]. Returns value length, 0 if missing.
// Keys can repeat (outputs): nth picks among them in file order.
native int   BSP_EntityKeyValue(int idx, const char[] key, char[] buf, int maxlen, int nth = 0);
// Brush-model index for entity[idx]: if its "model" key is "*N", returns N (usable as the cmodel/submodel index for BSP_CModel*).
// Ties a moving brush ent (func_door, func_brush, triggers) to its collision submodel.
// -1 if no "model" key, a studio (.mdl) model, or idx out of range.
native int   BSP_EntityModelIndex(int idx);

// Texinfo / Texdata (LUMP_TEXINFO + LUMP_TEXDATA)
native int   BSP_TexInfoCount();
native int   BSP_TexInfoFlags(int idx);      // SURF_* flags
native int   BSP_TexInfoTexData(int idx);    // texdata index, -1 if invalid
native int   BSP_TexDataCount();
// Material name (e.g. "concrete/concrete_01"). Returns length.
native int   BSP_TexDataMaterialName(int texdataIdx, char[] buf, int maxlen);
native bool  BSP_TexDataReflectivity(int texdataIdx, float refl[3]);

// Convenience / compound queries

// Entity search: returns first entity index >= startIdx with a value under key equal to value.
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
// World-space origin (stored as int[3] in BSP, returned as float[3]). false if OOB.
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
// Positive = edge forward (v[0] -> v[1]), negative = edge backward.
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
// Note: BSP faces are the renderer's split faces. Use BSP_FaceOrigFace to map back to the original editor face.
// Displacement faces have FaceDispInfo >= 0.
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
// Original (pre-split) face index. -1 if this face was never split.
native int   BSP_FaceOrigFace(int idx);
// Byte offset into the lighting lump for this face's lightmap samples. -1 if none.
native int   BSP_FaceLightOfs(int idx);

// LeafFaces (LUMP_LEAFFACES, uint16 face index table)
// Total leaf-face entries (sum across all leaves).
native int   BSP_LeafFacesCount();
// This leaf's face indices, from the on-disk leaf lump. leafIdx as from BSP_LeafAtPoint. Returns count written.
native int   BSP_LeafFaces(int leafIdx, int[] buf, int max);

// Worldlights (LDR preferred, HDR fallback)
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
native int   BSP_WorldlightTexInfo(int idx);          // associated texinfo index for surface lights (0 otherwise), -1 if OOB
native int   BSP_WorldlightOwner(int idx);            // entity index owning this light (0 otherwise), -1 if OOB

// Static props (sprp game lump in LUMP_GAME_LUMP)
// Parsed at map load from the BSP file's version-stable StaticPropLump_t prefix.
// NOTE: this is the prop PLACEMENT (origin/angles/model), NOT its collision mesh.
// Static props collide via VPhysics (the model's .phy), which is not in the BSP.

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
// Fields past the version-stable prefix. 0 / false when the sprp version predates the field
// (skin/fades v4, lighting origin v4-6, forced fade scale v5, flagsEx v10).
native int   BSP_StaticPropSkin(int idx);
native bool  BSP_StaticPropFadeDist(int idx, float &fadeMin, float &fadeMax);
native float BSP_StaticPropForcedFadeScale(int idx);
native bool  BSP_StaticPropLightingOrigin(int idx, float origin[3]);
native int   BSP_StaticPropFlagsEx(int idx);    // STATIC_PROP_FLAGS_EX_* bitmask
// BSP leaf indices this prop touches. Returns count written (clamped to max).
native int   BSP_StaticPropLeaves(int idx, int[] buf, int max);
// Index of the prop whose origin is nearest pos within maxDist (<=0 = unlimited). -1 if none.
native int   BSP_NearestStaticProp(const float pos[3], float maxDist = 0.0);

// Leaf water data (LUMP_LEAFWATERDATA)
// One entry per body of water, gives its surface plane.
// Pair with BSP_PointContents & BSPP_CONTENTS_WATER to know when you're submerged, then this for "how high to surface".
#define BSP_WATER_NO_SURFACE -1.0e30

native int   BSP_LeafWaterCount();
// surfaceZ = water top, minZ = bottom, surfaceTexInfo = texinfo index. false if OOB.
native bool  BSP_LeafWaterData(int idx, float &surfaceZ, float &minZ, int &surfaceTexInfo);
// Z-band heuristic: surface Z of the water body whose [minZ,surfaceZ] contains pos.z (lowest qualifying).
// BSP_WATER_NO_SURFACE if none. No XY test (the lump has no XY bounds), so it can false-match separate pools at the same height.
// Confirm with BSP_PointContents when precision matters.
native float BSP_WaterSurfaceZAt(const float pos[3]);

// Brush-entity / trigger / ladder helpers
// Convenience over the entity + cmodel accessors for the common routing cases (trigger volumes, movers, ladders).

// World AABB of a brush entity (any "model" == "*N" ent: triggers, func_door, func_brush, func_ladder...).
// Chains EntityModelIndex -> CModelBounds + origin (engine padding removed, "angles" not applied).
// false if entIdx isn't a brush-model entity.
native bool  BSP_EntityBrushBounds(int entIdx, float mins[3], float maxs[3]);
// Applied push velocity of a trigger_push: AngleVectors(pushdir) * speed (a missing or 0 speed is 100, as in the engine).
// Returns false (and zero vel) if entIdx isn't a trigger_push.
native bool  BSP_TriggerPushVelocity(int entIdx, float vel[3]);
// Origin of the entity referenced by entIdx's "target" key (e.g. a trigger_teleport's info_teleport_destination).
// Targetname matched like the engine: case-insensitive, trailing '*' wildcard.
// false if no target / not found.
native bool  BSP_EntityTargetOrigin(int entIdx, float origin[3]);
// Enumerate world brushes with CONTENTS_LADDER:
// returns the next brush index >= startIdx that is a ladder volume, or -1 when none remain.
// Call again with result+1 to iterate.
// (func_ladder entities: use BSP_FindEntityByKeyValue + BSP_EntityBrushBounds instead.)
native int   BSP_FindLadderBrush(int startIdx = 0);
