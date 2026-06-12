#include "bsp_data.h"
#include "bsp_util.h"
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

using BSPUtil::GetKeyInt;
using BSPUtil::ReadF32;
using BSPUtil::ReadI32;
using BSPUtil::ReadPtr;
using BSPUtil::ReadU16;

namespace BSPData {
// Globals - base pointer + field offsets (resolved at Init from gamedata)
static uint8_t *g_pBSPData = nullptr;

// CCollisionBSPData field offsets
static int OFF_NUMPLANES = 0;
static int OFF_MAP_PLANES = 0;
static int OFF_NUMBRUSHSIDES = 0;
static int OFF_MAP_BRUSHSIDES = 0;
static int OFF_NUMBRUSHES = 0;
static int OFF_MAP_BRUSHES = 0;
static int OFF_NUMLEAFBRUSHES = 0;
static int OFF_MAP_LEAFBRUSHES = 0;
static int OFF_NUMLEAFS = 0;
static int OFF_MAP_LEAFS = 0;
static int OFF_NUMNODES = 0;
static int OFF_MAP_NODES = 0;
static int OFF_NUMBOXBRUSHES = 0;
static int OFF_MAP_BOXBRUSHES = 0;
static int SZ_CBOXBRUSH = 48;
static int OFF_CBOXBRUSH_MINS = 0;
static int OFF_CBOXBRUSH_MAXS = 16;
static int OFF_CBOXBRUSH_SURFIDX = 32;
static int OFF_CBOXBRUSH_BRUSHNUM = 44;
static int OFF_NUMCMODELS = 0;
static int OFF_MAP_CMODELS = 0;
static int OFF_MAP_PATHNAME = 4;
static int OFF_EMPTYLEAF = 160;
static int OFF_SOLIDLEAF = 164;
static int OFF_NUMVISIBILITY = 0;
static int OFF_MAP_VISIBILITY = 0;
static int OFF_NUMAREAS = 0;
static int OFF_MAP_AREAS = 0;
static int OFF_NUMAREAPORTALS = 0;
static int OFF_MAP_AREAPORTALS = 0;

// darea_t layout
static int OFF_DAREA_NUMPORTALS = 0;
static int OFF_DAREA_FIRSTPORTAL = 4;
static int SZ_DAREA = 8;

// dareaportal_t layout
static int OFF_DAP_PORTALKEY = 0;
static int OFF_DAP_OTHERAREA = 2;
static int OFF_DAP_FIRSTCLIPVERT = 4;
static int OFF_DAP_CLIPVERTS = 6;
static int OFF_DAP_PLANENUM = 8;
static int SZ_DAP = 12;

// cbrush_t layout
static int OFF_CBRUSH_CONTENTS = 0;
static int OFF_CBRUSH_NUMSIDES = 4;
static int OFF_CBRUSH_FIRSTBRUSHSIDE = 6;
static int SZ_CBRUSH = 8;

// cbrushside_t layout
static int OFF_CBRUSHSIDE_PLANE = 0;
static int OFF_CBRUSHSIDE_TEXINFO = 4;
static int OFF_CBRUSHSIDE_BEVEL = 6;
static int OFF_CBRUSHSIDE_THIN = 7;
static int SZ_CBRUSHSIDE = 8;

// cplane_t layout
static int OFF_CPLANE_NORMAL = 0;
static int OFF_CPLANE_DIST = 12;
static int OFF_CPLANE_TYPE = 16;
static int SZ_CPLANE = 20;

// cleaf_t layout
static int OFF_CLEAF_CONTENTS = 0;
static int OFF_CLEAF_CLUSTER = 4;
static int OFF_CLEAF_AREA_FLAGS = 6; // packed: area:9 flags:7 (short)
static int OFF_CLEAF_FIRSTLEAFBRUSH = 8;
static int OFF_CLEAF_NUMLEAFBRUSHES = 10;
static int OFF_CLEAF_FIRSTLEAFFACE = 12;
static int OFF_CLEAF_NUMLEAFFACES = 14;
static int SZ_CLEAF = 16;

// cnode_t layout
static int OFF_CNODE_PLANE = 0;
static int OFF_CNODE_CHILDREN = 4;
static int SZ_CNODE = 12;

// cmodel_t layout
static int OFF_CMODEL_MINS = 0;
static int OFF_CMODEL_MAXS = 12;
static int OFF_CMODEL_ORIGIN = 24;
static int OFF_CMODEL_HEADNODE = 36;
static int SZ_CMODEL = 40;

// Cache state (brush AABB + leaf AABB)
struct BrushCacheEntry {
  float mins[3];
  float maxs[3];
  int contents;
  bool valid;
};

struct LeafCacheEntry {
  float mins[3];
  float maxs[3];
  bool valid;
};

static std::vector<BrushCacheEntry> g_brushCache;
static bool g_brushCacheBuilt = false;
static std::vector<LeafCacheEntry> g_leafCache;
// reuse same struct for node AABB
static std::vector<LeafCacheEntry> g_nodeCache;

static bool g_leafCacheBuilt = false; // gates both leaf + node caches

// Mutex guards both caches against the async worker that builds a fresh
// brush cache on a side thread. Reads (FindBrushPairAtSeam, LeafBounds)
// take the lock; the worker swaps under exclusive lock when done.
static std::mutex g_brushCacheMutex;
static std::atomic<bool> g_asyncBuildInProgress{false};
// Detached worker handle; kept around so a second OnMapStart can't race
// with an in-flight build. Joined explicitly by Shutdown() so the .so can
// unload cleanly without the worker outliving the engine module.
static std::thread g_asyncBuildThread;

// Authoritative box-brush membership: bit set per brush index that is the
// originalBrush of some cboxbrush_t entry. Built lazily on first BrushIsBoxAuth
// call and invalidated when the box-brush count changes (map switch).
// Natives run on the main thread, so no locking is needed
// (the async worker never touches this).
static std::vector<bool> g_boxBrushAuth;
// Parallel map: brush index -> its cboxbrush_t index (-1 if not a box brush).
// Built alongside g_boxBrushAuth. Lets the plane-accurate accessors fall back
// to the authoritative box AABB for box-optimized brushes (whose cbrush_t
// sides are stripped / bevel-padded, so their planes are not usable).
static std::vector<int> g_brushToBoxIdx;
static int g_boxBrushAuthBuiltFor = -1; // box count this set was built for

// Forward decls - built lazily by various accessors below.
static void BuildBrushCache();
static void BuildLeafCache();
static void PopulateBrushCache(std::vector<BrushCacheEntry> &out);

bool Init(IGameConfig *gameconf, char *error, size_t maxlen) {
  // Resolve g_BSPData base via Addresses chain.
  void *addr = nullptr;
  if (!gameconf->GetAddress("g_BSPData", &addr) || addr == nullptr) {
    snprintf(error, maxlen, "gamedata Address 'g_BSPData' did not resolve.");
    return false;
  }
  g_pBSPData = reinterpret_cast<uint8_t *>(addr);

  // CCollisionBSPData field offsets
  OFF_NUMPLANES = GetKeyInt(gameconf, "off_numplanes", -1);
  OFF_MAP_PLANES = GetKeyInt(gameconf, "off_map_planes", -1);
  OFF_NUMBRUSHSIDES = GetKeyInt(gameconf, "off_numbrushsides", -1);
  OFF_MAP_BRUSHSIDES = GetKeyInt(gameconf, "off_map_brushsides", -1);
  OFF_NUMBRUSHES = GetKeyInt(gameconf, "off_numbrushes", -1);
  OFF_MAP_BRUSHES = GetKeyInt(gameconf, "off_map_brushes", -1);
  OFF_NUMLEAFBRUSHES = GetKeyInt(gameconf, "off_numleafbrushes", -1);
  OFF_MAP_LEAFBRUSHES = GetKeyInt(gameconf, "off_map_leafbrushes", -1);
  OFF_NUMLEAFS = GetKeyInt(gameconf, "off_numleafs", 0);
  OFF_MAP_LEAFS = GetKeyInt(gameconf, "off_map_leafs", 0);
  OFF_NUMNODES = GetKeyInt(gameconf, "off_numnodes", 0);
  OFF_MAP_NODES = GetKeyInt(gameconf, "off_map_nodes", 0);
  OFF_NUMBOXBRUSHES = GetKeyInt(gameconf, "off_numboxbrushes", 0);
  OFF_MAP_BOXBRUSHES = GetKeyInt(gameconf, "off_map_boxbrushes", 0);
  SZ_CBOXBRUSH = GetKeyInt(gameconf, "cboxbrush_sizeof", 48);
  OFF_CBOXBRUSH_MINS = GetKeyInt(gameconf, "cboxbrush_mins", 0);
  OFF_CBOXBRUSH_MAXS = GetKeyInt(gameconf, "cboxbrush_maxs", 16);
  OFF_CBOXBRUSH_SURFIDX = GetKeyInt(gameconf, "cboxbrush_surfidx", 32);
  OFF_CBOXBRUSH_BRUSHNUM = GetKeyInt(gameconf, "cboxbrush_brushnum", 44);
  OFF_NUMCMODELS = GetKeyInt(gameconf, "off_numcmodels", 0);
  OFF_MAP_CMODELS = GetKeyInt(gameconf, "off_map_cmodels", 0);
  OFF_MAP_PATHNAME = GetKeyInt(gameconf, "off_map_pathname", 4);
  OFF_EMPTYLEAF = GetKeyInt(gameconf, "off_emptyleaf", 160);
  OFF_SOLIDLEAF = GetKeyInt(gameconf, "off_solidleaf", 164);

  // Visibility / areas / area portals
  OFF_NUMVISIBILITY = GetKeyInt(gameconf, "off_numvisibility", 0);
  OFF_MAP_VISIBILITY = GetKeyInt(gameconf, "off_map_visibility", 0);
  OFF_NUMAREAS = GetKeyInt(gameconf, "off_numareas", 0);
  OFF_MAP_AREAS = GetKeyInt(gameconf, "off_map_areas", 0);
  OFF_NUMAREAPORTALS = GetKeyInt(gameconf, "off_numareaportals", 0);
  OFF_MAP_AREAPORTALS = GetKeyInt(gameconf, "off_map_areaportals", 0);

  // darea_t / dareaportal_t
  OFF_DAREA_NUMPORTALS = GetKeyInt(gameconf, "darea_numportals", 0);
  OFF_DAREA_FIRSTPORTAL = GetKeyInt(gameconf, "darea_firstportal", 4);
  SZ_DAREA = GetKeyInt(gameconf, "darea_sizeof", 8);
  OFF_DAP_PORTALKEY = GetKeyInt(gameconf, "dareaportal_portalkey", 0);
  OFF_DAP_OTHERAREA = GetKeyInt(gameconf, "dareaportal_otherarea", 2);
  OFF_DAP_FIRSTCLIPVERT = GetKeyInt(gameconf, "dareaportal_firstclipvert", 4);
  OFF_DAP_CLIPVERTS = GetKeyInt(gameconf, "dareaportal_clipverts", 6);
  OFF_DAP_PLANENUM = GetKeyInt(gameconf, "dareaportal_planenum", 8);
  SZ_DAP = GetKeyInt(gameconf, "dareaportal_sizeof", 12);

  // cbrush_t / cbrushside_t / cplane_t
  OFF_CBRUSH_CONTENTS = GetKeyInt(gameconf, "cbrush_contents", 0);
  OFF_CBRUSH_NUMSIDES = GetKeyInt(gameconf, "cbrush_numsides", 4);
  OFF_CBRUSH_FIRSTBRUSHSIDE = GetKeyInt(gameconf, "cbrush_firstbrushside", 6);
  SZ_CBRUSH = GetKeyInt(gameconf, "cbrush_sizeof", 8);

  OFF_CBRUSHSIDE_PLANE = GetKeyInt(gameconf, "cbrushside_plane", 0);
  OFF_CBRUSHSIDE_TEXINFO = GetKeyInt(gameconf, "cbrushside_texinfo", 4);
  OFF_CBRUSHSIDE_BEVEL = GetKeyInt(gameconf, "cbrushside_bevel", 6);
  OFF_CBRUSHSIDE_THIN = GetKeyInt(gameconf, "cbrushside_thin", 7);
  SZ_CBRUSHSIDE = GetKeyInt(gameconf, "cbrushside_sizeof", 8);

  OFF_CPLANE_NORMAL = GetKeyInt(gameconf, "cplane_normal", 0);
  OFF_CPLANE_DIST = GetKeyInt(gameconf, "cplane_dist", 12);
  OFF_CPLANE_TYPE = GetKeyInt(gameconf, "cplane_type", 16);
  SZ_CPLANE = GetKeyInt(gameconf, "cplane_sizeof", 20);

  // cleaf_t
  OFF_CLEAF_CONTENTS = GetKeyInt(gameconf, "cleaf_contents", 0);
  OFF_CLEAF_CLUSTER = GetKeyInt(gameconf, "cleaf_cluster", 4);
  OFF_CLEAF_AREA_FLAGS = GetKeyInt(gameconf, "cleaf_area_flags", 6);
  OFF_CLEAF_FIRSTLEAFBRUSH = GetKeyInt(gameconf, "cleaf_firstleafbrush", 8);
  OFF_CLEAF_NUMLEAFBRUSHES = GetKeyInt(gameconf, "cleaf_numleafbrushes", 10);
  OFF_CLEAF_FIRSTLEAFFACE = GetKeyInt(gameconf, "cleaf_firstleafface", 12);
  OFF_CLEAF_NUMLEAFFACES = GetKeyInt(gameconf, "cleaf_numleaffaces", 14);
  SZ_CLEAF = GetKeyInt(gameconf, "cleaf_sizeof", 16);

  // cnode_t
  OFF_CNODE_PLANE = GetKeyInt(gameconf, "cnode_plane", 0);
  OFF_CNODE_CHILDREN = GetKeyInt(gameconf, "cnode_children", 4);
  SZ_CNODE = GetKeyInt(gameconf, "cnode_sizeof", 12);

  // cmodel_t
  OFF_CMODEL_MINS = GetKeyInt(gameconf, "cmodel_mins", 0);
  OFF_CMODEL_MAXS = GetKeyInt(gameconf, "cmodel_maxs", 12);
  OFF_CMODEL_ORIGIN = GetKeyInt(gameconf, "cmodel_origin", 24);
  OFF_CMODEL_HEADNODE = GetKeyInt(gameconf, "cmodel_headnode", 36);
  SZ_CMODEL = GetKeyInt(gameconf, "cmodel_sizeof", 40);

  // Sanity.
  if (OFF_NUMBRUSHES < 0 || OFF_MAP_BRUSHES < 0 || OFF_NUMBRUSHSIDES < 0 ||
      OFF_MAP_BRUSHSIDES < 0 || OFF_NUMPLANES < 0 || OFF_MAP_PLANES < 0) {
    snprintf(error, maxlen, "gamedata Keys missing required field offsets.");
    return false;
  }
  return true;
}

void Shutdown() {
  // Wait for any in-flight async build before we tear down the cache vector.
  if (g_asyncBuildThread.joinable())
    g_asyncBuildThread.join();
  std::lock_guard<std::mutex> lk(g_brushCacheMutex);
  g_brushCache.clear();
  g_brushCacheBuilt = false;
  g_leafCache.clear();
  g_nodeCache.clear();
  g_leafCacheBuilt = false;
  g_boxBrushAuth.clear();
  g_brushToBoxIdx.clear();
  g_boxBrushAuthBuiltFor = -1;
}

void OnMapStart() {
  std::lock_guard<std::mutex> lk(g_brushCacheMutex);
  g_brushCache.clear();
  g_brushCacheBuilt = false;
  g_leafCache.clear();
  g_nodeCache.clear();
  g_leafCacheBuilt = false;
  g_boxBrushAuth.clear();
  g_brushToBoxIdx.clear();
  g_boxBrushAuthBuiltFor = -1;
}

// Debug accessors (used by extension.cpp at load time)
void *DebugGetBase() { return g_pBSPData; }

int DebugGetOff(const char *name) {
  if (!strcmp(name, "numbrushes"))
    return OFF_NUMBRUSHES;
  if (!strcmp(name, "map_brushes"))
    return OFF_MAP_BRUSHES;
  if (!strcmp(name, "numplanes"))
    return OFF_NUMPLANES;
  if (!strcmp(name, "map_planes"))
    return OFF_MAP_PLANES;
  if (!strcmp(name, "numleafs"))
    return OFF_NUMLEAFS;
  if (!strcmp(name, "map_leafs"))
    return OFF_MAP_LEAFS;
  return -1;
}

void DebugDumpBoxBrushes(int maxCount) {
  if (!g_pBSPData || OFF_MAP_BOXBRUSHES == 0 || SZ_CBOXBRUSH <= 0)
    return;
  int n = GetNumBoxBrushes();
  if (n <= 0)
    return;
  const uint8_t *table = reinterpret_cast<const uint8_t *>(
      ReadPtr(g_pBSPData, OFF_MAP_BOXBRUSHES));
  if (!table)
    return;
  if (maxCount > n)
    maxCount = n;
  smutils->LogMessage(myself,
                      "BoxBrushDump: count=%d sizeof=%d table=%p dumping %d", n,
                      SZ_CBOXBRUSH, (const void *)table, maxCount);
  for (int i = 0; i < maxCount; ++i) {
    const uint8_t *p = table + (size_t)i * SZ_CBOXBRUSH;
    smutils->LogMessage(myself,
                        "  bb[%d] @+00..0F: "
                        "%02X %02X %02X %02X %02X %02X %02X %02X "
                        "%02X %02X %02X %02X %02X %02X %02X %02X",
                        i, p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8],
                        p[9], p[10], p[11], p[12], p[13], p[14], p[15]);
    smutils->LogMessage(myself,
                        "  bb[%d] @+10..1F: "
                        "%02X %02X %02X %02X %02X %02X %02X %02X "
                        "%02X %02X %02X %02X %02X %02X %02X %02X",
                        i, p[16], p[17], p[18], p[19], p[20], p[21], p[22],
                        p[23], p[24], p[25], p[26], p[27], p[28], p[29], p[30],
                        p[31]);
    smutils->LogMessage(myself,
                        "  bb[%d] @+20..2F: "
                        "%02X %02X %02X %02X %02X %02X %02X %02X "
                        "%02X %02X %02X %02X %02X %02X %02X %02X",
                        i, p[32], p[33], p[34], p[35], p[36], p[37], p[38],
                        p[39], p[40], p[41], p[42], p[43], p[44], p[45], p[46],
                        p[47]);
    // Decoded interpretations as floats at common offsets:
    // assuming layout might mirror cmodel_t (mins[3] @0, maxs[3] @12) or
    // SIMD-aligned VectorAligned (16B mins, 16B maxs, 16B meta).
    float f00 = ReadF32(p, 0), f04 = ReadF32(p, 4), f08 = ReadF32(p, 8);
    float f12 = ReadF32(p, 12), f16 = ReadF32(p, 16), f20 = ReadF32(p, 20);
    float f24 = ReadF32(p, 24), f28 = ReadF32(p, 28), f32 = ReadF32(p, 32);
    int i32 = ReadI32(p, 32), i36 = ReadI32(p, 36);
    smutils->LogMessage(
        myself,
        "  bb[%d] floats: f00=%.2f f04=%.2f f08=%.2f | f12=%.2f f16=%.2f "
        "f20=%.2f | f24=%.2f f28=%.2f f32=%.2f | i@32=%d i@36=%d",
        i, f00, f04, f08, f12, f16, f20, f24, f28, f32, i32, i36);
  }
}

