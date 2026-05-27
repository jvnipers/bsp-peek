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

// cbrush_t layout
static int OFF_CBRUSH_CONTENTS = 0;
static int OFF_CBRUSH_NUMSIDES = 4;
static int OFF_CBRUSH_FIRSTBRUSHSIDE = 6;
static int SZ_CBRUSH = 8;

// cbrushside_t layout
static int OFF_CBRUSHSIDE_PLANE = 0;
static int SZ_CBRUSHSIDE = 8;

// cplane_t layout
static int OFF_CPLANE_NORMAL = 0;
static int OFF_CPLANE_DIST = 12;
static int SZ_CPLANE = 20;

// cleaf_t layout
static int OFF_CLEAF_CONTENTS = 0;
static int OFF_CLEAF_CLUSTER = 4;
static int OFF_CLEAF_AREA_FLAGS = 6; // packed: area:9 flags:7 (short)
static int OFF_CLEAF_FIRSTLEAFBRUSH = 8;
static int OFF_CLEAF_NUMLEAFBRUSHES = 10;
static int SZ_CLEAF = 16;

// cnode_t layout
static int OFF_CNODE_PLANE = 0;
static int OFF_CNODE_CHILDREN = 4;
static int SZ_CNODE = 12;

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
static bool g_leafCacheBuilt = false;

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

  // cbrush_t / cbrushside_t / cplane_t
  OFF_CBRUSH_CONTENTS = GetKeyInt(gameconf, "cbrush_contents", 0);
  OFF_CBRUSH_NUMSIDES = GetKeyInt(gameconf, "cbrush_numsides", 4);
  OFF_CBRUSH_FIRSTBRUSHSIDE = GetKeyInt(gameconf, "cbrush_firstbrushside", 6);
  SZ_CBRUSH = GetKeyInt(gameconf, "cbrush_sizeof", 8);

  OFF_CBRUSHSIDE_PLANE = GetKeyInt(gameconf, "cbrushside_plane", 0);
  SZ_CBRUSHSIDE = GetKeyInt(gameconf, "cbrushside_sizeof", 8);

  OFF_CPLANE_NORMAL = GetKeyInt(gameconf, "cplane_normal", 0);
  OFF_CPLANE_DIST = GetKeyInt(gameconf, "cplane_dist", 12);
  SZ_CPLANE = GetKeyInt(gameconf, "cplane_sizeof", 20);

  // cleaf_t
  OFF_CLEAF_CONTENTS = GetKeyInt(gameconf, "cleaf_contents", 0);
  OFF_CLEAF_CLUSTER = GetKeyInt(gameconf, "cleaf_cluster", 4);
  OFF_CLEAF_AREA_FLAGS = GetKeyInt(gameconf, "cleaf_area_flags", 6);
  OFF_CLEAF_FIRSTLEAFBRUSH = GetKeyInt(gameconf, "cleaf_firstleafbrush", 8);
  OFF_CLEAF_NUMLEAFBRUSHES = GetKeyInt(gameconf, "cleaf_numleafbrushes", 10);
  SZ_CLEAF = GetKeyInt(gameconf, "cleaf_sizeof", 16);

  // cnode_t
  OFF_CNODE_PLANE = GetKeyInt(gameconf, "cnode_plane", 0);
  OFF_CNODE_CHILDREN = GetKeyInt(gameconf, "cnode_children", 4);
  SZ_CNODE = GetKeyInt(gameconf, "cnode_sizeof", 12);

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
  g_leafCacheBuilt = false;
}

void OnMapStart() {
  std::lock_guard<std::mutex> lk(g_brushCacheMutex);
  g_brushCache.clear();
  g_brushCacheBuilt = false;
  g_leafCache.clear();
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

bool LeafBounds(int leafIdx, float mins[3], float maxs[3]) {
  if (leafIdx < 0 || leafIdx >= GetNumLeaves())
    return false;
  // Build leaf cache lazily under lock.
  std::lock_guard<std::mutex> lk(g_brushCacheMutex);
  if (!g_brushCacheBuilt)
    BuildBrushCache();
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

// Per-map leaf AABB cache (union of member brush AABBs).
// Built lazily by LeafBounds; not called from RebuildCache to avoid the
// O(numleafs * numleafbrushes) cost on map load when callers may never
// need leaf bounds.
static void BuildLeafCache() {
  g_leafCache.clear();
  g_leafCacheBuilt = false;
  int nleaf = GetNumLeaves();
  if (nleaf <= 0 || nleaf > 200000)
    return;
  if (!g_brushCacheBuilt)
    BuildBrushCache();
  if (!g_brushCacheBuilt)
    return;

  const uint16_t *lbtable = reinterpret_cast<const uint16_t *>(
      ReadPtr(g_pBSPData, OFF_MAP_LEAFBRUSHES));
  if (!lbtable)
    return;

  g_leafCache.resize(nleaf);
  int nbrush = (int)g_brushCache.size();
  for (int i = 0; i < nleaf; ++i) {
    LeafCacheEntry &le = g_leafCache[i];
    le.valid = false;
    le.mins[0] = le.mins[1] = le.mins[2] = 1e30f;
    le.maxs[0] = le.maxs[1] = le.maxs[2] = -1e30f;

    const uint8_t *leaf = leaf_at(i);
    if (!leaf)
      continue;
    uint16_t first = ReadU16(leaf, OFF_CLEAF_FIRSTLEAFBRUSH);
    uint16_t count = ReadU16(leaf, OFF_CLEAF_NUMLEAFBRUSHES);
    if (count == 0)
      continue;

    for (uint16_t j = 0; j < count; ++j) {
      int brushIdx = (int)lbtable[(size_t)first + j];
      if (brushIdx < 0 || brushIdx >= nbrush)
        continue;
      const BrushCacheEntry &be = g_brushCache[brushIdx];
      if (!be.valid)
        continue;
      for (int k = 0; k < 3; ++k) {
        if (be.mins[k] < le.mins[k])
          le.mins[k] = be.mins[k];
        if (be.maxs[k] > le.maxs[k])
          le.maxs[k] = be.maxs[k];
      }
      le.valid = true;
    }
  }
  g_leafCacheBuilt = true;
}

void RebuildCache() {
  std::lock_guard<std::mutex> lk(g_brushCacheMutex);
  g_brushCache.clear();
  g_brushCacheBuilt = false;
  g_leafCache.clear();
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
