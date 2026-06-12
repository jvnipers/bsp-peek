// bsppeek_test - minimal consumer plugin exercising every bsppeek.ext native.
//
// Build: copy include/bsppeek.inc into your sourcemod/scripting/include, then:
//        spcomp bsppeek_test.sp
//
// Usage (in-game / server console):
//   sm_bsp            - run the whole suite at your aim/origin (everything)
//   sm_bsp_info       - selftest, engine build, counts, lumps
//   sm_bsp_point      - point/leaf/brush queries at your position
//   sm_bsp_brush <i>  - dump brush i (sides, planes, geometry, clip)
//   sm_bsp_disp       - displacement queries at your position
//   sm_bsp_props      - static prop (lump + runtime) queries
//   sm_bsp_trace      - unified hull trace along your aim
//   sm_bsp_ents       - entity lump dump
//   sm_bsp_surf       - surfaceprop database
//
// Pure smoke test:
// every native is called at least once so you can confirm the ext loaded and signatures resolved.

#include <sourcemod>
#include <sdktools>
#include <bsppeek>

#pragma semicolon 1
#pragma newdecls required

public Plugin myinfo =
{
    name        = "BSP-Peek Test plugin",
    author      = "jvnipers",
    description = "Exercises every bsppeek native",
    version     = "1.0",
    url         = "https://github.com/jvnipers/bsp-peek"
};

// All output goes to the calling client's console (or server console).
int  g_Target;    // client running the command (0 = server console)

void P(const char[] fmt, any...)
{
    char buf[512];
    VFormat(buf, sizeof buf, fmt, 2);
    if (g_Target > 0)
        PrintToConsole(g_Target, "%s", buf);
    else
        PrintToServer("%s", buf);
}

void Hdr(const char[] title)
{
    P("==================== %s ====================", title);
}

// Resolve the position to query: aim end-point if a player, else map origin.
bool QueryPos(float pos[3])
{
    if (g_Target > 0 && IsClientInGame(g_Target))
    {
        float eye[3], ang[3];
        GetClientEyePosition(g_Target, eye);
        GetClientEyeAngles(g_Target, ang);
        float fwd[3];
        GetAngleVectors(ang, fwd, NULL_VECTOR, NULL_VECTOR);
        ScaleVector(fwd, 8192.0);
        AddVectors(eye, fwd, pos);

        // Trace along aim to land on a real surface, fall back to the aim endpoint.
        Handle tr = TR_TraceRayFilterEx(eye, ang, MASK_SOLID, RayType_Infinite, TraceFilter_World);
        if (TR_DidHit(tr))
            TR_GetEndPosition(pos, tr);
        delete tr;
        return true;
    }
    pos = view_as<float>({ 0.0, 0.0, 0.0 });
    return false;
}

public bool TraceFilter_World(int entity, int mask)
{
    return entity == 0;
}

public void OnPluginStart()
{
    RegConsoleCmd("sm_bsp", Cmd_All);
    RegConsoleCmd("sm_bsp_info", Cmd_Info);
    RegConsoleCmd("sm_bsp_point", Cmd_Point);
    RegConsoleCmd("sm_bsp_brush", Cmd_Brush);
    RegConsoleCmd("sm_bsp_disp", Cmd_Disp);
    RegConsoleCmd("sm_bsp_props", Cmd_Props);
    RegConsoleCmd("sm_bsp_trace", Cmd_Trace);
    RegConsoleCmd("sm_bsp_ents", Cmd_Ents);
    RegConsoleCmd("sm_bsp_surf", Cmd_Surf);
    // UNSAFE (bad ptrOff may crash) -> kept out of the run-all suite.
    RegConsoleCmd("sm_bsp_debugptr", Cmd_DebugPtr);
}

// sm_bsp_debugptr <ptrOff> <bytes> - read pointer at CBSPData+ptrOff, dump bytes.
public Action Cmd_DebugPtr(int client, int args)
{
    g_Target = client;
    if (!Avail()) return Plugin_Handled;
    char a[16], b[16];
    GetCmdArg(1, a, sizeof a);
    GetCmdArg(2, b, sizeof b);
    int off   = StringToInt(a);
    int bytes = args >= 2 ? StringToInt(b) : 32;
    BSP_DebugDumpCBSPPtr(off, bytes);
    P("DebugDumpCBSPPtr(%d,%d): dumped to SourceMod log", off, bytes);
    return Plugin_Handled;
}

public void OnMapStart()
{
    BSP_RebuildCacheAsync();
}