void DebugDumpCBSP(int startOff, int endOff) {
  if (!g_pBSPData)
    return;
  if (startOff < 0)
    startOff = 0;
  if (endOff > 4096)
    endOff = 4096;
  if (endOff <= startOff)
    return;
  if (endOff - startOff > 2048)
    endOff = startOff + 2048;
  startOff &= ~3; // dword-align
  endOff = (endOff + 3) & ~3;

  smutils->LogMessage(myself, "CBSPDump base=%p range=+0x%X..+0x%X",
                      (void *)g_pBSPData, startOff, endOff);
  for (int off = startOff; off < endOff; off += 4) {
    const uint8_t *p = g_pBSPData + off;
    uint32_t v = (uint32_t)ReadI32(p, 0);
    float f = ReadF32(p, 0);
    int hasPtrShape = (v >= 0x10000u && v < 0x80000000u) ? 1 : 0;
    smutils->LogMessage(
        myself,
        "  +0x%03X: %02X %02X %02X %02X  int=%-11d float=%-12.4g ptr=0x%08X%s",
        off, p[0], p[1], p[2], p[3], (int)v, f, v, hasPtrShape ? " *" : "");
  }
}

void DebugDumpCBSPPtr(int ptrOff, int bytes) {
  if (!g_pBSPData || ptrOff < 0 || ptrOff > 4092)
    return;
  if (bytes <= 0)
    return;
  if (bytes > 1024)
    bytes = 1024;
  const uint8_t *p =
      reinterpret_cast<const uint8_t *>(ReadPtr(g_pBSPData, ptrOff));
  if (!p) {
    smutils->LogMessage(myself, "CBSPPtrDump +0x%X = NULL", ptrOff);
    return;
  }
  smutils->LogMessage(myself, "CBSPPtrDump +0x%X -> %p (%d bytes)", ptrOff,
                      (void *)p, bytes);
  // 16 bytes per line: hex + ascii + 4x interp dwords.
  int rounded = (bytes + 15) & ~15;
  for (int row = 0; row < rounded; row += 16) {
    const uint8_t *r = p + row;
    char hex[64], ascii[20];
    int hi = 0;
    for (int i = 0; i < 16; ++i)
      hi += snprintf(hex + hi, sizeof(hex) - hi, "%02X ", r[i]);
    for (int i = 0; i < 16; ++i)
      ascii[i] = (r[i] >= 32 && r[i] < 127) ? (char)r[i] : '.';
    ascii[16] = 0;
    int d0 = ReadI32(r, 0), d1 = ReadI32(r, 4), d2 = ReadI32(r, 8),
        d3 = ReadI32(r, 12);
    smutils->LogMessage(myself, "  +%02X: %s |%s|  d0=%d d1=%d d2=%d d3=%d",
                        row, hex, ascii, d0, d1, d2, d3);
  }
}

