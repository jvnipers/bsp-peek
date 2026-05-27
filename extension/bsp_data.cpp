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

// Forward decls - built lazily by various accessors below.
static void BuildBrushCache();
static void BuildLeafCache();

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
}

void OnMapStart() {
  std::lock_guard<std::mutex> lk(g_brushCacheMutex);
  g_brushCache.clear();
  g_brushCacheBuilt = false;
  g_leafCache.clear();
  g_nodeCache.clear();
  g_leafCacheBuilt = false;
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

// Internal helpers - table-relative pointer resolution
static const uint8_t *brush_at(int idx) {
  if (!g_pBSPData || idx < 0 || idx >= GetNumBrushes())
    return nullptr;
  const uint8_t *table =
      reinterpret_cast<const uint8_t *>(ReadPtr(g_pBSPData, OFF_MAP_BRUSHES));
  if (!table)
    return nullptr;
  return table + (size_t)idx * SZ_CBRUSH;
}

static const uint8_t *brushside_at(int idx) {
  if (!g_pBSPData || idx < 0 || idx >= GetNumBrushSides())
    return nullptr;
  const uint8_t *table = reinterpret_cast<const uint8_t *>(
      ReadPtr(g_pBSPData, OFF_MAP_BRUSHSIDES));
  if (!table)
    return nullptr;
  return table + (size_t)idx * SZ_CBRUSHSIDE;
}

static const uint8_t *leaf_at(int idx) {
  if (!g_pBSPData || OFF_MAP_LEAFS == 0 || idx < 0 || idx >= GetNumLeaves())
    return nullptr;
  const uint8_t *table =
      reinterpret_cast<const uint8_t *>(ReadPtr(g_pBSPData, OFF_MAP_LEAFS));
  if (!table)
    return nullptr;
  return table + (size_t)idx * SZ_CLEAF;
}

static const uint8_t *node_at(int idx) {
  if (!g_pBSPData || OFF_MAP_NODES == 0 || idx < 0 || idx >= GetNumNodes())
    return nullptr;
  const uint8_t *table =
      reinterpret_cast<const uint8_t *>(ReadPtr(g_pBSPData, OFF_MAP_NODES));
  if (!table)
    return nullptr;
  return table + (size_t)idx * SZ_CNODE;
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
  uint16_t numsides = ReadU16(b, OFF_CBRUSH_NUMSIDES);
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
  uint16_t numsides = ReadU16(b, OFF_CBRUSH_NUMSIDES);
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
  return (int)ReadU16(b, OFF_CBRUSH_NUMSIDES);
}

bool BrushSidePlane(int brushIdx, int sideIdx, float normal[3], float &dist) {
  const uint8_t *b = brush_at(brushIdx);
  if (!b)
    return false;
  uint16_t numsides = ReadU16(b, OFF_CBRUSH_NUMSIDES);
  uint16_t first = ReadU16(b, OFF_CBRUSH_FIRSTBRUSHSIDE);
  if (sideIdx < 0 || sideIdx >= (int)numsides)
    return false;
  const uint8_t *side = brushside_at((int)first + sideIdx);
  if (!side)
    return false;
  const uint8_t *plane =
      reinterpret_cast<const uint8_t *>(ReadPtr(side, OFF_CBRUSHSIDE_PLANE));
  if (!plane)
    return false;
  normal[0] = ReadF32(plane, OFF_CPLANE_NORMAL + 0);
  normal[1] = ReadF32(plane, OFF_CPLANE_NORMAL + 4);
  normal[2] = ReadF32(plane, OFF_CPLANE_NORMAL + 8);
  dist = ReadF32(plane, OFF_CPLANE_DIST);
  return true;
}

int BrushSideBevel(int brushIdx, int sideIdx) {
  const uint8_t *b = brush_at(brushIdx);
  if (!b)
    return -1;
  uint16_t numsides = ReadU16(b, OFF_CBRUSH_NUMSIDES);
  uint16_t first = ReadU16(b, OFF_CBRUSH_FIRSTBRUSHSIDE);
  if (sideIdx < 0 || sideIdx >= (int)numsides)
    return -1;
  const uint8_t *side = brushside_at((int)first + sideIdx);
  if (!side)
    return -1;
  return (int)*(side + OFF_CBRUSHSIDE_BEVEL); // byte read
}

int BrushSideThin(int brushIdx, int sideIdx) {
  const uint8_t *b = brush_at(brushIdx);
  if (!b)
    return -1;
  uint16_t numsides = ReadU16(b, OFF_CBRUSH_NUMSIDES);
  uint16_t first = ReadU16(b, OFF_CBRUSH_FIRSTBRUSHSIDE);
  if (sideIdx < 0 || sideIdx >= (int)numsides)
    return -1;
  const uint8_t *side = brushside_at((int)first + sideIdx);
  if (!side)
    return -1;
  return (int)*(side + OFF_CBRUSHSIDE_THIN);
}

int BrushSideTexInfo(int brushIdx, int sideIdx) {
  const uint8_t *b = brush_at(brushIdx);
  if (!b)
    return -1;
  uint16_t numsides = ReadU16(b, OFF_CBRUSH_NUMSIDES);
  uint16_t first = ReadU16(b, OFF_CBRUSH_FIRSTBRUSHSIDE);
  if (sideIdx < 0 || sideIdx >= (int)numsides)
    return -1;
  const uint8_t *side = brushside_at((int)first + sideIdx);
  if (!side)
    return -1;
  return (int)ReadU16(side, OFF_CBRUSHSIDE_TEXINFO);
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
  normal[0] = ReadF32(plane, OFF_CPLANE_NORMAL + 0);
  normal[1] = ReadF32(plane, OFF_CPLANE_NORMAL + 4);
  normal[2] = ReadF32(plane, OFF_CPLANE_NORMAL + 8);
  dist = ReadF32(plane, OFF_CPLANE_DIST);
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
  normal[0] = ReadF32(plane, OFF_CPLANE_NORMAL + 0);
  normal[1] = ReadF32(plane, OFF_CPLANE_NORMAL + 4);
  normal[2] = ReadF32(plane, OFF_CPLANE_NORMAL + 8);
  dist = ReadF32(plane, OFF_CPLANE_DIST);
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
  if (!g_pBSPData || OFF_MAP_CMODELS == 0 || idx < 0 || idx >= GetNumCModels())
    return nullptr;
  const uint8_t *table =
      reinterpret_cast<const uint8_t *>(ReadPtr(g_pBSPData, OFF_MAP_CMODELS));
  if (!table)
    return nullptr;
  return table + (size_t)idx * SZ_CMODEL;
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
  if (!g_pBSPData || OFF_MAP_BOXBRUSHES == 0 || idx < 0 ||
      idx >= GetNumBoxBrushes())
    return nullptr;
  const uint8_t *table = reinterpret_cast<const uint8_t *>(
      ReadPtr(g_pBSPData, OFF_MAP_BOXBRUSHES));
  if (!table)
    return nullptr;
  return table + (size_t)idx * SZ_CBOXBRUSH;
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

// Brush AABB cache
static void BuildBrushCache() {
  g_brushCache.clear();
  g_brushCacheBuilt = false;
  int n = GetNumBrushes();
  if (n <= 0 || n > 1000000)
    return;
  g_brushCache.resize(n);
  for (int i = 0; i < n; ++i) {
    BrushCacheEntry &e = g_brushCache[i];
    e.valid = GetBrushBounds(i, e.mins, e.maxs);
    e.contents = e.valid ? GetBrushContents(i) : 0;
  }
  g_brushCacheBuilt = true;
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
    std::vector<BrushCacheEntry> local;
    int n = GetNumBrushes();
    if (n > 0 && n <= 1000000) {
      local.resize(n);
      for (int i = 0; i < n; ++i) {
        BrushCacheEntry &e = local[i];
        e.valid = GetBrushBounds(i, e.mins, e.maxs);
        e.contents = e.valid ? GetBrushContents(i) : 0;
      }
    }
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