public Action Cmd_All(int client, int args)
{
    g_Target = client;
    if (!Avail()) return Plugin_Handled;
    Section_Info();
    Section_Point();
    Section_Brush(0);
    Section_Disp();
    Section_Props();
    Section_Trace();
    Section_Ents();
    Section_Surf();
    P("[bsp] full suite done.");
    return Plugin_Handled;
}

public Action Cmd_Info(int client, int args)
{
    g_Target = client;
    if (Avail()) Section_Info();
    return Plugin_Handled;
}

public Action Cmd_Point(int client, int args)
{
    g_Target = client;
    if (Avail()) Section_Point();
    return Plugin_Handled;
}

public Action Cmd_Disp(int client, int args)
{
    g_Target = client;
    if (Avail()) Section_Disp();
    return Plugin_Handled;
}

public Action Cmd_Props(int client, int args)
{
    g_Target = client;
    if (Avail()) Section_Props();
    return Plugin_Handled;
}

public Action Cmd_Trace(int client, int args)
{
    g_Target = client;
    if (Avail()) Section_Trace();
    return Plugin_Handled;
}

public Action Cmd_Ents(int client, int args)
{
    g_Target = client;
    if (Avail()) Section_Ents();
    return Plugin_Handled;
}

public Action Cmd_Surf(int client, int args)
{
    g_Target = client;
    if (Avail()) Section_Surf();
    return Plugin_Handled;
}

public Action Cmd_Brush(int client, int args)
{
    g_Target = client;
    if (!Avail()) return Plugin_Handled;
    char a[16];
    GetCmdArg(1, a, sizeof a);
    Section_Brush(StringToInt(a));
    return Plugin_Handled;
}

bool Avail()
{
    if (!BSPPeek_Available())
    {
        P("[bsp] ext NOT loaded (LibraryExists(\"bsppeek\")==false). Aborting.");
        return false;
    }
    return true;
}