// Internal helpers - table-relative pointer resolution.
// Resolve element `idx` of an array whose base pointer lives at tableOff inside
// g_BSPData. Gates on a null base, an unconfigured (0) offset, a bad stride and
// a [0,count) range, so every accessor below collapses to one call.
static const uint8_t *TableEntry(int tableOff, int idx, int count, int stride) {
  if (!g_pBSPData || tableOff == 0 || stride <= 0 || idx < 0 || idx >= count)
    return nullptr;
  const uint8_t *table =
      reinterpret_cast<const uint8_t *>(ReadPtr(g_pBSPData, tableOff));
  if (!table)
    return nullptr;
  return table + (size_t)idx * stride;
}

// Read a cplane_t's normal + dist from its resolved pointer.
static void ReadPlane(const uint8_t *plane, float normal[3], float &dist) {
  normal[0] = ReadF32(plane, OFF_CPLANE_NORMAL + 0);
  normal[1] = ReadF32(plane, OFF_CPLANE_NORMAL + 4);
  normal[2] = ReadF32(plane, OFF_CPLANE_NORMAL + 8);
  dist = ReadF32(plane, OFF_CPLANE_DIST);
}

static const uint8_t *brush_at(int idx) {
  return TableEntry(OFF_MAP_BRUSHES, idx, GetNumBrushes(), SZ_CBRUSH);
}

static const uint8_t *brushside_at(int idx) {
  return TableEntry(OFF_MAP_BRUSHSIDES, idx, GetNumBrushSides(), SZ_CBRUSHSIDE);
}

static const uint8_t *leaf_at(int idx) {
  return TableEntry(OFF_MAP_LEAFS, idx, GetNumLeaves(), SZ_CLEAF);
}

static const uint8_t *node_at(int idx) {
  return TableEntry(OFF_MAP_NODES, idx, GetNumNodes(), SZ_CNODE);
}

// CSGO marks box-optimized brushes with cbrush_t.numsides == 0xFFFF and strips/
// pads their planar sides (the real solid lives in the cboxbrush_t SIMD table).
// Reading that count verbatim makes every side loop iterate 65535 times across
// the shared brushside array, producing map-spanning garbage bounds, empty
// windings, and a 65535-side "SideOrder". Treat the sentinel (or any count past
// the global side table) as "no usable planar sides" so callers fall back to
// the box table / brush AABB cache instead.
static uint16_t BrushSideCount(const uint8_t *b) {
  uint16_t numsides = ReadU16(b, OFF_CBRUSH_NUMSIDES);
  int total = GetNumBrushSides();
  if (numsides == 0xFFFF || (total > 0 && (int)numsides > total))
    return 0;
  return numsides;
}

// Resolve brush `brushIdx`'s side `sideIdx` (0-based within the brush).
static const uint8_t *brushside_for(int brushIdx, int sideIdx) {
  const uint8_t *b = brush_at(brushIdx);
  if (!b)
    return nullptr;
  uint16_t numsides = BrushSideCount(b);
  uint16_t first = ReadU16(b, OFF_CBRUSH_FIRSTBRUSHSIDE);
  if (sideIdx < 0 || sideIdx >= (int)numsides)
    return nullptr;
  return brushside_at((int)first + sideIdx);
}

// Counts
int GetNumBrushes() {
  return g_pBSPData ? ReadI32(g_pBSPData, OFF_NUMBRUSHES) : 0;
}
int GetNumBrushSides() {
  return g_pBSPData ? ReadI32(g_pBSPData, OFF_NUMBRUSHSIDES) : 0;
}
int GetNumPlanes() {
  return g_pBSPData ? ReadI32(g_pBSPData, OFF_NUMPLANES) : 0;
}
int GetNumLeafBrushes() {
  return g_pBSPData ? ReadI32(g_pBSPData, OFF_NUMLEAFBRUSHES) : 0;
}
int GetNumLeaves() {
  if (!g_pBSPData || OFF_NUMLEAFS == 0)
    return 0;
  return ReadI32(g_pBSPData, OFF_NUMLEAFS);
}
int GetNumNodes() {
  if (!g_pBSPData || OFF_NUMNODES == 0)
    return 0;
  return ReadI32(g_pBSPData, OFF_NUMNODES);
}
int GetNumBoxBrushes() {
  if (!g_pBSPData || OFF_NUMBOXBRUSHES == 0)
    return 0;
  return ReadI32(g_pBSPData, OFF_NUMBOXBRUSHES);
}
int GetNumCModels() {
  if (!g_pBSPData || OFF_NUMCMODELS == 0)
    return 0;
  return ReadI32(g_pBSPData, OFF_NUMCMODELS);
}
int GetNumAreas() {
  if (!g_pBSPData || OFF_NUMAREAS == 0)
    return 0;
  return ReadI32(g_pBSPData, OFF_NUMAREAS);
}
int GetNumAreaPortals() {
  if (!g_pBSPData || OFF_NUMAREAPORTALS == 0)
    return 0;
  return ReadI32(g_pBSPData, OFF_NUMAREAPORTALS);
}
int GetNumClusters() {
  if (!g_pBSPData || OFF_MAP_VISIBILITY == 0)
    return 0;
  const uint8_t *vis = reinterpret_cast<const uint8_t *>(
      ReadPtr(g_pBSPData, OFF_MAP_VISIBILITY));
  if (!vis)
    return 0;
  return ReadI32(vis, 0);
}

// Areas / area portals
static const uint8_t *area_at(int idx) {
  return TableEntry(OFF_MAP_AREAS, idx, GetNumAreas(), SZ_DAREA);
}

static const uint8_t *areaportal_at(int idx) {
  return TableEntry(OFF_MAP_AREAPORTALS, idx, GetNumAreaPortals(), SZ_DAP);
}

bool AreaInfo(int areaIdx, int &numPortals, int &firstPortal) {
  const uint8_t *a = area_at(areaIdx);
  if (!a)
    return false;
  numPortals = ReadI32(a, OFF_DAREA_NUMPORTALS);
  firstPortal = ReadI32(a, OFF_DAREA_FIRSTPORTAL);
  return true;
}

bool AreaPortalInfo(int portalIdx, int &portalKey, int &otherArea,
                    int &firstClipVert, int &clipVerts, int &planenum) {
  const uint8_t *p = areaportal_at(portalIdx);
  if (!p)
    return false;
  portalKey = (int)ReadU16(p, OFF_DAP_PORTALKEY);
  otherArea = (int)ReadU16(p, OFF_DAP_OTHERAREA);
  firstClipVert = (int)ReadU16(p, OFF_DAP_FIRSTCLIPVERT);
  clipVerts = (int)ReadU16(p, OFF_DAP_CLIPVERTS);
  planenum = ReadI32(p, OFF_DAP_PLANENUM);
  return true;
}

// Visibility (PVS)
int VisRowDecompress(int cluster, uint8_t *outBuf, int maxBytes) {
  if (cluster < 0 || !outBuf || maxBytes <= 0 || !g_pBSPData ||
      OFF_MAP_VISIBILITY == 0)
    return 0;
  const uint8_t *vis = reinterpret_cast<const uint8_t *>(
      ReadPtr(g_pBSPData, OFF_MAP_VISIBILITY));
  if (!vis)
    return 0;
  int nc = ReadI32(vis, 0);
  if (cluster >= nc)
    return 0;
  // dvis_t: { int numclusters; int bitofs[numclusters][2]; }
  // bitofs[cluster][0] = byte offset of PVS row from start of vis blob.
  int rowOff = ReadI32(vis, 4 + cluster * 8 + 0);
  int rowBytes = (nc + 7) >> 3;
  if (rowBytes > maxBytes)
    rowBytes = maxBytes;
  const uint8_t *in = vis + rowOff;
  int outIdx = 0;
  // Standard Source RLE: nonzero byte verbatim; zero byte followed by N
  // (count of zero bytes to emit).
  while (outIdx < rowBytes) {
    uint8_t b = *in++;
    if (b) {
      outBuf[outIdx++] = b;
    } else {
      int rep = *in++;
      while (rep > 0 && outIdx < rowBytes) {
        outBuf[outIdx++] = 0;
        --rep;
      }
    }
  }
  return outIdx;
}

bool ClustersVisible(int c1, int c2) {
  if (c1 < 0 || c2 < 0)
    return false;
  if (c1 == c2)
    return true;
  int nc = GetNumClusters();
  if (nc <= 0 || c1 >= nc || c2 >= nc)
    return false;
  // MAX_MAP_CLUSTERS = 65536 -> 8192 bytes max row.
  uint8_t row[8192];
  int got = VisRowDecompress(c1, row, (int)sizeof(row));
  int targetByte = c2 >> 3;
  if (got <= targetByte)
    return false;
  return (row[targetByte] & (1 << (c2 & 7))) != 0;
}

bool LeavesVisible(int leaf1, int leaf2) {
  int c1 = LeafCluster(leaf1);
  int c2 = LeafCluster(leaf2);
  return ClustersVisible(c1, c2);
}

// Misc accessors
int MapPathName(char *buf, int maxlen) {
  if (!g_pBSPData || maxlen <= 0)
    return 0;
  const char *src =
      reinterpret_cast<const char *>(g_pBSPData + OFF_MAP_PATHNAME);
  int len = 0;
  while (len < 95 && len < maxlen - 1 && src[len] != '\0') {
    buf[len] = src[len];
    ++len;
  }
  buf[len] = '\0';
  return len;
}

int EmptyLeaf() { return g_pBSPData ? ReadI32(g_pBSPData, OFF_EMPTYLEAF) : -1; }

int SolidLeaf() { return g_pBSPData ? ReadI32(g_pBSPData, OFF_SOLIDLEAF) : -1; }

