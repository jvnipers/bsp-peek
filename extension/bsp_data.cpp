#include "bsp_data.h"
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

// All offsets / sizes are populated from gamedata Keys at Init() time.

namespace BSPData {
// Base address of CCollisionBSPData global
// (resolved from "Addresses/g_BSPData").
static uint8_t *g_pBSPData = nullptr;

// Field offsets within CCollisionBSPData (bytes).
static int OFF_NUMPLANES = 0;
static int OFF_MAP_PLANES = 0;
static int OFF_NUMBRUSHSIDES = 0;
static int OFF_MAP_BRUSHSIDES = 0;
static int OFF_NUMBRUSHES = 0;
static int OFF_MAP_BRUSHES = 0;
static int OFF_NUMLEAFBRUSHES = 0;
static int OFF_MAP_LEAFBRUSHES = 0;
static int OFF_NUMLEAFS = 0;  // optional (0 = not derived)
static int OFF_MAP_LEAFS = 0; // optional

// cbrush_t layout.
static int OFF_CBRUSH_CONTENTS = 0;
static int OFF_CBRUSH_NUMSIDES = 4;
static int OFF_CBRUSH_FIRSTBRUSHSIDE = 6;
static int SZ_CBRUSH = 8;

// cbrushside_t layout.
static int OFF_CBRUSHSIDE_PLANE = 0;
static int SZ_CBRUSHSIDE = 8;

// cplane_t layout.
static int OFF_CPLANE_NORMAL = 0;
static int OFF_CPLANE_DIST = 12;
static int SZ_CPLANE = 20;

// cleaf_t layout.
static int OFF_CLEAF_FIRSTLEAFBRUSH = 8;
static int OFF_CLEAF_NUMLEAFBRUSHES = 10;
static int SZ_CLEAF = 16;

// cnode_t layout.
static int OFF_CNODE_PLANE = 0;
static int OFF_CNODE_CHILDREN = 4;
static int SZ_CNODE = 12;

// Node table offsets within CCollisionBSPData.
static int OFF_NUMNODES = 0;
static int OFF_MAP_NODES = 0;

// helpers
static inline int read_i32(const uint8_t *p, int off) {
  int v;
  memcpy(&v, p + off, 4);
  return v;
}
static inline uint16_t read_u16(const uint8_t *p, int off) {
  uint16_t v;
  memcpy(&v, p + off, 2);
  return v;
}
static inline void *read_ptr(const uint8_t *p, int off) {
  uint32_t v;
  memcpy(&v, p + off, 4);
  return reinterpret_cast<void *>(static_cast<uintptr_t>(v));
}
static inline float read_f32(const uint8_t *p, int off) {
  float v;
  memcpy(&v, p + off, 4);
  return v;
}

static int get_key_int(IGameConfig *gc, const char *key, int defaultVal) {
  const char *s = gc->GetKeyValue(key);
  return (s && *s) ? atoi(s) : defaultVal;
}

bool Init(IGameConfig *gameconf, char *error, size_t maxlen) {
  // Resolve g_BSPData base via Addresses chain.
  void *addr = nullptr;
  if (!gameconf->GetAddress("g_BSPData", &addr) || addr == nullptr) {
    snprintf(error, maxlen, "gamedata Address 'g_BSPData' did not resolve.");
    return false;
  }
  g_pBSPData = reinterpret_cast<uint8_t *>(addr);

  // Load all field offsets from "Keys".
  OFF_NUMPLANES = get_key_int(gameconf, "off_numplanes", -1);
  OFF_MAP_PLANES = get_key_int(gameconf, "off_map_planes", -1);
  OFF_NUMBRUSHSIDES = get_key_int(gameconf, "off_numbrushsides", -1);
  OFF_MAP_BRUSHSIDES = get_key_int(gameconf, "off_map_brushsides", -1);
  OFF_NUMBRUSHES = get_key_int(gameconf, "off_numbrushes", -1);
  OFF_MAP_BRUSHES = get_key_int(gameconf, "off_map_brushes", -1);
  OFF_NUMLEAFBRUSHES = get_key_int(gameconf, "off_numleafbrushes", -1);
  OFF_MAP_LEAFBRUSHES = get_key_int(gameconf, "off_map_leafbrushes", -1);
  OFF_NUMLEAFS = get_key_int(gameconf, "off_numleafs", 0);
  OFF_MAP_LEAFS = get_key_int(gameconf, "off_map_leafs", 0);

  OFF_CBRUSH_CONTENTS = get_key_int(gameconf, "cbrush_contents", 0);
  OFF_CBRUSH_NUMSIDES = get_key_int(gameconf, "cbrush_numsides", 4);
  OFF_CBRUSH_FIRSTBRUSHSIDE = get_key_int(gameconf, "cbrush_firstbrushside", 6);
  SZ_CBRUSH = get_key_int(gameconf, "cbrush_sizeof", 8);

  OFF_CBRUSHSIDE_PLANE = get_key_int(gameconf, "cbrushside_plane", 0);
  SZ_CBRUSHSIDE = get_key_int(gameconf, "cbrushside_sizeof", 8);

  OFF_CPLANE_NORMAL = get_key_int(gameconf, "cplane_normal", 0);
  OFF_CPLANE_DIST = get_key_int(gameconf, "cplane_dist", 12);
  SZ_CPLANE = get_key_int(gameconf, "cplane_sizeof", 20);

  OFF_CLEAF_FIRSTLEAFBRUSH = get_key_int(gameconf, "cleaf_firstleafbrush", 8);
  OFF_CLEAF_NUMLEAFBRUSHES = get_key_int(gameconf, "cleaf_numleafbrushes", 10);
  SZ_CLEAF = get_key_int(gameconf, "cleaf_sizeof", 16);

  OFF_CNODE_PLANE = get_key_int(gameconf, "cnode_plane", 0);
  OFF_CNODE_CHILDREN = get_key_int(gameconf, "cnode_children", 4);
  SZ_CNODE = get_key_int(gameconf, "cnode_sizeof", 12);

  OFF_NUMNODES = get_key_int(gameconf, "off_numnodes", 0);
  OFF_MAP_NODES = get_key_int(gameconf, "off_map_nodes", 0);

  // Sanity.
  if (OFF_NUMBRUSHES < 0 || OFF_MAP_BRUSHES < 0 || OFF_NUMBRUSHSIDES < 0 ||
      OFF_MAP_BRUSHSIDES < 0 || OFF_NUMPLANES < 0 || OFF_MAP_PLANES < 0) {
    snprintf(error, maxlen, "gamedata Keys missing required field offsets.");
    return false;
  }

  return true;
}

// Debug accessor, used by extension.cpp to log resolved address at load.
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

// per-map brush AABB cache
struct BrushCacheEntry {
  float mins[3];
  float maxs[3];
  int contents;
  bool valid;
};
static std::vector<BrushCacheEntry> g_brushCache;
static bool g_brushCacheBuilt = false;
// Mutex guards g_brushCache + g_brushCacheBuilt against the async worker that
// builds a fresh cache on a side thread.
// Reads (FindBrushPairAtSeam) take a shared lock;
// the worker swaps the vector under exclusive lock when done.
// Using std::mutex, lock is uncontended after build
// finishes and the per-query cost is negligible.
static std::mutex g_brushCacheMutex;
static std::atomic<bool> g_asyncBuildInProgress{false};
// Detached worker handle; we keep one around so a second OnMapStart can't race
// with an in-flight build. Joined explicitly by Shutdown() so the .so can
// unload cleanly without the worker outliving the engine module.
static std::thread g_asyncBuildThread;

// per-map leaf AABB cache (defined later, declared here for visibility)
struct LeafCacheEntry {
  float mins[3];
  float maxs[3];
  bool valid;
};
static std::vector<LeafCacheEntry> g_leafCache;
static bool g_leafCacheBuilt = false;

static void BuildLeafCache(); // forward decl (used by RebuildCache below)

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

void RebuildCache() {
  std::lock_guard<std::mutex> lk(g_brushCacheMutex);
  g_brushCache.clear();
  g_brushCacheBuilt = false;
  g_leafCache.clear();
  g_leafCacheBuilt = false;
  BuildBrushCache();
}

// Off-main-thread cache build. Returns immediately.
// Reads via FindBrushPairAtSeam will see g_brushCacheBuilt=false
// until the worker finishes, at which point a freshly-built vector is swapped
// in under lock. A second call while a build is in flight is a no-op.
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

// top-level field accessors
int GetNumBrushes() {
  return g_pBSPData ? read_i32(g_pBSPData, OFF_NUMBRUSHES) : 0;
}
int GetNumBrushSides() {
  return g_pBSPData ? read_i32(g_pBSPData, OFF_NUMBRUSHSIDES) : 0;
}
int GetNumPlanes() {
  return g_pBSPData ? read_i32(g_pBSPData, OFF_NUMPLANES) : 0;
}
int GetNumLeafBrushes() {
  return g_pBSPData ? read_i32(g_pBSPData, OFF_NUMLEAFBRUSHES) : 0;
}
int GetNumLeaves() {
  if (!g_pBSPData || OFF_NUMLEAFS == 0)
    return 0;
  return read_i32(g_pBSPData, OFF_NUMLEAFS);
}

static const uint8_t *brush_at(int idx) {
  if (!g_pBSPData || idx < 0 || idx >= GetNumBrushes())
    return nullptr;
  const uint8_t *table =
      reinterpret_cast<const uint8_t *>(read_ptr(g_pBSPData, OFF_MAP_BRUSHES));
  if (!table)
    return nullptr;
  return table + (size_t)idx * SZ_CBRUSH;
}

static const uint8_t *brushside_at(int idx) {
  if (!g_pBSPData || idx < 0 || idx >= GetNumBrushSides())
    return nullptr;
  const uint8_t *table = reinterpret_cast<const uint8_t *>(
      read_ptr(g_pBSPData, OFF_MAP_BRUSHSIDES));
  if (!table)
    return nullptr;
  return table + (size_t)idx * SZ_CBRUSHSIDE;
}

int GetBrushContents(int brushIdx) {
  const uint8_t *b = brush_at(brushIdx);
  if (!b)
    return 0;
  return read_i32(b, OFF_CBRUSH_CONTENTS);
}

bool GetBrushBounds(int brushIdx, float mins[3], float maxs[3]) {
  const uint8_t *b = brush_at(brushIdx);
  if (!b)
    return false;
  uint16_t numsides = read_u16(b, OFF_CBRUSH_NUMSIDES);
  uint16_t first = read_u16(b, OFF_CBRUSH_FIRSTBRUSHSIDE);
  if (numsides == 0)
    return false;

  mins[0] = mins[1] = mins[2] = 1e30f;
  maxs[0] = maxs[1] = maxs[2] = -1e30f;

  for (uint16_t i = 0; i < numsides; ++i) {
    const uint8_t *side = brushside_at(first + i);
    if (!side)
      continue;
    const uint8_t *plane =
        reinterpret_cast<const uint8_t *>(read_ptr(side, OFF_CBRUSHSIDE_PLANE));
    if (!plane)
      continue;
    float nx = read_f32(plane, OFF_CPLANE_NORMAL + 0);
    float ny = read_f32(plane, OFF_CPLANE_NORMAL + 4);
    float nz = read_f32(plane, OFF_CPLANE_NORMAL + 8);
    float d = read_f32(plane, OFF_CPLANE_DIST);
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
  uint16_t numsides = read_u16(b, OFF_CBRUSH_NUMSIDES);
  uint16_t first = read_u16(b, OFF_CBRUSH_FIRSTBRUSHSIDE);
  if (numsides != 6)
    return false;
  int axisHit[6] = {0, 0, 0, 0, 0, 0};
  for (uint16_t i = 0; i < numsides; ++i) {
    const uint8_t *side = brushside_at(first + i);
    if (!side)
      return false;
    const uint8_t *plane =
        reinterpret_cast<const uint8_t *>(read_ptr(side, OFF_CBRUSHSIDE_PLANE));
    if (!plane)
      return false;
    float nx = read_f32(plane, OFF_CPLANE_NORMAL + 0);
    float ny = read_f32(plane, OFF_CPLANE_NORMAL + 4);
    float nz = read_f32(plane, OFF_CPLANE_NORMAL + 8);
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

// leaf accessors
static const uint8_t *leaf_at(int idx) {
  if (!g_pBSPData || OFF_MAP_LEAFS == 0 || idx < 0 || idx >= GetNumLeaves())
    return nullptr;
  const uint8_t *table =
      reinterpret_cast<const uint8_t *>(read_ptr(g_pBSPData, OFF_MAP_LEAFS));
  if (!table)
    return nullptr;
  return table + (size_t)idx * SZ_CLEAF;
}

int LeafBrushes(int leafIdx, int *outBuf, int maxOut) {
  const uint8_t *leaf = leaf_at(leafIdx);
  if (!leaf || OFF_MAP_LEAFBRUSHES == 0)
    return -1;
  const uint16_t *table = reinterpret_cast<const uint16_t *>(
      read_ptr(g_pBSPData, OFF_MAP_LEAFBRUSHES));
  if (!table)
    return -1;
  uint16_t first = read_u16(leaf, OFF_CLEAF_FIRSTLEAFBRUSH);
  uint16_t count = read_u16(leaf, OFF_CLEAF_NUMLEAFBRUSHES);
  int n = (int)count;
  if (n > maxOut)
    n = maxOut;
  for (int i = 0; i < n; ++i)
    outBuf[i] = (int)table[(size_t)first + i];
  return n;
}

// Per-map leaf AABB cache (union of member brush AABBs).
// No longer used by LeafAtPoint (which uses the O(log N) BSP node-walk via
// `map_nodes`), but kept available for any future code that wants per-leaf
// bounds without re-walking the leafbrushes table.
// RebuildCache() does NOT call this.
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
      read_ptr(g_pBSPData, OFF_MAP_LEAFBRUSHES));
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
    uint16_t first = read_u16(leaf, OFF_CLEAF_FIRSTLEAFBRUSH);
    uint16_t count = read_u16(leaf, OFF_CLEAF_NUMLEAFBRUSHES);
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

int LeafAtPoint(const float pos[3]) {
  // Start at root (node[0]), each step:
  // signed distance to plane -> descend children[0] if d>=0 else children[1].
  // Negative child = leaf via Source convention: leafIdx = -1 - child.
  // O(log N).
  if (!g_pBSPData || OFF_MAP_NODES == 0 || SZ_CNODE == 0)
    return -1;
  int numnodes = (OFF_NUMNODES != 0) ? read_i32(g_pBSPData, OFF_NUMNODES) : 0;
  if (numnodes <= 0)
    return -1;
  const uint8_t *nodes =
      reinterpret_cast<const uint8_t *>(read_ptr(g_pBSPData, OFF_MAP_NODES));
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
        reinterpret_cast<const uint8_t *>(read_ptr(node, OFF_CNODE_PLANE));
    if (!plane)
      return -1;
    float nx = read_f32(plane, OFF_CPLANE_NORMAL + 0);
    float ny = read_f32(plane, OFF_CPLANE_NORMAL + 4);
    float nz = read_f32(plane, OFF_CPLANE_NORMAL + 8);
    float d = pos[0] * nx + pos[1] * ny + pos[2] * nz -
              read_f32(plane, OFF_CPLANE_DIST);
    int childOff = OFF_CNODE_CHILDREN + (d >= 0.0f ? 0 : 4);
    idx = read_i32(node, childOff);
  }
  return -1;
}

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
  // Reused for all subsequent queries.
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
} // namespace BSPData