void Section_Info()
{
    Hdr("INFO / HEALTH");
    char buf[256];

    BSP_MapPathName(buf, sizeof buf);
    P("MapPathName        : %s", buf);

    int st = BSP_SelfTest();
    P("SelfTest           : 0x%02X  [BSPData=%d leaf/node=%d boxbrush=%d vis=%d dispRdr=%d props=%d disk=%d]",
      st, !!(st & 0x01), !!(st & 0x02), !!(st & 0x04), !!(st & 0x08), !!(st & 0x10), !!(st & 0x20), !!(st & 0x40));

    int ver = BSP_EngineBuild(buf, sizeof buf);
    P("EngineBuild        : %d  (%s)", ver, buf);

    Hdr("COUNTS");
    P("Brushes=%d BrushSides=%d Leaves=%d Nodes=%d Planes=%d",
      BSP_NumBrushes(), BSP_NumBrushSides(), BSP_NumLeaves(), BSP_NumNodes(), BSP_NumPlanes());
    P("BoxBrushes=%d CModels=%d Areas=%d AreaPortals=%d Clusters=%d",
      BSP_NumBoxBrushes(), BSP_NumCModels(), BSP_NumAreas(), BSP_NumAreaPortals(), BSP_NumClusters());
    P("EmptyLeaf=%d SolidLeaf=%d", BSP_EmptyLeaf(), BSP_SolidLeaf());

    // Areas / area portals
    int nP, fP;
    if (BSP_AreaInfo(0, nP, fP))
        P("AreaInfo[0]        : numPortals=%d firstPortal=%d", nP, fP);
    int pk, oa, fcv, cv, pn;
    if (BSP_AreaPortalInfo(0, pk, oa, fcv, cv, pn))
        P("AreaPortalInfo[0]  : key=%d otherArea=%d firstClipVert=%d clipVerts=%d planenum=%d", pk, oa, fcv, cv, pn);

    // Visibility
    P("Clusters Vis(0,0)  : %d   ClustersVisible(0,1)=%d", BSP_ClustersVisible(0, 0), BSP_ClustersVisible(0, 1));
    P("LeavesVisible(0,0) : %d", BSP_LeavesVisible(0, 0));
    P("VisClusterCount    : %d   ClusterVisible(0,0)=%d LeafVisible(0,0)=%d",
      BSP_VisClusterCount(), BSP_ClusterVisible(0, 0), BSP_LeafVisible(0, 0));
    int vrow[64];
    P("VisRowDecompress(0): %d bytes", BSP_VisRowDecompress(0, vrow, sizeof vrow));

    // Planes
    float n[3], d;
    if (BSP_PlaneAt(0, n, d))
        P("PlaneAt[0]         : (%.2f %.2f %.2f) d=%.2f type=%d", n[0], n[1], n[2], d, BSP_PlaneType(0));

    // Cache (synchronous rebuild, blocks the server briefly)
    P("RebuildCache       : %d brushes", BSP_RebuildCache());
    P("CacheIsBuilding    : %d", BSP_CacheIsBuilding());

    // Debug hex-dump of CCollisionBSPData head (range clamped server-side -> log)
    BSP_DebugDumpCBSP(0, 64);
    P("DebugDumpCBSP(0,64): dumped to SourceMod log");

    Hdr("BSP FILE LUMPS");
    P("BSPVersion=%d Revision=%d HasLighting=%d", BSP_BSPVersion(), BSP_BSPRevision(), BSP_HasLighting());
    int ofs, len, lv;
    if (BSP_LumpInfo(0, ofs, len, lv))
        P("LumpInfo[0]        : ofs=%d len=%d ver=%d", ofs, len, lv);
    P("Counts: Verts=%d Edges=%d Surfedges=%d Faces=%d LeafFaces=%d",
      BSP_VertexCount(), BSP_EdgeCount(), BSP_SurfedgeCount(), BSP_FaceCount(), BSP_LeafFacesCount());
    P("Counts: TexInfo=%d TexData=%d Cubemaps=%d Worldlights=%d LeafWater=%d",
      BSP_TexInfoCount(), BSP_TexDataCount(), BSP_CubemapCount(), BSP_WorldlightCount(), BSP_LeafWaterCount());

    // Vertex / edge / surfedge
    float vp[3];
    if (BSP_VertexCount() > 0 && BSP_VertexPos(0, vp))
        P("VertexPos[0]       : (%.2f %.2f %.2f)", vp[0], vp[1], vp[2]);
    int e0, e1;
    if (BSP_EdgeCount() > 0 && BSP_EdgeVertices(0, e0, e1))
        P("EdgeVertices[0]    : v0=%d v1=%d", e0, e1);
    if (BSP_SurfedgeCount() > 0)
        P("Surfedge[0]=%d  SurfedgeVertex[0]=%d", BSP_Surfedge(0), BSP_SurfedgeVertex(0));

    // Face
    if (BSP_FaceCount() > 0)
    {
        P("Face[0]            : plane=%d firstEdge=%d numEdges=%d texinfo=%d disp=%d area=%.1f origFace=%d lightOfs=%d",
          BSP_FacePlaneNum(0), BSP_FaceFirstEdge(0), BSP_FaceNumEdges(0), BSP_FaceTexInfo(0),
          BSP_FaceDispInfo(0), BSP_FaceArea(0), BSP_FaceOrigFace(0), BSP_FaceLightOfs(0));
        int styles[4];
        BSP_FaceLightStyles(0, styles);
        P("Face[0] lightStyles: %d %d %d %d", styles[0], styles[1], styles[2], styles[3]);
        float fc[3];
        if (BSP_FaceCentroid(0, fc))
            P("Face[0] centroid   : (%.1f %.1f %.1f)", fc[0], fc[1], fc[2]);
        float fv[3];
        if (BSP_FaceVertex(0, 0, fv))
            P("Face[0] vert0      : (%.1f %.1f %.1f)", fv[0], fv[1], fv[2]);
        char mat[128];
        BSP_FaceMaterialName(0, mat, sizeof mat);
        P("Face[0] material   : %s", mat);
    }

    // TexInfo / TexData
    if (BSP_TexInfoCount() > 0)
        P("TexInfo[0]         : flags=0x%X texdata=%d", BSP_TexInfoFlags(0), BSP_TexInfoTexData(0));
    if (BSP_TexDataCount() > 0)
    {
        char mat[128];
        BSP_TexDataMaterialName(0, mat, sizeof mat);
        float refl[3];
        BSP_TexDataReflectivity(0, refl);
        P("TexData[0]         : %s  refl=(%.2f %.2f %.2f)", mat, refl[0], refl[1], refl[2]);
    }

    // Cubemaps
    if (BSP_CubemapCount() > 0)
    {
        float co[3];
        BSP_CubemapOrigin(0, co);
        P("Cubemap[0]         : (%.0f %.0f %.0f) size=%d", co[0], co[1], co[2], BSP_CubemapSize(0));
    }

    // Worldlights
    if (BSP_WorldlightCount() > 0)
    {
        float o[3], inten[3], wn[3], off[3];
        BSP_WorldlightOrigin(0, o);
        BSP_WorldlightIntensity(0, inten);
        BSP_WorldlightNormal(0, wn);
        BSP_WorldlightShadowCastOffset(0, off);
        P("Worldlight[0]      : org=(%.0f %.0f %.0f) type=%d style=%d cluster=%d flags=0x%X texinfo=%d owner=%d",
          o[0], o[1], o[2], BSP_WorldlightType(0), BSP_WorldlightStyle(0), BSP_WorldlightCluster(0),
          BSP_WorldlightFlags(0), BSP_WorldlightTexInfo(0), BSP_WorldlightOwner(0));
        P("Worldlight[0] phys : stopDot=%.3f stopDot2=%.3f exp=%.2f radius=%.1f attn(c/l/q)=%.3f/%.3f/%.5f",
          BSP_WorldlightStopDot(0), BSP_WorldlightStopDot2(0), BSP_WorldlightExponent(0),
          BSP_WorldlightRadius(0), BSP_WorldlightConstantAttn(0), BSP_WorldlightLinearAttn(0),
          BSP_WorldlightQuadraticAttn(0));
    }

    // Leaf water
    if (BSP_LeafWaterCount() > 0)
    {
        float sz, mz;
        int   sti;
        if (BSP_LeafWaterData(0, sz, mz, sti))
            P("LeafWater[0]       : surfaceZ=%.1f minZ=%.1f texinfo=%d", sz, mz, sti);
    }
}