int SelfTest() {
  int mask = 0;
  if (!g_pBSPData)
    return 0;
  // Bit 0: base resolved + brush count + table look sane.
  int nb = GetNumBrushes();
  if (nb > 0 && nb <= 1000000 && OFF_MAP_BRUSHES != 0 &&
      ReadPtr(g_pBSPData, OFF_MAP_BRUSHES) != nullptr)
    mask |= 0x1;
  // Bit 1: leaf + node tables present.
  if (GetNumLeaves() > 0 && GetNumNodes() > 0 && OFF_MAP_LEAFS != 0 &&
      OFF_MAP_NODES != 0 && ReadPtr(g_pBSPData, OFF_MAP_LEAFS) != nullptr &&
      ReadPtr(g_pBSPData, OFF_MAP_NODES) != nullptr)
    mask |= 0x2;
  // Bit 2: cboxbrush_t SIMD table present.
  if (GetNumBoxBrushes() > 0 && OFF_MAP_BOXBRUSHES != 0 &&
      ReadPtr(g_pBSPData, OFF_MAP_BOXBRUSHES) != nullptr)
    mask |= 0x4;
  // Bit 3: visibility blob present.
  if (GetNumClusters() > 0)
    mask |= 0x8;
  return mask;
}

// Point queries
int LeafAtPoint(const float pos[3]) {
  // Start at root (node[0]), each step:
  // signed distance to plane -> descend children[0] if d>=0 else children[1].
  // Negative child = leaf via Source convention: leafIdx = -1 - child.
  // O(log N).
  if (!g_pBSPData || OFF_MAP_NODES == 0 || SZ_CNODE == 0)
    return -1;
  int numnodes = (OFF_NUMNODES != 0) ? ReadI32(g_pBSPData, OFF_NUMNODES) : 0;
  if (numnodes <= 0)
    return -1;
  const uint8_t *nodes =
      reinterpret_cast<const uint8_t *>(ReadPtr(g_pBSPData, OFF_MAP_NODES));
  if (!nodes)
    return -1;

  int idx = 0; // root = map_nodes[0]
  for (int safety = 0; safety < 512; ++safety) {
    if (idx < 0)
      return -1 - idx; // leaf reached
    if (idx >= numnodes)
      return -1; // out-of-bounds (corrupt)
    const uint8_t *node = nodes + (size_t)idx * SZ_CNODE;
    const uint8_t *plane =
        reinterpret_cast<const uint8_t *>(ReadPtr(node, OFF_CNODE_PLANE));
    if (!plane)
      return -1;
    float nx = ReadF32(plane, OFF_CPLANE_NORMAL + 0);
    float ny = ReadF32(plane, OFF_CPLANE_NORMAL + 4);
    float nz = ReadF32(plane, OFF_CPLANE_NORMAL + 8);
    float d = pos[0] * nx + pos[1] * ny + pos[2] * nz -
              ReadF32(plane, OFF_CPLANE_DIST);
    int childOff = OFF_CNODE_CHILDREN + (d >= 0.0f ? 0 : 4);
    idx = ReadI32(node, childOff);
  }
  return -1;
}

int PointContents(const float pos[3]) {
  int leaf = LeafAtPoint(pos);
  if (leaf < 0)
    return 0;
  return LeafContents(leaf);
}

// Brush accessors
int GetBrushContents(int brushIdx) {
  const uint8_t *b = brush_at(brushIdx);
  if (!b)
    return 0;
  return ReadI32(b, OFF_CBRUSH_CONTENTS);
}

bool GetBrushBounds(int brushIdx, float mins[3], float maxs[3]) {
  const uint8_t *b = brush_at(brushIdx);
  if (!b)
    return false;
  uint16_t numsides = BrushSideCount(b);
  uint16_t first = ReadU16(b, OFF_CBRUSH_FIRSTBRUSHSIDE);
  if (numsides == 0)
    return false;

  mins[0] = mins[1] = mins[2] = 1e30f;
  maxs[0] = maxs[1] = maxs[2] = -1e30f;

  for (uint16_t i = 0; i < numsides; ++i) {
    const uint8_t *side = brushside_at(first + i);
    if (!side)
      continue;
    const uint8_t *plane =
        reinterpret_cast<const uint8_t *>(ReadPtr(side, OFF_CBRUSHSIDE_PLANE));
    if (!plane)
      continue;
    float nx = ReadF32(plane, OFF_CPLANE_NORMAL + 0);
    float ny = ReadF32(plane, OFF_CPLANE_NORMAL + 4);
    float nz = ReadF32(plane, OFF_CPLANE_NORMAL + 8);
    float d = ReadF32(plane, OFF_CPLANE_DIST);
    if (nx > 0.99f) {
      if (d > maxs[0])
        maxs[0] = d;
    }
    if (nx < -0.99f) {
      if (-d < mins[0])
        mins[0] = -d;
    }
    if (ny > 0.99f) {
      if (d > maxs[1])
        maxs[1] = d;
    }
    if (ny < -0.99f) {
      if (-d < mins[1])
        mins[1] = -d;
    }
    if (nz > 0.99f) {
      if (d > maxs[2])
        maxs[2] = d;
    }
    if (nz < -0.99f) {
      if (-d < mins[2])
        mins[2] = -d;
    }
  }
  for (int i = 0; i < 3; ++i)
    if (mins[i] > maxs[i])
      return false;
  return true;
}

bool IsBoxBrush(int brushIdx) {
  const uint8_t *b = brush_at(brushIdx);
  if (!b)
    return false;
  uint16_t numsides = BrushSideCount(b);
  uint16_t first = ReadU16(b, OFF_CBRUSH_FIRSTBRUSHSIDE);
  if (numsides != 6)
    return false;
  int axisHit[6] = {0, 0, 0, 0, 0, 0};
  for (uint16_t i = 0; i < numsides; ++i) {
    const uint8_t *side = brushside_at(first + i);
    if (!side)
      return false;
    const uint8_t *plane =
        reinterpret_cast<const uint8_t *>(ReadPtr(side, OFF_CBRUSHSIDE_PLANE));
    if (!plane)
      return false;
    float nx = ReadF32(plane, OFF_CPLANE_NORMAL + 0);
    float ny = ReadF32(plane, OFF_CPLANE_NORMAL + 4);
    float nz = ReadF32(plane, OFF_CPLANE_NORMAL + 8);
    if (nx > 0.999f)
      axisHit[0]++;
    else if (nx < -0.999f)
      axisHit[1]++;
    else if (ny > 0.999f)
      axisHit[2]++;
    else if (ny < -0.999f)
      axisHit[3]++;
    else if (nz > 0.999f)
      axisHit[4]++;
    else if (nz < -0.999f)
      axisHit[5]++;
    else
      return false;
  }
  for (int i = 0; i < 6; ++i)
    if (axisHit[i] != 1)
      return false;
  return true;
}

int BrushNumSides(int brushIdx) {
  const uint8_t *b = brush_at(brushIdx);
  if (!b)
    return -1;
  return (int)BrushSideCount(b);
}

bool BrushSidePlane(int brushIdx, int sideIdx, float normal[3], float &dist) {
  const uint8_t *side = brushside_for(brushIdx, sideIdx);
  if (!side)
    return false;
  const uint8_t *plane =
      reinterpret_cast<const uint8_t *>(ReadPtr(side, OFF_CBRUSHSIDE_PLANE));
  if (!plane)
    return false;
  ReadPlane(plane, normal, dist);
  return true;
}

int BrushSideBevel(int brushIdx, int sideIdx) {
  const uint8_t *side = brushside_for(brushIdx, sideIdx);
  if (!side)
    return -1;
  return (int)*(side + OFF_CBRUSHSIDE_BEVEL); // byte read
}

int BrushSideThin(int brushIdx, int sideIdx) {
  const uint8_t *side = brushside_for(brushIdx, sideIdx);
  if (!side)
    return -1;
  return (int)*(side + OFF_CBRUSHSIDE_THIN);
}

int BrushSideTexInfo(int brushIdx, int sideIdx) {
  const uint8_t *side = brushside_for(brushIdx, sideIdx);
  if (!side)
    return -1;
  return (int)ReadU16(side, OFF_CBRUSHSIDE_TEXINFO);
}

int BrushSidePlaneIndex(int brushIdx, int sideIdx) {
  const uint8_t *side = brushside_for(brushIdx, sideIdx);
  if (!side || !g_pBSPData || OFF_MAP_PLANES == 0 || SZ_CPLANE <= 0)
    return -1;
  const uint8_t *plane =
      reinterpret_cast<const uint8_t *>(ReadPtr(side, OFF_CBRUSHSIDE_PLANE));
  const uint8_t *base =
      reinterpret_cast<const uint8_t *>(ReadPtr(g_pBSPData, OFF_MAP_PLANES));
  if (!plane || !base || plane < base)
    return -1;
  auto delta = plane - base;
  if (delta % SZ_CPLANE != 0)
    return -1; // pointer doesn't land on a map_planes entry
  int idx = (int)(delta / SZ_CPLANE);
  if (idx < 0 || idx >= GetNumPlanes())
    return -1;
  return idx;
}

// Leaf accessors
int LeafBrushes(int leafIdx, int *outBuf, int maxOut) {
  const uint8_t *leaf = leaf_at(leafIdx);
  if (!leaf || OFF_MAP_LEAFBRUSHES == 0)
    return -1;
  const uint16_t *table = reinterpret_cast<const uint16_t *>(
      ReadPtr(g_pBSPData, OFF_MAP_LEAFBRUSHES));
  if (!table)
    return -1;
  uint16_t first = ReadU16(leaf, OFF_CLEAF_FIRSTLEAFBRUSH);
  uint16_t count = ReadU16(leaf, OFF_CLEAF_NUMLEAFBRUSHES);
  int n = (int)count;
  if (n > maxOut)
    n = maxOut;
  for (int i = 0; i < n; ++i)
    outBuf[i] = (int)table[(size_t)first + i];
  return n;
}

int LeafContents(int leafIdx) {
  const uint8_t *leaf = leaf_at(leafIdx);
  if (!leaf)
    return 0;
  return ReadI32(leaf, OFF_CLEAF_CONTENTS);
}

int LeafCluster(int leafIdx) {
  const uint8_t *leaf = leaf_at(leafIdx);
  if (!leaf)
    return -1;
  int16_t v;
  memcpy(&v, leaf + OFF_CLEAF_CLUSTER, 2);
  return (int)v;
}

int LeafArea(int leafIdx) {
  const uint8_t *leaf = leaf_at(leafIdx);
  if (!leaf)
    return -1;
  uint16_t v = ReadU16(leaf, OFF_CLEAF_AREA_FLAGS);
  return (int)(v & 0x1FF); // low 9 bits = area
}

int LeafFlags(int leafIdx) {
  const uint8_t *leaf = leaf_at(leafIdx);
  if (!leaf)
    return -1;
  uint16_t v = ReadU16(leaf, OFF_CLEAF_AREA_FLAGS);
  return (int)((v >> 9) & 0x7F); // high 7 bits = flags
}

int LeafFirstFace(int leafIdx) {
  const uint8_t *leaf = leaf_at(leafIdx);
  if (!leaf)
    return -1;
  return (int)ReadU16(leaf, OFF_CLEAF_FIRSTLEAFFACE);
}

int LeafNumFaces(int leafIdx) {
  const uint8_t *leaf = leaf_at(leafIdx);
  if (!leaf)
    return -1;
  return (int)ReadU16(leaf, OFF_CLEAF_NUMLEAFFACES);
}

bool LeafBounds(int leafIdx, float mins[3], float maxs[3]) {
  if (leafIdx < 0 || leafIdx >= GetNumLeaves())
    return false;
  // Build leaf cache lazily under lock. Independent of brush cache:
  // walks BSP tree from root and splits AABB on axis-aligned planes.
  std::lock_guard<std::mutex> lk(g_brushCacheMutex);
  if (!g_leafCacheBuilt)
    BuildLeafCache();
  if (!g_leafCacheBuilt || (size_t)leafIdx >= g_leafCache.size())
    return false;
  const LeafCacheEntry &le = g_leafCache[leafIdx];
  if (!le.valid)
    return false;
  memcpy(mins, le.mins, 12);
  memcpy(maxs, le.maxs, 12);
  return true;
}

bool NodeBounds(int nodeIdx, float mins[3], float maxs[3]) {
  if (nodeIdx < 0 || nodeIdx >= GetNumNodes())
    return false;
  std::lock_guard<std::mutex> lk(g_brushCacheMutex);
  if (!g_leafCacheBuilt)
    BuildLeafCache();
  if (!g_leafCacheBuilt || (size_t)nodeIdx >= g_nodeCache.size())
    return false;
  const LeafCacheEntry &ne = g_nodeCache[nodeIdx];
  if (!ne.valid)
    return false;
  memcpy(mins, ne.mins, 12);
  memcpy(maxs, ne.maxs, 12);
  return true;
}

// Node accessors (manual BSP walking)
bool NodePlane(int nodeIdx, float normal[3], float &dist) {
  const uint8_t *node = node_at(nodeIdx);
  if (!node)
    return false;
  const uint8_t *plane =
      reinterpret_cast<const uint8_t *>(ReadPtr(node, OFF_CNODE_PLANE));
  if (!plane)
    return false;
  ReadPlane(plane, normal, dist);
  return true;
}

bool NodeChildren(int nodeIdx, int &leftChild, int &rightChild) {
  const uint8_t *node = node_at(nodeIdx);
  if (!node)
    return false;
  leftChild = ReadI32(node, OFF_CNODE_CHILDREN + 0);
  rightChild = ReadI32(node, OFF_CNODE_CHILDREN + 4);
  return true;
}

// Plane table access
bool PlaneAt(int planeIdx, float normal[3], float &dist) {
  if (!g_pBSPData || planeIdx < 0 || planeIdx >= GetNumPlanes())
    return false;
  const uint8_t *table =
      reinterpret_cast<const uint8_t *>(ReadPtr(g_pBSPData, OFF_MAP_PLANES));
  if (!table)
    return false;
  const uint8_t *plane = table + (size_t)planeIdx * SZ_CPLANE;
  ReadPlane(plane, normal, dist);
  return true;
}

int PlaneType(int planeIdx) {
  if (!g_pBSPData || planeIdx < 0 || planeIdx >= GetNumPlanes())
    return -1;
  const uint8_t *table =
      reinterpret_cast<const uint8_t *>(ReadPtr(g_pBSPData, OFF_MAP_PLANES));
  if (!table)
    return -1;
  return (int)*(table + (size_t)planeIdx * SZ_CPLANE + OFF_CPLANE_TYPE);
}

// Submodel (cmodel_t) accessors.
static const uint8_t *cmodel_at(int idx) {
  return TableEntry(OFF_MAP_CMODELS, idx, GetNumCModels(), SZ_CMODEL);
}

bool CModelBounds(int idx, float mins[3], float maxs[3]) {
  const uint8_t *m = cmodel_at(idx);
  if (!m)
    return false;
  mins[0] = ReadF32(m, OFF_CMODEL_MINS + 0);
  mins[1] = ReadF32(m, OFF_CMODEL_MINS + 4);
  mins[2] = ReadF32(m, OFF_CMODEL_MINS + 8);
  maxs[0] = ReadF32(m, OFF_CMODEL_MAXS + 0);
  maxs[1] = ReadF32(m, OFF_CMODEL_MAXS + 4);
  maxs[2] = ReadF32(m, OFF_CMODEL_MAXS + 8);
  return true;
}

bool CModelOrigin(int idx, float origin[3]) {
  const uint8_t *m = cmodel_at(idx);
  if (!m)
    return false;
  origin[0] = ReadF32(m, OFF_CMODEL_ORIGIN + 0);
  origin[1] = ReadF32(m, OFF_CMODEL_ORIGIN + 4);
  origin[2] = ReadF32(m, OFF_CMODEL_ORIGIN + 8);
  return true;
}

int CModelHeadnode(int idx) {
  const uint8_t *m = cmodel_at(idx);
  if (!m)
    return -1;
  return ReadI32(m, OFF_CMODEL_HEADNODE);
}

// Box brush (cboxbrush_t) accessors - SIMD-optimized axis-aligned brushes.
static const uint8_t *boxbrush_at(int idx) {
  return TableEntry(OFF_MAP_BOXBRUSHES, idx, GetNumBoxBrushes(), SZ_CBOXBRUSH);
}

bool BoxBrushBounds(int idx, float mins[3], float maxs[3]) {
  const uint8_t *b = boxbrush_at(idx);
  if (!b)
    return false;
  mins[0] = ReadF32(b, OFF_CBOXBRUSH_MINS + 0);
  mins[1] = ReadF32(b, OFF_CBOXBRUSH_MINS + 4);
  mins[2] = ReadF32(b, OFF_CBOXBRUSH_MINS + 8);
  maxs[0] = ReadF32(b, OFF_CBOXBRUSH_MAXS + 0);
  maxs[1] = ReadF32(b, OFF_CBOXBRUSH_MAXS + 4);
  maxs[2] = ReadF32(b, OFF_CBOXBRUSH_MAXS + 8);
  return true;
}

int BoxBrushOriginalBrush(int idx) {
  const uint8_t *b = boxbrush_at(idx);
  if (!b)
    return -1;
  return ReadI32(b, OFF_CBOXBRUSH_BRUSHNUM);
}

bool BoxBrushSurfaceIndex(int idx, int outSurf[6]) {
  const uint8_t *b = boxbrush_at(idx);
  if (!b)
    return false;
  for (int i = 0; i < 6; ++i)
    outSurf[i] = (int)ReadU16(b, OFF_CBOXBRUSH_SURFIDX + i * 2);
  return true;
}

int BoxBrushContents(int idx) {
  int orig = BoxBrushOriginalBrush(idx);
  if (orig < 0)
    return 0;
  return GetBrushContents(orig);
}

// Build (once per map) the brush-index -> isBoxBrush bit set from the
// cboxbrush_t table's originalBrush column.
static void EnsureBoxBrushAuthSet() {
  int nbox = GetNumBoxBrushes();
  if (g_boxBrushAuthBuiltFor == nbox && !g_boxBrushAuth.empty())
    return;
  if (g_boxBrushAuthBuiltFor == nbox && nbox == 0)
    return; // built, legitimately empty
  int nbrush = GetNumBrushes();
  g_boxBrushAuth.assign(nbrush > 0 ? nbrush : 0, false);
  g_brushToBoxIdx.assign(nbrush > 0 ? nbrush : 0, -1);
  for (int i = 0; i < nbox; ++i) {
    int orig = BoxBrushOriginalBrush(i);
    if (orig >= 0 && orig < nbrush) {
      g_boxBrushAuth[orig] = true;
      g_brushToBoxIdx[orig] = i;
    }
  }
  g_boxBrushAuthBuiltFor = nbox;
}

// cboxbrush_t index for a brush index, or -1 if the brush is not box-optimized.
static int BoxIndexForBrush(int brushIdx) {
  if (brushIdx < 0)
    return -1;
  EnsureBoxBrushAuthSet();
  if (brushIdx >= (int)g_brushToBoxIdx.size())
    return -1;
  return g_brushToBoxIdx[brushIdx];
}

bool BrushIsBoxAuth(int brushIdx) {
  if (brushIdx < 0)
    return false;
  EnsureBoxBrushAuthSet();
  if (brushIdx >= (int)g_boxBrushAuth.size())
    return false;
  return g_boxBrushAuth[brushIdx];
}