void Section_Point()
{
    Hdr("POINT / LEAF QUERIES");
    float pos[3];
    QueryPos(pos);
    P("Query pos          : (%.1f %.1f %.1f)", pos[0], pos[1], pos[2]);

    int leaf = BSP_LeafAtPoint(pos);
    P("LeafAtPoint        : %d", leaf);
    P("PointContents      : 0x%X", BSP_PointContents(pos));
    P("PointContentsBrush : 0x%X", BSP_PointContentsBrushes(pos));
    P("IsLadder           : %d", BSP_IsLadder(pos));

    if (leaf >= 0)
    {
        P("Leaf %d            : contents=0x%X cluster=%d area=%d flags=0x%X firstFace=%d numFaces=%d",
          leaf, BSP_LeafContents(leaf), BSP_LeafCluster(leaf), BSP_LeafArea(leaf),
          BSP_LeafFlags(leaf), BSP_LeafFirstFace(leaf), BSP_LeafNumFaces(leaf));
        float lmin[3], lmax[3];
        if (BSP_LeafBounds(leaf, lmin, lmax))
            P("Leaf bounds        : (%.0f %.0f %.0f)-(%.0f %.0f %.0f)", lmin[0], lmin[1], lmin[2], lmax[0], lmax[1], lmax[2]);

        int lb[64];
        int nb = BSP_LeafBrushes(leaf, lb, sizeof lb);
        P("LeafBrushes        : %d  [first=%s]", nb, nb > 0 ? "see below" : "none");
        if (nb > 0)
            Section_Brush(lb[0]);

        int lf[64];
        int nf = BSP_LeafFaces(leaf, lf, sizeof lf);
        P("LeafFaces          : %d", nf);
    }

    // Node walk (root)
    if (BSP_NumNodes() > 0)
    {
        float n[3], d;
        int   lc, rc;
        float nmin[3], nmax[3];
        BSP_NodePlane(0, n, d);
        BSP_NodeChildren(0, lc, rc);
        BSP_NodeBounds(0, nmin, nmax);
        P("Node[0]            : plane(%.2f %.2f %.2f) d=%.1f  children L=%d R=%d", n[0], n[1], n[2], d, lc, rc);
    }

    // CModel 0 (world)
    float cmin[3], cmax[3], corg[3];
    if (BSP_CModelBounds(0, cmin, cmax))
    {
        BSP_CModelOrigin(0, corg);
        P("CModel[0] (world)  : (%.0f %.0f %.0f)-(%.0f %.0f %.0f) head=%d",
          cmin[0], cmin[1], cmin[2], cmax[0], cmax[1], cmax[2], BSP_CModelHeadnode(0));
    }

    // Box brush 0
    if (BSP_NumBoxBrushes() > 0)
    {
        float bmin[3], bmax[3];
        int   surf[6];
        BSP_BoxBrushBounds(0, bmin, bmax);
        BSP_BoxBrushSurfaceIndex(0, surf);
        P("BoxBrush[0]        : (%.0f %.0f %.0f)-(%.0f %.0f %.0f) orig=%d contents=0x%X surf={%d,%d,%d,%d,%d,%d}",
          bmin[0], bmin[1], bmin[2], bmax[0], bmax[1], bmax[2],
          BSP_BoxBrushOriginalBrush(0), BSP_BoxBrushContents(0),
          surf[0], surf[1], surf[2], surf[3], surf[4], surf[5]);
    }

    // Seam finders at the query point (seamZ = pos.z)
    int lo, hi, sleaf, lpos, upos, blo, bhi;
    P("FindBrushPairAtSeam   : %d", BSP_FindBrushPairAtSeam(pos, pos[2], lo, hi));
    P("FindBoxBrushPairAtSeam: %d", BSP_FindBoxBrushPairAtSeam(pos, pos[2], blo, bhi));
    int   obox, oface;
    float ocoord, obz, oh;
    P("FindBoxBrushOverhang  : %d (box=%d face=%d wall=%.2f bottomZ=%.2f H=%.2f)",
      BSP_FindBoxBrushOverhang(pos, obox, oface, ocoord, obz, oh),
      obox, oface, ocoord, obz, oh);
    float tvel[3];
    tvel[2] = -6.25;    // apex-tick vz @64t (StartGravity half-step)
    float twin, tvp;
    P("BoxBrushOverhangWindow: %d (box=%d face=%d wall=%.2f bottomZ=%.2f H=%.2f win=%.5f vperp=%.5f)",
      BSP_BoxBrushOverhangWindow(pos, tvel, BSP_CSGO_HULL_STAND, obox, oface, ocoord, obz, oh, twin, tvp),
      obox, oface, ocoord, obz, oh, twin, tvp);
    P("LeafBrushPairAtSeam   : %d (leaf=%d lpos=%d upos=%d)",
      BSP_LeafBrushPairAtSeam(pos, pos[2], lo, hi, sleaf, lpos, upos), sleaf, lpos, upos);

    // Nearest cubemap / static prop / ladder
    P("NearestCubemap     : %d", BSP_NearestCubemap(pos));
    P("NearestStaticProp  : %d", BSP_NearestStaticProp(pos));
    P("FindLadderBrush(0) : %d", BSP_FindLadderBrush(0));
    P("WaterSurfaceZAt    : %.1f", BSP_WaterSurfaceZAt(pos));
    P("FindEntityByKV     : worldspawn idx=%d", BSP_FindEntityByKeyValue("classname", "worldspawn"));
}

void Section_Brush(int b)
{
    Hdr("BRUSH");
    int nb = BSP_NumBrushes();
    if (nb <= 0)
    {
        P("no brushes");
        return;
    }
    if (b < 0 || b >= nb) b = 0;

    float mins[3], maxs[3];
    BSP_BrushBounds(b, mins, maxs);
    int sides = BSP_BrushNumSides(b);
    P("Brush %d           : contents=0x%X sides=%d isBox(heur)=%d isBox(auth)=%d",
      b, BSP_BrushContents(b), sides, BSP_IsBoxBrush(b), BSP_BrushIsBoxAuth(b));
    P("Bounds             : (%.1f %.1f %.1f)-(%.1f %.1f %.1f)", mins[0], mins[1], mins[2], maxs[0], maxs[1], maxs[2]);

    // Side 0 details
    if (sides > 0)
    {
        float n[3], d;
        if (BSP_BrushSidePlane(b, 0, n, d))
            P("Side0 plane        : (%.2f %.2f %.2f) d=%.2f bevel=%d thin=%d texinfo=%d planeIdx=%d",
              n[0], n[1], n[2], d, BSP_BrushSideBevel(b, 0), BSP_BrushSideThin(b, 0),
              BSP_BrushSideTexInfo(b, 0), BSP_BrushSidePlaneIndex(b, 0));
        char mat[128];
        BSP_BrushSideMaterial(b, 0, mat, sizeof mat);
        P("Side0 material     : %s", mat);

        float verts[32][3];
        int   nv = BSP_BrushSideWinding(b, 0, verts, sizeof verts);
        P("Side0 winding      : %d verts%s", nv, nv > 0 ? "" : " (box-optimized? use BoxBrushBounds)");
    }

    // Geometry / collision
    float c[3];
    c[0] = (mins[0] + maxs[0]) * 0.5;
    c[1] = (mins[1] + maxs[1]) * 0.5;
    c[2] = (mins[2] + maxs[2]) * 0.5;
    P("PointInBrush(ctr)  : %d", BSP_PointInBrush(b, c));
    float zmn, zmx, ztop;
    P("ColumnSpan(ctr xy) : %d  zMin=%.1f zMax=%.1f", BSP_BrushColumnSpan(b, c[0], c[1], zmn, zmx), zmn, zmx);
    P("TopZAt(ctr xy)     : %d  z=%.1f", BSP_BrushTopZAt(b, c[0], c[1], ztop), ztop);

    // Clip a small box straight down through the brush center
    float start[3], end[3], bmin[3], bmax[3], frac, hn[3];
    bool  ss;
    start    = c;
    start[2] = maxs[2] + 64.0;
    end      = c;
    end[2]   = mins[2] - 64.0;
    bmin     = view_as<float>({ -16.0, -16.0, 0.0 });
    bmax     = view_as<float>({ 16.0, 16.0, 72.0 });
    int hit  = BSP_BrushClipBox(b, start, end, bmin, bmax, frac, hn, ss);
    P("BrushClipBox       : %d frac=%.3f normal=(%.2f %.2f %.2f) startSolid=%d", hit, frac, hn[0], hn[1], hn[2], ss);

    char ord[512];
    BSP_BrushSideOrder(b, ord, sizeof ord);
    P("SideOrder          :\n%s", ord);
}