// Exact brush geometry / collision (plane-accurate)
namespace {
inline float DotV(const float a[3], const float b[3]) {
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
inline void CrossV(const float a[3], const float b[3], float o[3]) {
  o[0] = a[1] * b[2] - a[2] * b[1];
  o[1] = a[2] * b[0] - a[0] * b[2];
  o[2] = a[0] * b[1] - a[1] * b[0];
}
inline void NormV(float a[3]) {
  float l = std::sqrt(DotV(a, a));
  if (l > 1e-9f) {
    a[0] /= l;
    a[1] /= l;
    a[2] /= l;
  }
}

// Resolve a brush's side count / first side plus the brush header pointer.
// Returns nullptr (and leaves out-params untouched) when brushIdx is invalid.
const uint8_t *brush_sides(int brushIdx, uint16_t &numsides, uint16_t &first) {
  const uint8_t *b = brush_at(brushIdx);
  if (!b)
    return nullptr;
  numsides = BrushSideCount(b);
  first = ReadU16(b, OFF_CBRUSH_FIRSTBRUSHSIDE);
  return b;
}

// Read side k's plane normal+dist (k is brush-relative). false if unresolved.
bool side_plane(uint16_t first, int k, float n[3], float &d) {
  const uint8_t *side = brushside_at((int)first + k);
  if (!side)
    return false;
  const uint8_t *plane =
      reinterpret_cast<const uint8_t *>(ReadPtr(side, OFF_CBRUSHSIDE_PLANE));
  if (!plane)
    return false;
  ReadPlane(plane, n, d);
  return true;
}
} // namespace

bool PointInBrush(int brushIdx, const float pos[3]) {
  // Box-optimized brushes have stripped/bevel-padded sides, so their planes are
  // not usable. Their solid IS the SIMD box AABB, test against that directly.
  int bi = BoxIndexForBrush(brushIdx);
  if (bi >= 0) {
    float mn[3], mx[3];
    if (!BoxBrushBounds(bi, mn, mx))
      return false;
    return pos[0] >= mn[0] && pos[0] <= mx[0] && pos[1] >= mn[1] &&
           pos[1] <= mx[1] && pos[2] >= mn[2] && pos[2] <= mx[2];
  }
  uint16_t numsides, first;
  if (!brush_sides(brushIdx, numsides, first) || numsides == 0)
    return false;
  for (uint16_t i = 0; i < numsides; ++i) {
    float n[3], d;
    if (!side_plane(first, i, n, d))
      return false;
    if (DotV(n, pos) - d > 0.0f)
      return false; // strictly in front of a side -> outside
  }
  return true;
}

int PointContentsBrushes(const float pos[3]) {
  int leaf = LeafAtPoint(pos);
  if (leaf < 0)
    return 0;
  int brushes[1024];
  int n = LeafBrushes(leaf, brushes, 1024);
  if (n <= 0)
    return 0;
  int contents = 0;
  for (int i = 0; i < n; ++i)
    if (PointInBrush(brushes[i], pos))
      contents |= GetBrushContents(brushes[i]);
  return contents;
}

bool BrushColumnSpan(int brushIdx, float x, float y, float &zMin, float &zMax) {
  // Box-optimized brush: authoritative extent is the SIMD box AABB
  //(an axis box is exactly its AABB), not the stripped cbrush_t planes.
  int bi = BoxIndexForBrush(brushIdx);
  if (bi >= 0) {
    float mn[3], mx[3];
    if (!BoxBrushBounds(bi, mn, mx))
      return false;
    // 0.1u XY slack mirrors FindBrushPairAtSeam's containment tolerance.
    if (x < mn[0] - 0.1f || x > mx[0] + 0.1f || y < mn[1] - 0.1f ||
        y > mx[1] + 0.1f)
      return false;
    zMin = mn[2];
    zMax = mx[2];
    return true;
  }
  uint16_t numsides, first;
  if (!brush_sides(brushIdx, numsides, first) || numsides == 0)
    return false;
  // Line L(t) = (x, y, t).
  // Each side n.L <= d becomes n.z*t <= d - n.x*x - n.y*y.
  float lo = -1e30f, hi = 1e30f;
  for (uint16_t i = 0; i < numsides; ++i) {
    float n[3], d;
    if (!side_plane(first, i, n, d))
      return false;
    float rhs = d - n[0] * x - n[1] * y;
    if (n[2] > 1e-6f) {
      float t = rhs / n[2]; // upper bound
      if (t < hi)
        hi = t;
    } else if (n[2] < -1e-6f) {
      float t = rhs / n[2]; // lower bound (divide by negative flips)
      if (t > lo)
        lo = t;
    } else {
      // Vertical plane: column is outside the XY footprint if rhs < 0.
      // 0.1u slack matches FindBrushPairAtSeam's XY containment tolerance.
      if (rhs < -0.1f)
        return false;
    }
  }
  if (lo > hi)
    return false;
  zMin = lo;
  zMax = hi;
  return true;
}

bool BrushTopZAt(int brushIdx, float x, float y, float &z) {
  float lo, hi;
  if (!BrushColumnSpan(brushIdx, x, y, lo, hi))
    return false;
  z = hi;
  return true;
}

int BrushSideWinding(int brushIdx, int sideIdx, float *outVerts, int maxVerts) {
  if (!outVerts || maxVerts <= 0)
    return 0;
  uint16_t numsides, first;
  if (!brush_sides(brushIdx, numsides, first) || numsides == 0)
    return 0;
  if (sideIdx < 0 || sideIdx >= (int)numsides)
    return 0;

  float n[3], d;
  if (!side_plane(first, sideIdx, n, d))
    return 0;

  // Seed a huge quad on the side's plane, oriented by an axis least parallel
  // to the normal, then clip it down by every other side's half-space.
  float seed[3] = {0, 0, 0};
  int least = 0;
  for (int k = 1; k < 3; ++k)
    if (std::fabs(n[k]) < std::fabs(n[least]))
      least = k;
  seed[least] = 1.0f;
  float right[3], up[3];
  CrossV(seed, n, right);
  NormV(right);
  CrossV(n, right, up);
  NormV(up);

  const float R = 131072.0f; // > 4 * MAX_COORD (32768) to span any CSGO map
  float center[3] = {n[0] * d, n[1] * d, n[2] * d};
  struct V3 {
    float v[3];
  };
  std::vector<V3> poly;
  poly.reserve(8);
  for (int s = 0; s < 4; ++s) {
    // corners: (-,-), (+,-), (+,+), (-,+) about center in (right, up)
    float sr = (s == 1 || s == 2) ? R : -R;
    float su = (s >= 2) ? R : -R;
    V3 p;
    for (int k = 0; k < 3; ++k)
      p.v[k] = center[k] + right[k] * sr + up[k] * su;
    poly.push_back(p);
  }

  // Clip by each other side: keep the part with normal.p - dist <= 0.
  for (uint16_t j = 0; j < numsides && !poly.empty(); ++j) {
    if (j == (uint16_t)sideIdx)
      continue;
    float nj[3], dj;
    if (!side_plane(first, j, nj, dj))
      continue;
    std::vector<V3> out;
    out.reserve(poly.size() + 1);
    size_t cnt = poly.size();
    for (size_t a = 0; a < cnt; ++a) {
      const V3 &A = poly[a];
      const V3 &B = poly[(a + 1) % cnt];
      float da = DotV(nj, A.v) - dj;
      float db = DotV(nj, B.v) - dj;
      if (da <= 0.0f)
        out.push_back(A);
      if ((da <= 0.0f) != (db <= 0.0f)) {
        float t = da / (da - db);
        V3 p;
        for (int k = 0; k < 3; ++k)
          p.v[k] = A.v[k] + t * (B.v[k] - A.v[k]);
        out.push_back(p);
      }
    }
    poly.swap(out);
    if (poly.size() > 256) // pathological, bail to avoid runaway
      break;
  }

  int wrote = 0;
  for (size_t i = 0; i < poly.size() && wrote < maxVerts; ++i, ++wrote) {
    outVerts[wrote * 3 + 0] = poly[i].v[0];
    outVerts[wrote * 3 + 1] = poly[i].v[1];
    outVerts[wrote * 3 + 2] = poly[i].v[2];
  }
  return wrote;
}

int BrushClipBox(int brushIdx, const float start[3], const float end[3],
                 const float mins[3], const float maxs[3], float &fraction,
                 float normal[3], bool &startSolid) {
  fraction = 1.0f;
  normal[0] = normal[1] = normal[2] = 0.0f;
  startSolid = false;

  // Gather this brush's clipping planes. Box-optimized brushes have stripped /
  // bevel-padded sides, so synthesize the 6 axis planes from the SIMD box AABB
  // (the box is exactly that AABB). Other brushes use their cbrush_t sides.
  float planeN[256][3];
  float planeD[256];
  int nplanes = 0;
  int bi = BoxIndexForBrush(brushIdx);
  if (bi >= 0) {
    float mn[3], mx[3];
    if (!BoxBrushBounds(bi, mn, mx))
      return -1;
    const float axes[6][3] = {{1, 0, 0},  {-1, 0, 0}, {0, 1, 0},
                              {0, -1, 0}, {0, 0, 1},  {0, 0, -1}};
    const float dists[6] = {mx[0], -mn[0], mx[1], -mn[1], mx[2], -mn[2]};
    for (int i = 0; i < 6; ++i) {
      planeN[i][0] = axes[i][0];
      planeN[i][1] = axes[i][1];
      planeN[i][2] = axes[i][2];
      planeD[i] = dists[i];
    }
    nplanes = 6;
  } else {
    uint16_t numsides, first;
    if (!brush_sides(brushIdx, numsides, first) || numsides == 0)
      return -1;
    for (uint16_t i = 0; i < numsides && nplanes < 256; ++i) {
      float n[3], d;
      if (!side_plane(first, i, n, d))
        return -1;
      planeN[nplanes][0] = n[0];
      planeN[nplanes][1] = n[1];
      planeN[nplanes][2] = n[2];
      planeD[nplanes] = d;
      ++nplanes;
    }
  }

  const float DIST_EPSILON = 0.03125f; // 1/32, engine constant
  float enterfrac = -1.0f, leavefrac = 1.0f;
  float clipN[3] = {0, 0, 0};
  bool gotClip = false, getout = false, startout = false;

  for (int i = 0; i < nplanes; ++i) {
    const float *n = planeN[i];
    float d = planeD[i];
    // Push the plane out by the box extents (support point in -normal dir).
    float offset[3];
    for (int k = 0; k < 3; ++k)
      offset[k] = (n[k] < 0.0f) ? maxs[k] : mins[k];
    float dist = d - DotV(offset, n);
    float d1 = DotV(start, n) - dist;
    float d2 = DotV(end, n) - dist;
    if (d2 > 0.0f)
      getout = true; // endpoint outside this side
    if (d1 > 0.0f)
      startout = true; // startpoint outside this side
    // Completely in front of a side and not crossing inward -> never hits.
    if (d1 > 0.0f && (d2 >= DIST_EPSILON || d2 >= d1))
      return 0;
    if (d1 <= 0.0f && d2 <= 0.0f)
      continue;
    if (d1 > d2) { // entering the brush across this side
      float f = (d1 - DIST_EPSILON) / (d1 - d2);
      if (f < 0.0f)
        f = 0.0f;
      if (f > enterfrac) {
        enterfrac = f;
        clipN[0] = n[0];
        clipN[1] = n[1];
        clipN[2] = n[2];
        gotClip = true;
      }
    } else { // leaving
      float f = (d1 + DIST_EPSILON) / (d1 - d2);
      if (f > 1.0f)
        f = 1.0f;
      if (f < leavefrac)
        leavefrac = f;
    }
  }

  if (!startout) {
    // Started inside the brush.
    startSolid = true;
    fraction = getout ? 1.0f : 0.0f; // allsolid (never exits) -> blocked at 0
    return 1;
  }
  if (enterfrac < leavefrac && enterfrac > -1.0f && gotClip) {
    if (enterfrac < 0.0f)
      enterfrac = 0.0f;
    fraction = enterfrac;
    normal[0] = clipN[0];
    normal[1] = clipN[1];
    normal[2] = clipN[2];
    return 1;
  }
  return 0;
}

int BrushSideOrder(int brushIdx, char *buf, int maxlen) {
  if (!buf || maxlen <= 0)
    return 0;
  buf[0] = '\0';
  uint16_t numsides, first;
  if (!brush_sides(brushIdx, numsides, first) || numsides == 0)
    return 0;
  int off = 0;
  off += snprintf(buf + off, maxlen - off, "brush #%d: %d sides (engine order)",
                  brushIdx, (int)numsides);
  static const char *kAxis[] = {"+X", "-X", "+Y", "-Y", "+Z", "-Z", "slope"};
  for (uint16_t i = 0; i < numsides && off < maxlen - 1; ++i) {
    float n[3], d;
    if (!side_plane(first, i, n, d))
      continue;
    int axis = 6; // slope
    if (n[0] > 0.999f)
      axis = 0;
    else if (n[0] < -0.999f)
      axis = 1;
    else if (n[1] > 0.999f)
      axis = 2;
    else if (n[1] < -0.999f)
      axis = 3;
    else if (n[2] > 0.999f)
      axis = 4;
    else if (n[2] < -0.999f)
      axis = 5;
    int bevel = BrushSideBevel(brushIdx, i);
    int thin = BrushSideThin(brushIdx, i);
    int ptype = -1;
    {
      const uint8_t *side = brushside_at((int)first + i);
      if (side) {
        const uint8_t *plane = reinterpret_cast<const uint8_t *>(
            ReadPtr(side, OFF_CBRUSHSIDE_PLANE));
        if (plane)
          ptype = (int)*(plane + OFF_CPLANE_TYPE);
      }
    }
    off += snprintf(
        buf + off, maxlen - off,
        "\n [%d] %s n=(%.3f,%.3f,%.3f) d=%.3f type=%d bevel=%d thin=%d", i,
        kAxis[axis], n[0], n[1], n[2], d, ptype, bevel, thin);
  }
  return off;
}

// "High-level" pixelsurf
bool FindBrushPairAtSeam(const float samplePos[3], float seamZ, int &outLower,
                         int &outUpper) {
  outLower = -1;
  outUpper = -1;
  if (!g_pBSPData)
    return false;

  int n = GetNumBrushes();
  // Sanity: a valid map has hundreds-to-tens-of-thousands of brushes.
  // Anything outside [1, 1_000_000] means g_BSPData is pointing at garbage,
  // bail before dereferencing.
  if (n <= 0 || n > 1000000) {
    static bool warned = false;
    if (!warned) {
      warned = true;
      smutils->LogError(myself,
                        "FindBrushPairAtSeam: numbrushes=%d looks bogus, "
                        "g_BSPData=%p (sanity bail).",
                        n, (void *)g_pBSPData);
    }
    return false;
  }

  // Lock cache for the rest of this query (covers cache build + read).
  std::lock_guard<std::mutex> lk(g_brushCacheMutex);

  // Build per-map brush AABB cache on first call.
  // Auto-invalidate when the brush count changes (map switch).
  if (!g_brushCacheBuilt || (int)g_brushCache.size() != n)
    BuildBrushCache();
  if (!g_brushCacheBuilt)
    return false;

  int cacheN = (int)g_brushCache.size();
  for (int i = 0; i < cacheN; ++i) {
    const BrushCacheEntry &e = g_brushCache[i];
    if (!e.valid)
      continue;
    // XY AABB containment test (cheapest first).
    if (samplePos[0] < e.mins[0] - 0.1f || samplePos[0] > e.maxs[0] + 0.1f)
      continue;
    if (samplePos[1] < e.mins[1] - 0.1f || samplePos[1] > e.maxs[1] + 0.1f)
      continue;
    float top_diff = e.maxs[2] - seamZ;
    if (top_diff > -0.1f && top_diff < 0.1f && outLower < 0)
      outLower = i;
    float bot_diff = e.mins[2] - seamZ;
    if (bot_diff > -0.1f && bot_diff < 0.1f && outUpper < 0)
      outUpper = i;
    if (outLower >= 0 && outUpper >= 0)
      return true;
  }
  return (outLower >= 0 && outUpper >= 0);
}

// Box-table analog of FindBrushPairAtSeam for CSGO box-optimized walls.
// The leafbrush lump and the cbrush-indexed brush cache both miss these:
// box brushes are absent from the per-leaf leafbrush list, and several
// cboxbrush_t entries can share one originalBrush index, so they collapse into
// a single cache slot (only one survives -> no pair).
// Scan the cboxbrush_t table directly instead.
//   lower box: maxs.z ~= seamZ AND XY AABB contains samplePos
//   upper box: mins.z ~= seamZ AND XY AABB contains samplePos
// Returns BOX-TABLE indices (NOT cbrush indices). false if no flush pair.
// Caller applies contents + visit-order policy (lower box index < upper).
bool FindBoxBrushPairAtSeam(const float samplePos[3], float seamZ,
                            int &outLower, int &outUpper) {
  outLower = -1;
  outUpper = -1;
  if (!g_pBSPData)
    return false;
  int nb = GetNumBoxBrushes();
  if (nb <= 0)
    return false;
  for (int i = 0; i < nb; ++i) {
    float mn[3], mx[3];
    if (!BoxBrushBounds(i, mn, mx))
      continue;
    // XY AABB containment (0.1u slack mirrors FindBrushPairAtSeam).
    if (samplePos[0] < mn[0] - 0.1f || samplePos[0] > mx[0] + 0.1f)
      continue;
    if (samplePos[1] < mn[1] - 0.1f || samplePos[1] > mx[1] + 0.1f)
      continue;
    float top_diff = mx[2] - seamZ;
    if (top_diff > -0.1f && top_diff < 0.1f && outLower < 0)
      outLower = i;
    float bot_diff = mn[2] - seamZ;
    if (bot_diff > -0.1f && bot_diff < 0.1f && outUpper < 0)
      outUpper = i;
    if (outLower >= 0 && outUpper >= 0)
      return true;
  }
  return (outLower >= 0 && outUpper >= 0);
}

// Texturebug overhang.
// Scans the cboxbrush_t table for a SOLID box brush whose XY AABB contains
// samplePos, whose underside (mins.z) is open to air,
// and which has >=1 exposed vertical wall face.
// Returns the BOX-TABLE index plus the hugged wall
// (face axis+sign, world coord), the underside z, and the brush height.
// Among exposed lateral faces, picks the one whose outward normal points toward
// samplePos. false if none.
// A column usually pierces several qualifying boxes,
// the winner is the one whose bottom edge is nearest BELOW samplePos.z
// (the next edge a falling player crosses).
// If none lie below, nearest above is returned instead.
bool FindBoxBrushOverhang(const float samplePos[3], int &outBoxIdx,
                          int &outFace, float &outWallCoord, float &outBottomZ,
                          float &outHeight) {
  outBoxIdx = -1;
  outFace = -1;
  outWallCoord = 0.f;
  outBottomZ = 0.f;
  outHeight = 0.f;
  if (!g_pBSPData)
    return false;
  const int CONTENTS_PLAYERCOLLIDE = 0x1 | 0x10000;
  const float PROBE = 1.0f; // clear the eps-bevel padding box brushes carry
  int nb = GetNumBoxBrushes();
  if (nb <= 0)
    return false;

  // Best-below / best-above tracking (see selection note above).
  int bestBelowIdx = -1, bestBelowFace = -1;
  float bestBelowCoord = 0.f, bestBelowBz = 0.f, bestBelowH = 0.f;
  int bestAboveIdx = -1, bestAboveFace = -1;
  float bestAboveCoord = 0.f, bestAboveBz = 0.f, bestAboveH = 0.f;

  for (int i = 0; i < nb; ++i) {
    float mn[3], mx[3];
    if (!BoxBrushBounds(i, mn, mx))
      continue;
    // XY AABB containment (0.1u slack mirrors FindBoxBrushPairAtSeam).
    if (samplePos[0] < mn[0] - 0.1f || samplePos[0] > mx[0] + 0.1f)
      continue;
    if (samplePos[1] < mn[1] - 0.1f || samplePos[1] > mx[1] + 0.1f)
      continue;
    if ((BoxBrushContents(i) & CONTENTS_PLAYERCOLLIDE) == 0)
      continue; // player-collidable box brushes only (solid or playerclip)

    // Underside must be open: the player has to fall past the bottom face.
    float cx = samplePos[0] < mn[0]
                   ? mn[0]
                   : (samplePos[0] > mx[0] ? mx[0] : samplePos[0]);
    float cy = samplePos[1] < mn[1]
                   ? mn[1]
                   : (samplePos[1] > mx[1] ? mx[1] : samplePos[1]);
    float probeBelow[3] = {cx, cy, mn[2] - PROBE};
    if (PointContentsBrushes(probeBelow) & CONTENTS_PLAYERCOLLIDE)
      continue; // underside buried -> nothing to drop past

    float midZ = (mn[2] + mx[2]) * 0.5f;

    // Pick an exposed lateral wall. Two passes: first prefer the face whose
    // outward normal points toward samplePos (the side the player approaches);
    // if none of those are exposed, take the first exposed lateral face.
    int bestFace = -1;
    float bestCoord = 0.f;
    for (int pass = 0; pass < 2 && bestFace < 0; ++pass) {
      for (int face = 0; face < 4; ++face) {
        int axis = face >> 1; // 0 = X, 1 = Y
        int otherAxis = axis ^ 1;
        bool positive = (face & 1); // odd = +axis face
        float wallCoord = positive ? mx[axis] : mn[axis];

        if (pass == 0) {
          // require the outward normal to point toward samplePos on this axis
          bool sampleOutward = positive ? (samplePos[axis] > mx[axis])
                                        : (samplePos[axis] < mn[axis]);
          if (!sampleOutward)
            continue;
        }

        // probe one unit outside the face center at mid height
        float probeOut[3];
        probeOut[axis] = wallCoord + (positive ? PROBE : -PROBE);
        probeOut[otherAxis] = (mn[otherAxis] + mx[otherAxis]) * 0.5f;
        probeOut[2] = midZ;
        if (PointContentsBrushes(probeOut) & CONTENTS_PLAYERCOLLIDE)
          continue; // wall buried behind neighbor geometry
        bestFace = face;
        bestCoord = wallCoord;
        break;
      }
    }
    if (bestFace < 0)
      continue; // no exposed lateral wall on this box

    // Qualifies. Slot into below/above by bottom edge vs samplePos.z
    // (0.1 slack so standing exactly on the edge still counts as "below").
    if (mn[2] <= samplePos[2] + 0.1f) {
      if (bestBelowIdx < 0 || mn[2] > bestBelowBz) {
        bestBelowIdx = i;
        bestBelowFace = bestFace;
        bestBelowCoord = bestCoord;
        bestBelowBz = mn[2];
        bestBelowH = mx[2] - mn[2];
      }
    } else {
      if (bestAboveIdx < 0 || mn[2] < bestAboveBz) {
        bestAboveIdx = i;
        bestAboveFace = bestFace;
        bestAboveCoord = bestCoord;
        bestAboveBz = mn[2];
        bestAboveH = mx[2] - mn[2];
      }
    }
  }

  if (bestBelowIdx >= 0) {
    outBoxIdx = bestBelowIdx;
    outFace = bestBelowFace;
    outWallCoord = bestBelowCoord;
    outBottomZ = bestBelowBz;
    outHeight = bestBelowH;
    return true;
  }
  if (bestAboveIdx >= 0) {
    outBoxIdx = bestAboveIdx;
    outFace = bestAboveFace;
    outWallCoord = bestAboveCoord;
    outBottomZ = bestAboveBz;
    outHeight = bestAboveH;
    return true;
  }
  return false;
}

bool BoxBrushOverhangWindow(const float playerPos[3], const float vel[3],
                            float hullHeight, int &outBoxIdx, int &outFace,
                            float &outWallCoord, float &outBottomZ,
                            float &outHeight, float &outMaxVPerp,
                            float &outVPerp) {
  outBoxIdx = -1;
  outFace = -1;
  outWallCoord = 0.f;
  outBottomZ = 0.f;
  outHeight = 0.f;
  outMaxVPerp = 0.f;
  outVPerp = 0.f;

  if (!FindBoxBrushOverhang(playerPos, outBoxIdx, outFace, outWallCoord,
                            outBottomZ, outHeight))
    return false;

  float vz = vel[2];
  if (vz >= 0.f) // must be falling
    return false;

  const float DIST_EPSILON = 0.03125f;
  int wallAxis = outFace >> 1; // 0=X 1=Y
  float vPerp = fabsf(vel[wallAxis]);
  float maxVPerp = fabsf(vz) * DIST_EPSILON / (outHeight + hullHeight);

  outMaxVPerp = maxVPerp;
  outVPerp = vPerp;

  return vPerp < maxVPerp;
}

bool LeafBrushPairAtSeam(const float samplePos[3], float seamZ, int &outLower,
                         int &outUpper, int &outLeaf, int &outLowerPos,
                         int &outUpperPos) {
  outLeaf = -1;
  outLowerPos = -1;
  outUpperPos = -1;
  // Resolve the brush pair (own lock taken inside FindBrushPairAtSeam).
  FindBrushPairAtSeam(samplePos, seamZ, outLower, outUpper);
  if (outLower < 0 || outUpper < 0)
    return false;

  // Sample the leaf just below the seam (inside the lower brush's extent).
  float leafSample[3] = {samplePos[0], samplePos[1], seamZ - 0.5f};
  outLeaf = LeafAtPoint(leafSample);
  if (outLeaf < 0)
    return false;

  // Find each brush's position in the leaf's ordered brush list.
  // 1024 mirrors the natives-layer LeafBrushes clamp; leaves never approach it.
  int brushes[1024];
  int n = LeafBrushes(outLeaf, brushes, (int)(sizeof(brushes) / sizeof(int)));
  if (n < 0)
    return false;
  for (int i = 0; i < n; ++i) {
    if (brushes[i] == outLower && outLowerPos < 0)
      outLowerPos = i;
    if (brushes[i] == outUpper && outUpperPos < 0)
      outUpperPos = i;
  }
  // Surfable ordering: both in this leaf, lower visited first.
  return (outLowerPos >= 0 && outUpperPos >= 0 && outLowerPos < outUpperPos);
}

// Brush AABB cache
// Populate a brush AABB cache vector: plane-derived bounds + contents per
// brush, then fill any box-optimized brushes from the cboxbrush_t table.
// Shared by the synchronous and async builders so both produce identical
// caches. Leaves `out` empty (=> caller treats cache as unbuilt) on bad counts.
static void PopulateBrushCache(std::vector<BrushCacheEntry> &out) {
  out.clear();
  int n = GetNumBrushes();
  if (n <= 0 || n > 1000000)
    return;
  out.resize(n);
  for (int i = 0; i < n; ++i) {
    BrushCacheEntry &e = out[i];
    e.valid = GetBrushBounds(i, e.mins, e.maxs);
    e.contents = e.valid ? GetBrushContents(i) : 0;
  }

  // CSGO moves axis-aligned box brushes into a parallel cboxbrush_t SIMD table.
  // Those brushes can have their planar cbrush_t sides stripped or padded with
  // bevels, so GetBrushBounds above yields an invalid AABB for them and they
  // drop out of the cache. That is exactly the bottom brush of a pixelsurf, so
  // FindBrushPairAtSeam would never find the pair. Fill any such gaps from the
  // authoritative box-table bounds so box-brush seams become findable.
  int nb = GetNumBoxBrushes();
  for (int i = 0; i < nb; ++i) {
    int orig = BoxBrushOriginalBrush(i);
    if (orig < 0 || orig >= n)
      continue;
    BrushCacheEntry &e = out[orig];
    if (e.valid)
      continue; // keep plane-derived bounds when we already have them
    float bmins[3], bmaxs[3];
    if (BoxBrushBounds(i, bmins, bmaxs)) {
      memcpy(e.mins, bmins, sizeof(bmins));
      memcpy(e.maxs, bmaxs, sizeof(bmaxs));
      e.contents = BoxBrushContents(i);
      e.valid = true;
    }
  }
}

static void BuildBrushCache() {
  PopulateBrushCache(g_brushCache);
  g_brushCacheBuilt = !g_brushCache.empty();
}

// Per-map leaf AABB cache via BSP tree descent.
// Engine doesn't preserve per-leaf bounds (cleaf_t has no mins/maxs), and
// unioning leafbrush AABBs is wrong: Source maps reference huge world-hull
// SOLID brushes from most leaves, so the union always collapses to that
// filler's AABB. Walk from root carrying current AABB, split by axis-aligned
// node planes, write final box on leaf hit. Oblique planes don't tighten
// (leaf bounds remain a superset of the true cell, but bounded by every
// axis-aligned split along the path).
static void BuildLeafCache() {
  g_leafCache.clear();
  g_nodeCache.clear();
  g_leafCacheBuilt = false;
  int nleaf = GetNumLeaves();
  int nnode = GetNumNodes();
  if (nleaf <= 0 || nleaf > 200000 || nnode <= 0)
    return;

  // Seed bounds from world cmodel (cmodel[0]).
  float world_mins[3], world_maxs[3];
  if (!CModelBounds(0, world_mins, world_maxs))
    return;

  g_leafCache.resize(nleaf);
  for (LeafCacheEntry &le : g_leafCache) {
    le.valid = false;
    le.mins[0] = le.mins[1] = le.mins[2] = 1e30f;
    le.maxs[0] = le.maxs[1] = le.maxs[2] = -1e30f;
  }
  g_nodeCache.resize(nnode);
  for (LeafCacheEntry &ne : g_nodeCache) {
    ne.valid = false;
    ne.mins[0] = ne.mins[1] = ne.mins[2] = 1e30f;
    ne.maxs[0] = ne.maxs[1] = ne.maxs[2] = -1e30f;
  }

  struct Frame {
    int node;
    float mins[3];
    float maxs[3];
  };
  std::vector<Frame> stack;
  stack.reserve(64);
  Frame root;
  root.node = 0;
  memcpy(root.mins, world_mins, 12);
  memcpy(root.maxs, world_maxs, 12);
  stack.push_back(root);

  while (!stack.empty()) {
    Frame f = stack.back();
    stack.pop_back();

    if (f.node < 0) {
      int leafIdx = -1 - f.node;
      if (leafIdx >= 0 && leafIdx < nleaf) {
        LeafCacheEntry &le = g_leafCache[leafIdx];
        memcpy(le.mins, f.mins, 12);
        memcpy(le.maxs, f.maxs, 12);
        le.valid = true;
      }
      continue;
    }
    if (f.node >= nnode)
      continue;

    // Record this node's AABB (the parent-propagated box at this split).
    LeafCacheEntry &ne = g_nodeCache[f.node];
    memcpy(ne.mins, f.mins, 12);
    memcpy(ne.maxs, f.maxs, 12);
    ne.valid = true;

    float normal[3], dist;
    if (!NodePlane(f.node, normal, dist))
      continue;
    int leftChild, rightChild;
    if (!NodeChildren(f.node, leftChild, rightChild))
      continue;

    Frame fr = f;
    fr.node = leftChild;
    Frame bk = f;
    bk.node = rightChild;

    // Tighten on axis-aligned splits. Source PLANE_X/Y/Z always have
    // positive-axis normals, but check sign defensively.
    for (int ax = 0; ax < 3; ++ax) {
      if (std::fabs(normal[ax]) > 0.999f) {
        if (normal[ax] > 0.0f) {
          if (fr.mins[ax] < dist)
            fr.mins[ax] = dist;
          if (bk.maxs[ax] > dist)
            bk.maxs[ax] = dist;
        } else {
          float d = -dist;
          if (fr.maxs[ax] > d)
            fr.maxs[ax] = d;
          if (bk.mins[ax] < d)
            bk.mins[ax] = d;
        }
        break;
      }
    }

    stack.push_back(fr);
    stack.push_back(bk);
  }

  g_leafCacheBuilt = true;
}

void RebuildCache() {
  std::lock_guard<std::mutex> lk(g_brushCacheMutex);
  g_brushCache.clear();
  g_brushCacheBuilt = false;
  g_leafCache.clear();
  g_nodeCache.clear();
  g_leafCacheBuilt = false;
  BuildBrushCache();
}

// Off-main-thread cache build. Returns immediately.
// Readers see g_brushCacheBuilt=false until the worker swaps the fresh
// vector in under lock. A second call while a build is in flight is a no-op.
void RebuildCacheAsync() {
  if (g_asyncBuildInProgress.exchange(true))
    return; // already building

  // Join any prior worker that may have completed but not been joined yet.
  if (g_asyncBuildThread.joinable())
    g_asyncBuildThread.join();

  {
    std::lock_guard<std::mutex> lk(g_brushCacheMutex);
    g_brushCache.clear();
    g_brushCacheBuilt = false;
    g_leafCache.clear();
    g_nodeCache.clear();
    g_leafCacheBuilt = false;
  }

  g_asyncBuildThread = std::thread([] {
    // Build into a local vector first so readers see no partial state.
    // Same path as the sync build, incl. the cboxbrush_t gap-fill.
    std::vector<BrushCacheEntry> local;
    PopulateBrushCache(local);
    {
      std::lock_guard<std::mutex> lk(g_brushCacheMutex);
      g_brushCache.swap(local);
      g_brushCacheBuilt = !g_brushCache.empty();
    }
    g_asyncBuildInProgress.store(false);
  });
}

bool CacheIsBuilding() { return g_asyncBuildInProgress.load(); }

} // namespace BSPData