void Section_Disp()
{
    Hdr("DISPLACEMENTS");
    float pos[3];
    QueryPos(pos);

    P("DispReady=%d DispCount=%d DispDiskCount=%d", BSP_DispReady(), BSP_DispCount(), BSP_DispDiskCount());

    // Unified XY queries
    P("DispHeightAt(xy)   : %.2f", BSP_DispHeightAt(pos[0], pos[1]));
    int di;
    P("DispHeightAtDebug  : %.2f (idx=%d)", BSP_DispHeightAtDebug(pos[0], pos[1], di), di);
    float dn[3];
    P("DispSurfaceNormalAt: %.2f normal=(%.2f %.2f %.2f)", BSP_DispSurfaceNormalAt(pos[0], pos[1], dn), dn[0], dn[1], dn[2]);
    P("DispIsPointOnDisp  : %d", BSP_DispIsPointOnDisp(pos[0], pos[1]));
    float multi[8];
    P("DispHeightAtMulti  : %d hits", BSP_DispHeightAtMulti(pos[0], pos[1], multi, sizeof multi));
    P("DispDistToSurface  : %.2f", BSP_DispDistToSurface(pos, 128.0));
    P("DispTreeIndexAt    : %d", BSP_DispTreeIndexAt(pos, 128.0));

    float tn[3], v0[3], v1[3], v2[3];
    float dist = BSP_DispNearestTri(pos, 256.0, tn, v0, v1, v2);
    P("DispNearestTri     : dist=%.2f normal=(%.2f %.2f %.2f)", dist, tn[0], tn[1], tn[2]);

    // Engine accessors on tree 0
    if (BSP_DispCount() > 0)
    {
        float emin[3], emax[3];
        int   props[4];
        BSP_DispGetBounds(0, emin, emax);
        BSP_DispGetSurfaceProps(0, props);
        P("DispTree[0]        : power=%d contents=0x%X verts=%d tris=%d surfProps={%d,%d,%d,%d}",
          BSP_DispGetPower(0), BSP_DispGetContents(0), BSP_DispVertCount(0), BSP_DispTriCount(0),
          props[0], props[1], props[2], props[3]);
        P("DispTree[0] bounds : (%.0f %.0f %.0f)-(%.0f %.0f %.0f)", emin[0], emin[1], emin[2], emax[0], emax[1], emax[2]);
        float dv[3];
        if (BSP_DispGetVert(0, 0, dv))
            P("DispTree[0] vert0  : (%.1f %.1f %.1f)", dv[0], dv[1], dv[2]);
        char buf[256];
        BSP_DispDebugInfo(0, buf, sizeof buf);
        P("DispDebugInfo[0]   : %s", buf);
    }
    if (BSP_DispDiskCount() > 0)
    {
        float dmin[3], dmax[3];
        BSP_DispDiskBounds(0, dmin, dmax);
        char buf[256];
        BSP_DispDiskDebugInfo(0, buf, sizeof buf);
        P("DispDisk[0]        : (%.0f %.0f %.0f)-(%.0f %.0f %.0f) %s",
          dmin[0], dmin[1], dmin[2], dmax[0], dmax[1], dmax[2], buf);
    }
    char dq[256];
    BSP_DispDiagnoseQuery(pos[0], pos[1], dq, sizeof dq);
    P("DispDiagnoseQuery  : %s", dq);
}

void Section_Props()
{
    Hdr("STATIC PROPS");
    float pos[3];
    QueryPos(pos);

    int n = BSP_NumStaticProps();
    P("NumStaticProps=%d version=%d RtStaticPropCount=%d", n, BSP_StaticPropVersion(), BSP_RtStaticPropCount());

    if (n > 0)
    {
        float o[3], ang[3], lo[3];
        char  model[256];
        BSP_StaticPropOrigin(0, o);
        BSP_StaticPropAngles(0, ang);
        BSP_StaticPropModelName(0, model, sizeof model);
        P("Prop[0]            : org=(%.0f %.0f %.0f) ang=(%.0f %.0f %.0f) solid=%d flags=0x%X skin=%d",
          o[0], o[1], o[2], ang[0], ang[1], ang[2], BSP_StaticPropSolid(0), BSP_StaticPropFlags(0), BSP_StaticPropSkin(0));
        P("Prop[0] model      : %s", model);
        float fmin, fmax;
        BSP_StaticPropFadeDist(0, fmin, fmax);
        BSP_StaticPropLightingOrigin(0, lo);
        P("Prop[0] fade=%.0f..%.0f forcedScale=%.2f flagsEx=0x%X lightOrg=(%.0f %.0f %.0f)",
          fmin, fmax, BSP_StaticPropForcedFadeScale(0), BSP_StaticPropFlagsEx(0), lo[0], lo[1], lo[2]);
        int leaves[32];
        P("Prop[0] leaves     : %d", BSP_StaticPropLeaves(0, leaves, sizeof leaves));
    }

    // Runtime props
    char dbg[256];
    BSP_StaticPropDebug(dbg, sizeof dbg);
    P("StaticPropDebug    : %s", dbg);
    if (BSP_RtStaticPropCount() > 0)
    {
        float rmin[3], rmax[3], ro[3], ra[3];
        char  rmodel[256];
        BSP_RtStaticPropBounds(0, rmin, rmax);
        BSP_RtStaticPropOrigin(0, ro);
        BSP_RtStaticPropAngles(0, ra);
        BSP_RtStaticPropModelName(0, rmodel, sizeof rmodel);
        P("RtProp[0]          : org=(%.0f %.0f %.0f) solid=%d fsolid=0x%X model=%s",
          ro[0], ro[1], ro[2], BSP_RtStaticPropSolid(0), BSP_RtStaticPropSolidFlags(0), rmodel);

        char probe[256];
        BSP_StaticPropProbe(0, probe, sizeof probe);
        P("Probe[0]           : %s", probe);
        int tc = BSP_StaticPropTriCount(0);
        P("Prop[0] triCount   : %d", tc);
        if (tc > 0)
        {
            float a[3], bb[3], cc[3];
            if (BSP_StaticPropTri(0, 0, a, bb, cc))
                P("Prop[0] tri0       : (%.1f %.1f %.1f)", a[0], a[1], a[2]);
        }

        // Hull sweep against this prop's model
        float st[3], en[3], hmin[3], hmax[3], frac, ep[3], hn[3];
        bool  ss;
        st = ro;
        st[2] += 128.0;
        en = ro;
        en[2] -= 128.0;
        hmin = view_as<float>({ -16.0, -16.0, 0.0 });
        hmax = view_as<float>({ 16.0, 16.0, 72.0 });
        P("StaticPropTraceHull: %d", BSP_StaticPropTraceHull(0, st, en, hmin, hmax, frac, ep, hn, ss));
        P("StaticPropHullSweep: %d", BSP_StaticPropHullSweep(st, en, hmin, hmax, ro[2], frac, ep, hn, ss));
    }

    // Nearest tri across all props near the query point
    float tn[3], v0[3], v1[3], v2[3];
    int   pidx;
    float d = BSP_StaticPropNearestTri(pos, 512.0, pidx, tn, v0, v1, v2);
    P("PropNearestTri     : dist=%.2f prop=%d", d, pidx);

    // Ray hit
    float eye[3], end[3];
    if (g_Target > 0)
    {
        GetClientEyePosition(g_Target, eye);
        end = pos;
    }
    else {
        eye = pos;
        end = pos;
        end[2] -= 64.0;
    }
    P("StaticPropAtRay    : %d", BSP_StaticPropAtRay(eye, end));
}

void Section_Trace()
{
    Hdr("UNIFIED HULL TRACE");
    float start[3], end[3];
    if (g_Target > 0 && IsClientInGame(g_Target))
    {
        float ang[3], fwd[3];
        GetClientEyePosition(g_Target, start);
        GetClientEyeAngles(g_Target, ang);
        GetAngleVectors(ang, fwd, NULL_VECTOR, NULL_VECTOR);
        ScaleVector(fwd, 4096.0);
        AddVectors(start, fwd, end);
    }
    else
    {
        start = view_as<float>({ 0.0, 0.0, 1000.0 });
        end   = view_as<float>({ 0.0, 0.0, -4096.0 });
    }

    float mins[3], maxs[3];
    mins = view_as<float>({ -16.0, -16.0, 0.0 });
    maxs = view_as<float>({ 16.0, 16.0, 72.0 });

    float frac, ep[3], n[3], pd;
    bool  ss, allSolid;
    int   contents, dispFlags, sprops, sflags, htype;
    char  sname[128];

    int   r = BSP_TraceHull(start, end, mins, maxs, BSPP_MASK_PLAYERCOLLIDE, true,
                            frac, ep, n, ss, allSolid, contents, dispFlags, pd, sprops, sflags, htype, sname, sizeof sname);

    P("TraceHull          : ret=%d frac=%.3f hitType=%d", r, frac, htype);
    P("  endpos           : (%.1f %.1f %.1f)", ep[0], ep[1], ep[2]);
    P("  normal           : (%.2f %.2f %.2f) planeDist=%.2f", n[0], n[1], n[2], pd);
    P("  startSolid=%d allSolid=%d contents=0x%X dispFlags=0x%X", ss, allSolid, contents, dispFlags);
    P("  surfaceProps=%d surfaceFlags=0x%X material=%s", sprops, sflags, sname);
}

void Section_Ents()
{
    Hdr("ENTITIES");
    int n = BSP_EntityCount();
    P("EntityCount=%d EntityRawLen=%d", n, BSP_EntityRawLen());

    // Raw text length sanity (don't dump the whole thing)
    char raw[64];
    int  copied = BSP_EntityRawCopy(raw, sizeof raw);
    P("RawCopy(64)        : %d bytes, head=\"%s\"", copied, raw);

    // First few classnames
    int show = n < 5 ? n : 5;
    for (int i = 0; i < show; i++)
    {
        char cn[64];
        BSP_EntityClassname(i, cn, sizeof cn);
        float o[3];
        bool  hasOrg = BSP_EntityOrigin(i, o);
        int   mdl    = BSP_EntityModelIndex(i);
        if (hasOrg)
            P("Ent[%d] %-22s org=(%.0f %.0f %.0f) modelIdx=%d", i, cn, o[0], o[1], o[2], mdl);
        else
            P("Ent[%d] %-22s (no origin) modelIdx=%d", i, cn, mdl);
    }

    // Arbitrary KV lookup on entity 0
    char val[128];
    BSP_EntityKeyValue(0, "classname", val, sizeof val);
    P("Ent[0] KV classname: %s", val);

    // Brush-entity helpers: find a trigger_push / func_door etc.
    int tp = BSP_FindEntityByKeyValue("classname", "trigger_push");
    if (tp >= 0)
    {
        float vel[3];
        BSP_TriggerPushVelocity(tp, vel);
        float bmin[3], bmax[3];
        BSP_EntityBrushBounds(tp, bmin, bmax);
        P("trigger_push[%d]   : pushVel=(%.1f %.1f %.1f) bounds=(%.0f %.0f %.0f)-(%.0f %.0f %.0f)",
          tp, vel[0], vel[1], vel[2], bmin[0], bmin[1], bmin[2], bmax[0], bmax[1], bmax[2]);
    }
    int tt = BSP_FindEntityByKeyValue("classname", "trigger_teleport");
    if (tt >= 0)
    {
        float to[3];
        if (BSP_EntityTargetOrigin(tt, to))
            P("trigger_teleport   : target org=(%.0f %.0f %.0f)", to[0], to[1], to[2]);
    }
}

void Section_Surf()
{
    Hdr("SURFACE PROPS");
    P("SurfacePropsReady=%d SurfacePropCount=%d", BSP_SurfacePropsReady(), BSP_SurfacePropCount());
    if (!BSP_SurfacePropsReady()) return;

    int concrete = BSP_SurfacePropIndex("concrete");
    P("Index(\"concrete\")  : %d", concrete);

    // Raw surfacedata_t dump for surface 0 (RE the runtime game.* offsets).
    // Print line-split so the per-line P() buffer never truncates it.
    char dump[4096];
    BSP_SurfacePropDump(0, dump, sizeof dump);
    char lines[64][96];
    int  nl = ExplodeString(dump, "\n", lines, sizeof lines, sizeof lines[]);
    for (int i = 0; i < nl; i++)
        if (lines[i][0])
            P("%s", lines[i]);

    int show = BSP_SurfacePropCount();
    if (show > 8) show = 8;
    for (int i = 0; i < show; i++)
    {
        char name[64];
        BSP_SurfacePropName(i, name, sizeof name);
        float fr, el, den, th, damp, msf, jf;
        int   matc;
        bool  climb;
        BSP_SurfacePropData(i, fr, el, den, th, damp, msf, jf, matc, climb);
        P("Surf[%d] %-16s fric=%.2f elas=%.2f dens=%.0f maxSpd=%.2f jump=%.2f mat='%c' climb=%d",
          i, name, fr, el, den, msf, jf, matc, climb);
    }
}
