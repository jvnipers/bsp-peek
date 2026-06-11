#include "bsp_lumps.h"
#include "bsp_util.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

using BSPUtil::ReadF32;
using BSPUtil::ReadI16;
using BSPUtil::ReadI32;
using BSPUtil::ReadU16;
using BSPUtil::ReadU8;

namespace BSPLumps {

namespace {

// BSP file format constants (CSGO).
constexpr int kIdent = (('P' << 24) + ('S' << 16) + ('B' << 8) + 'V');
constexpr int kNumLumps = 64;
constexpr int kHeaderSize = 1036; // 4+4 + 16*64 + 4

constexpr int LUMP_ENTITIES = 0;
constexpr int LUMP_VERTEXES = 3;
constexpr int LUMP_VISIBILITY = 4;
constexpr int LUMP_FACES = 7;
constexpr int LUMP_TEXDATA = 2;
constexpr int LUMP_EDGES = 12;
constexpr int LUMP_SURFEDGES = 13;
constexpr int LUMP_LIGHTING = 8;
constexpr int LUMP_WORLDLIGHTS_LDR = 15;
constexpr int LUMP_LEAFFACES = 16;
constexpr int LUMP_TEXINFO = 18;
constexpr int LUMP_CUBEMAPS = 42;
constexpr int LUMP_TEXDATA_STRING_DATA = 43;
constexpr int LUMP_TEXDATA_STRING_TABLE = 44;
constexpr int LUMP_LIGHTING_HDR = 53;
constexpr int LUMP_WORLDLIGHTS_HDR = 54;
constexpr int LUMP_GAME_LUMP = 35;

// Game lump directory: int count, then dgamelump_t[count] (16B each).
//   int id; uint16 flags; uint16 version; int fileofs; int filelen
// fileofs is an ABSOLUTE offset into the .bsp file.
constexpr int kSizeofGameLumpEntry = 16;
// GAMELUMP_STATIC_PROPS = 'sprp' multichar int (engine: id == this constant).
constexpr int kGameLumpSprp = ('s' << 24) | ('p' << 16) | ('r' << 8) | ('p');
constexpr int kGameLump_Id = 0;
constexpr int kGameLump_Version = 6;
constexpr int kGameLump_FileOfs = 8;
constexpr int kGameLump_FileLen = 12;

// Static prop dict entry = char m_Name[128].
constexpr int kStaticPropNameLen = 128;
// StaticPropLump_t field offsets.
// The prefix is identical across versions >= 4,
// the fields past it appeared incrementally
// (skin/fades v4, lighting origin v4-6, forced fade scale v5, flagsEx v10).
// Each is read only when the per-prop stride covers it,
// so older versions just skip them.
constexpr int kSP_Origin = 0;
constexpr int kSP_Angles = 12;
constexpr int kSP_PropType = 24;
constexpr int kSP_FirstLeaf = 26;
constexpr int kSP_LeafCount = 28;
constexpr int kSP_Solid = 30;
constexpr int kSP_Flags = 31;
constexpr int kSP_PrefixBytes = 32;
constexpr int kSP_Skin = 32;            // int
constexpr int kSP_FadeMinDist = 36;     // float
constexpr int kSP_FadeMaxDist = 40;     // float
constexpr int kSP_LightingOrigin = 44;  // Vector (12B)
constexpr int kSP_ForcedFadeScale = 56; // float (v5+)
constexpr int kSP_FlagsEx = 72;         // int (v10+)

// Vertex: Vector (3 floats) = 12B
constexpr int kSizeofVertex = 12;

// dface_t = 56B
constexpr int kSizeofFace = 56;
constexpr int kFace_PlaneNum = 0;  // uint16
constexpr int kFace_FirstEdge = 4; // int32
constexpr int kFace_NumEdges = 8;  // int16
constexpr int kFace_TexInfo = 10;  // int16
constexpr int kFace_DispInfo = 12; // int16
constexpr int kFace_Styles = 16;   // uint8[4]
constexpr int kFace_LightOfs = 20; // int32
constexpr int kFace_Area = 24;     // float
constexpr int kFace_OrigFace = 44; // int32

// dcubemapsample_t = 16B: int origin[3] + int size
constexpr int kSizeofCubemap = 16;
constexpr int kCubemap_Origin = 0; // int32[3]
constexpr int kCubemap_Size = 12;  // int32

// dedge_t = 4B: uint16 v[2]
constexpr int kSizeofEdge = 4;
// surfedges: int32 array, 4B each
constexpr int kSizeofSurfedge = 4;

// dtexinfo_t: textureVecs[2][4]=64B + texdata=4B + flags=4B = 72B
constexpr int kSizeofTexInfo = 72;
constexpr int kTexInfo_TexData = 64;
constexpr int kTexInfo_Flags = 68;

// dtexdata_t: reflectivity[3]=12B + nameStringTableID=4 + width=4 + height=4
//             + view_width=4 + view_height=4 = 32B
constexpr int kSizeofTexData = 32;
constexpr int kTexData_Reflectivity = 0;
constexpr int kTexData_NameStringTableID = 12;

// dworldlight_t (CSGO): origin[3]=12 + intensity[3]=12 + normal[3]=12
//   + shadow_cast_offset[3]=12 + cluster=4 + type=4 + style=4
//   + stopdot=4 + stopdot2=4 + exponent=4 + radius=4 + constant_attn=4
//   + linear_attn=4 + quadratic_attn=4 + flags=4 + texinfo=4 + owner=4
//   = 100 bytes. Actual size varies by SDK; CSGO is 100.
constexpr int kSizeofWorldlight = 100;
constexpr int kWL_Origin = 0;
constexpr int kWL_Intensity = 12;
constexpr int kWL_Normal = 24;
constexpr int kWL_ShadowOffset = 36;
constexpr int kWL_Cluster = 48;
constexpr int kWL_Type = 52;
constexpr int kWL_Style = 56;
constexpr int kWL_StopDot = 60;
constexpr int kWL_StopDot2 = 64;
constexpr int kWL_Exponent = 68;
constexpr int kWL_Radius = 72;
constexpr int kWL_ConstantAttn = 76;
constexpr int kWL_LinearAttn = 80;
constexpr int kWL_QuadraticAttn = 84;
constexpr int kWL_Flags = 88;
constexpr int kWL_TexInfo = 92;
constexpr int kWL_Owner = 96;

struct LumpEntry {
  int32_t fileofs;
  int32_t filelen;
  int32_t version;
  int32_t uncompressedLen;
};

struct Entity {
  std::unordered_map<std::string, std::string> kv;
  std::string classname;
  bool hasOrigin;
  float origin[3];
};

// Parsed static prop (sprp game lump). Fields past the stable prefix are
// populated only when the per-prop stride covers them
// (older lump versions leave them 0).
struct StaticProp {
  float origin[3];
  float angles[3];
  uint16_t propType;  // index into staticPropNames
  uint16_t firstLeaf; // index into staticPropLeafs
  uint16_t leafCount;
  uint8_t solid; // SOLID_* (6 = VPHYSICS, 2 = BBOX, 0 = none)
  uint8_t flags;
  int32_t skin;
  float fadeMinDist;
  float fadeMaxDist;
  float lightingOrigin[3];
  float forcedFadeScale;
  int32_t flagsEx;
};

struct State {
  bool loaded = false;
  char loadedMap[128] = {0};

  int32_t version = 0;
  int32_t revision = 0;
  LumpEntry lumps[kNumLumps] = {};

  std::string entityRaw;
  std::vector<Entity> entities;

  std::vector<uint8_t> visLump;       // LUMP_VISIBILITY raw bytes
  std::vector<uint8_t> cubemapsLump;  // array of dcubemapsample_t (16B each)
  std::vector<uint8_t> vertexesLump;  // array of Vector (12B each)
  std::vector<uint8_t> edgesLump;     // array of dedge_t (4B each)
  std::vector<uint8_t> surfedgesLump; // array of int32 (4B each)
  std::vector<uint8_t> facesLump;     // array of dface_t (56B each)

  std::vector<uint8_t> texinfoLump;
  std::vector<uint8_t> texdataLump;
  std::vector<uint8_t> texdataStringTable; // array of int32 offsets
  std::vector<uint8_t> texdataStringData;  // packed null-terminated names

  std::vector<uint8_t> leafFacesLump; // array of uint16

  std::vector<uint8_t> worldlightsLump; // LDR or HDR (LDR preferred)

  // Static props (sprp game lump), parsed into structs at load.
  int staticPropVersion = 0;
  std::vector<std::string> staticPropNames; // dict (model paths)
  std::vector<uint16_t> staticPropLeafs;    // flat leaf-index pool
  std::vector<StaticProp> staticProps;
};

static State g;

bool ReadFile(FILE *f, int64_t offset, int len, std::vector<uint8_t> &out) {
  out.clear();
  if (len <= 0)
    return true;
  out.resize(len);
  if (std::fseek(f, (long)offset, SEEK_SET) != 0)
    return false;
  if (std::fread(out.data(), 1, len, f) != (size_t)len)
    return false;
  return true;
}

void ParseEntities(const std::string &src, std::vector<Entity> &out) {
  out.clear();
  size_t i = 0;
  while (i < src.size()) {
    while (i < src.size() && src[i] != '{')
      ++i;
    if (i >= src.size())
      break;
    ++i; // skip '{'
    Entity e;
    e.hasOrigin = false;
    e.origin[0] = e.origin[1] = e.origin[2] = 0;
    while (i < src.size() && src[i] != '}') {
      // skip whitespace
      while (i < src.size() && (src[i] == ' ' || src[i] == '\t' ||
                                src[i] == '\n' || src[i] == '\r'))
        ++i;
      if (i < src.size() && src[i] == '}')
        break;
      if (i >= src.size() || src[i] != '"')
        break;
      ++i;
      size_t kStart = i;
      while (i < src.size() && src[i] != '"')
        ++i;
      if (i >= src.size())
        break;
      std::string key(src, kStart, i - kStart);
      ++i; // closing "
      while (i < src.size() && (src[i] == ' ' || src[i] == '\t'))
        ++i;
      if (i >= src.size() || src[i] != '"')
        break;
      ++i;
      size_t vStart = i;
      while (i < src.size() && src[i] != '"')
        ++i;
      if (i >= src.size())
        break;
      std::string val(src, vStart, i - vStart);
      ++i;
      e.kv[key] = val;
      if (key == "classname")
        e.classname = val;
      else if (key == "origin") {
        float x = 0, y = 0, z = 0;
        if (std::sscanf(val.c_str(), "%f %f %f", &x, &y, &z) >= 3) {
          e.origin[0] = x;
          e.origin[1] = y;
          e.origin[2] = z;
          e.hasOrigin = true;
        }
      }
    }
    if (i < src.size())
      ++i; // skip '}'
    out.push_back(std::move(e));
  }
}

// Parse the sprp game lump (static props) from the open BSP file.
// Layout at the sprp blob: int dictCount, char[128] names[dictCount],
//   int leafCount, uint16 leafs[leafCount], int propCount,
//   StaticPropLump_t[propCount].
// The prop struct size varies by version, we derive the stride from the blob
// size and read only the version-stable prefix.
// Best-effort: silently leaves the prop list empty on any structural mismatch.
void ParseStaticProps(FILE *f) {
  const LumpEntry &gl = g.lumps[LUMP_GAME_LUMP];
  if (gl.filelen < 4)
    return;
  std::vector<uint8_t> dir;
  if (!ReadFile(f, gl.fileofs, gl.filelen, dir) || dir.size() < 4)
    return;

  int count = ReadI32(dir.data(), 0);
  if (count <= 0 ||
      (size_t)4 + (size_t)count * kSizeofGameLumpEntry > dir.size())
    return;

  int sprpOfs = -1, sprpLen = 0, sprpVer = 0;
  for (int i = 0; i < count; ++i) {
    int base = 4 + i * kSizeofGameLumpEntry;
    // GameLumpId_t is a multichar int: 'sprp'==('s'<<24)|('p'<<16)|('r'<<8)|'p'
    // On a little-endian PC BSP it is stored as the raw int,
    // so the on-disk bytes are 'p','r','p','s'.
    // Compare the int the way the engine does (id == GAMELUMP_STATIC_PROPS),
    // not the bytes in name order.
    int id = ReadI32(dir.data(), base + kGameLump_Id);
    if (id == kGameLumpSprp) {
      sprpVer = ReadU16(dir.data(), base + kGameLump_Version);
      sprpOfs = ReadI32(dir.data(), base + kGameLump_FileOfs);
      sprpLen = ReadI32(dir.data(), base + kGameLump_FileLen);
      break;
    }
  }
  if (sprpOfs < 0 || sprpLen < 12)
    return;

  std::vector<uint8_t> b;
  if (!ReadFile(f, sprpOfs, sprpLen, b) || b.size() < 12)
    return;

  size_t p = 0;
  int dictCount = ReadI32(b.data(), p);
  p += 4;
  if (dictCount < 0 ||
      p + (size_t)dictCount * kStaticPropNameLen + 8 > b.size())
    return;
  g.staticPropNames.reserve(dictCount);
  for (int i = 0; i < dictCount; ++i) {
    const char *name = (const char *)b.data() + p;
    size_t n = 0;
    while (n < (size_t)kStaticPropNameLen && name[n] != '\0')
      ++n;
    g.staticPropNames.emplace_back(name, n);
    p += kStaticPropNameLen;
  }

  int leafCount = ReadI32(b.data(), p);
  p += 4;
  if (leafCount < 0 || p + (size_t)leafCount * 2 + 4 > b.size()) {
    g.staticPropNames.clear();
    return;
  }
  g.staticPropLeafs.reserve(leafCount);
  for (int i = 0; i < leafCount; ++i) {
    g.staticPropLeafs.push_back(ReadU16(b.data(), p));
    p += 2;
  }

  int propCount = ReadI32(b.data(), p);
  p += 4;
  // Upper bound guards a corrupt count from a huge reserve(). A real map's prop
  // array also can't exceed the remaining blob at the minimum struct size.
  if (propCount <= 0 || (size_t)propCount * kSP_PrefixBytes > b.size() - p) {
    g.staticPropNames.clear();
    g.staticPropLeafs.clear();
    return;
  }

  // Derive per-prop stride from the remaining bytes.
  // If the exact division fails, retry skipping a leading 4-byte field
  // (some CSGO versions prepend an unknown int before the prop array).
  size_t poolStart = p;
  size_t poolBytes = b.size() - poolStart;
  int stride = (int)(poolBytes / (size_t)propCount);
  if (poolBytes % (size_t)propCount != 0 || stride < kSP_PrefixBytes) {
    if (poolBytes >= 4) {
      size_t pb2 = poolBytes - 4;
      int s2 = (int)(pb2 / (size_t)propCount);
      if (pb2 % (size_t)propCount == 0 && s2 >= kSP_PrefixBytes) {
        poolStart += 4;
        stride = s2;
      }
    }
  }
  if (stride < kSP_PrefixBytes) {
    g.staticPropNames.clear();
    g.staticPropLeafs.clear();
    return;
  }

  g.staticPropVersion = sprpVer;
  g.staticProps.reserve(propCount);
  for (int i = 0; i < propCount; ++i) {
    size_t off = poolStart + (size_t)i * stride;
    if (off + kSP_PrefixBytes > b.size())
      break;
    const uint8_t *q = b.data() + off;
    StaticProp sp;
    sp.origin[0] = ReadF32(q, kSP_Origin + 0);
    sp.origin[1] = ReadF32(q, kSP_Origin + 4);
    sp.origin[2] = ReadF32(q, kSP_Origin + 8);
    sp.angles[0] = ReadF32(q, kSP_Angles + 0);
    sp.angles[1] = ReadF32(q, kSP_Angles + 4);
    sp.angles[2] = ReadF32(q, kSP_Angles + 8);
    sp.propType = ReadU16(q, kSP_PropType);
    sp.firstLeaf = ReadU16(q, kSP_FirstLeaf);
    sp.leafCount = ReadU16(q, kSP_LeafCount);
    sp.solid = ReadU8(q, kSP_Solid);
    sp.flags = ReadU8(q, kSP_Flags);
    // Fields past the prefix: read only when the per-prop stride covers them.
    sp.skin = (stride >= kSP_Skin + 4) ? ReadI32(q, kSP_Skin) : 0;
    sp.fadeMinDist =
        (stride >= kSP_FadeMinDist + 4) ? ReadF32(q, kSP_FadeMinDist) : 0.0f;
    sp.fadeMaxDist =
        (stride >= kSP_FadeMaxDist + 4) ? ReadF32(q, kSP_FadeMaxDist) : 0.0f;
    if (stride >= kSP_LightingOrigin + 12) {
      sp.lightingOrigin[0] = ReadF32(q, kSP_LightingOrigin + 0);
      sp.lightingOrigin[1] = ReadF32(q, kSP_LightingOrigin + 4);
      sp.lightingOrigin[2] = ReadF32(q, kSP_LightingOrigin + 8);
    } else {
      sp.lightingOrigin[0] = sp.lightingOrigin[1] = sp.lightingOrigin[2] = 0.0f;
    }
    sp.forcedFadeScale = (stride >= kSP_ForcedFadeScale + 4)
                             ? ReadF32(q, kSP_ForcedFadeScale)
                             : 0.0f;
    sp.flagsEx = (stride >= kSP_FlagsEx + 4) ? ReadI32(q, kSP_FlagsEx) : 0;
    g.staticProps.push_back(sp);
  }
}

} // namespace

void Shutdown() { Clear(); }

void Clear() {
  g.loaded = false;
  g.loadedMap[0] = '\0';
  g.version = 0;
  g.revision = 0;
  std::memset(g.lumps, 0, sizeof(g.lumps));
  g.entityRaw.clear();
  g.entities.clear();
  g.visLump.clear();
  g.cubemapsLump.clear();
  g.vertexesLump.clear();
  g.edgesLump.clear();
  g.surfedgesLump.clear();
  g.facesLump.clear();
  g.texinfoLump.clear();
  g.texdataLump.clear();
  g.texdataStringTable.clear();
  g.texdataStringData.clear();
  g.leafFacesLump.clear();
  g.worldlightsLump.clear();
  g.staticPropVersion = 0;
  g.staticPropNames.clear();
  g.staticPropLeafs.clear();
  g.staticProps.clear();
}

bool Loaded() { return g.loaded; }

bool LoadFromMap(const char *mapname, const char *bspPath, char *err,
                 size_t errLen) {
  if (g.loaded && mapname && std::strcmp(g.loadedMap, mapname) == 0)
    return true;
  Clear();

  FILE *f = std::fopen(bspPath, "rb");
  if (!f) {
    std::snprintf(err, errLen, "BSPLumps: fopen('%s') failed", bspPath);
    return false;
  }

  uint8_t hdr[kHeaderSize];
  if (std::fread(hdr, 1, kHeaderSize, f) != kHeaderSize) {
    std::snprintf(err, errLen, "BSPLumps: short read on header");
    std::fclose(f);
    return false;
  }

  int32_t ident;
  std::memcpy(&ident, hdr + 0, 4);
  if (ident != kIdent) {
    std::snprintf(err, errLen, "BSPLumps: bad ident 0x%X (want VBSP)", ident);
    std::fclose(f);
    return false;
  }
  std::memcpy(&g.version, hdr + 4, 4);
  for (int i = 0; i < kNumLumps; ++i) {
    int base = 8 + i * 16;
    std::memcpy(&g.lumps[i].fileofs, hdr + base + 0, 4);
    std::memcpy(&g.lumps[i].filelen, hdr + base + 4, 4);
    std::memcpy(&g.lumps[i].version, hdr + base + 8, 4);
    std::memcpy(&g.lumps[i].uncompressedLen, hdr + base + 12, 4);
  }
  std::memcpy(&g.revision, hdr + 1032, 4);

  // Read lumps we care about.
  std::vector<uint8_t> raw;

  // Entities
  if (g.lumps[LUMP_ENTITIES].filelen > 0 &&
      ReadFile(f, g.lumps[LUMP_ENTITIES].fileofs,
               g.lumps[LUMP_ENTITIES].filelen, raw)) {
    g.entityRaw.assign((const char *)raw.data(), raw.size());
    // Strip trailing null byte if present.
    while (!g.entityRaw.empty() && g.entityRaw.back() == '\0')
      g.entityRaw.pop_back();
    ParseEntities(g.entityRaw, g.entities);
  }

  // Visibility
  ReadFile(f, g.lumps[LUMP_VISIBILITY].fileofs,
           g.lumps[LUMP_VISIBILITY].filelen, g.visLump);
  // Cubemaps
  ReadFile(f, g.lumps[LUMP_CUBEMAPS].fileofs, g.lumps[LUMP_CUBEMAPS].filelen,
           g.cubemapsLump);
  // Vertexes
  ReadFile(f, g.lumps[LUMP_VERTEXES].fileofs, g.lumps[LUMP_VERTEXES].filelen,
           g.vertexesLump);
  // Edges + Surfedges
  ReadFile(f, g.lumps[LUMP_EDGES].fileofs, g.lumps[LUMP_EDGES].filelen,
           g.edgesLump);
  ReadFile(f, g.lumps[LUMP_SURFEDGES].fileofs, g.lumps[LUMP_SURFEDGES].filelen,
           g.surfedgesLump);
  // Faces
  ReadFile(f, g.lumps[LUMP_FACES].fileofs, g.lumps[LUMP_FACES].filelen,
           g.facesLump);

  // Texinfo
  ReadFile(f, g.lumps[LUMP_TEXINFO].fileofs, g.lumps[LUMP_TEXINFO].filelen,
           g.texinfoLump);
  // Texdata
  ReadFile(f, g.lumps[LUMP_TEXDATA].fileofs, g.lumps[LUMP_TEXDATA].filelen,
           g.texdataLump);
  // Texdata string table + data
  ReadFile(f, g.lumps[LUMP_TEXDATA_STRING_TABLE].fileofs,
           g.lumps[LUMP_TEXDATA_STRING_TABLE].filelen, g.texdataStringTable);
  ReadFile(f, g.lumps[LUMP_TEXDATA_STRING_DATA].fileofs,
           g.lumps[LUMP_TEXDATA_STRING_DATA].filelen, g.texdataStringData);

  // LeafFaces
  ReadFile(f, g.lumps[LUMP_LEAFFACES].fileofs, g.lumps[LUMP_LEAFFACES].filelen,
           g.leafFacesLump);

  // Worldlights: prefer LDR (most maps); fall back to HDR if no LDR.
  if (g.lumps[LUMP_WORLDLIGHTS_LDR].filelen > 0) {
    ReadFile(f, g.lumps[LUMP_WORLDLIGHTS_LDR].fileofs,
             g.lumps[LUMP_WORLDLIGHTS_LDR].filelen, g.worldlightsLump);
  } else if (g.lumps[LUMP_WORLDLIGHTS_HDR].filelen > 0) {
    ReadFile(f, g.lumps[LUMP_WORLDLIGHTS_HDR].fileofs,
             g.lumps[LUMP_WORLDLIGHTS_HDR].filelen, g.worldlightsLump);
  }

  // Static props (sprp game lump).
  // Reads sub-blobs at their own absolute offsets.
  ParseStaticProps(f);

  std::fclose(f);

  std::strncpy(g.loadedMap, mapname ? mapname : "", sizeof(g.loadedMap) - 1);
  g.loadedMap[sizeof(g.loadedMap) - 1] = '\0';
  g.loaded = true;
  return true;
}

int BSPVersion() { return g.loaded ? g.version : 0; }
int BSPRevision() { return g.loaded ? g.revision : 0; }

bool LumpInfo(int lumpId, int &outOffset, int &outLength, int &outVersion) {
  if (!g.loaded || lumpId < 0 || lumpId >= kNumLumps)
    return false;
  outOffset = g.lumps[lumpId].fileofs;
  outLength = g.lumps[lumpId].filelen;
  outVersion = g.lumps[lumpId].version;
  return true;
}

bool HasLighting() {
  if (!g.loaded)
    return false;
  return g.lumps[LUMP_LIGHTING].filelen > 0 ||
         g.lumps[LUMP_LIGHTING_HDR].filelen > 0;
}

// Entities
int EntityRawLen() { return g.loaded ? (int)g.entityRaw.size() : 0; }

int EntityRawCopy(char *buf, int maxlen) {
  if (!buf || maxlen <= 0)
    return 0;
  if (!g.loaded) {
    buf[0] = '\0';
    return 0;
  }
  int n = (int)g.entityRaw.size();
  if (n > maxlen - 1)
    n = maxlen - 1;
  std::memcpy(buf, g.entityRaw.data(), n);
  buf[n] = '\0';
  return n;
}

int EntityCount() { return g.loaded ? (int)g.entities.size() : 0; }

int EntityClassname(int idx, char *buf, int maxlen) {
  if (!buf || maxlen <= 0)
    return 0;
  buf[0] = '\0';
  if (!g.loaded || idx < 0 || idx >= (int)g.entities.size())
    return 0;
  const std::string &c = g.entities[idx].classname;
  int n = (int)c.size();
  if (n > maxlen - 1)
    n = maxlen - 1;
  std::memcpy(buf, c.data(), n);
  buf[n] = '\0';
  return n;
}

bool EntityOrigin(int idx, float out[3]) {
  if (!g.loaded || idx < 0 || idx >= (int)g.entities.size())
    return false;
  const Entity &e = g.entities[idx];
  if (!e.hasOrigin)
    return false;
  out[0] = e.origin[0];
  out[1] = e.origin[1];
  out[2] = e.origin[2];
  return true;
}

int EntityKeyValue(int idx, const char *key, char *buf, int maxlen) {
  if (!buf || maxlen <= 0)
    return 0;
  buf[0] = '\0';
  if (!g.loaded || idx < 0 || idx >= (int)g.entities.size() || !key)
    return 0;
  const auto &kv = g.entities[idx].kv;
  auto it = kv.find(key);
  if (it == kv.end())
    return 0;
  const std::string &v = it->second;
  int n = (int)v.size();
  if (n > maxlen - 1)
    n = maxlen - 1;
  std::memcpy(buf, v.data(), n);
  buf[n] = '\0';
  return n;
}

int EntityModelIndex(int idx) {
  if (!g.loaded || idx < 0 || idx >= (int)g.entities.size())
    return -1;
  const auto &kv = g.entities[idx].kv;
  auto it = kv.find("model");
  if (it == kv.end())
    return -1;
  const std::string &v = it->second;
  // Brush models are "*N", studio props are "models/....mdl".
  if (v.size() < 2 || v[0] != '*')
    return -1;
  char *end = nullptr;
  long n = std::strtol(v.c_str() + 1, &end, 10);
  if (end == v.c_str() + 1 || n < 0)
    return -1;
  return (int)n;
}

// Texinfo + Texdata
int TexInfoCount() {
  return g.loaded ? (int)(g.texinfoLump.size() / kSizeofTexInfo) : 0;
}

int TexInfoFlags(int idx) {
  if (idx < 0 || idx >= TexInfoCount())
    return 0;
  return ReadI32(g.texinfoLump.data() + (size_t)idx * kSizeofTexInfo,
                 kTexInfo_Flags);
}

int TexInfoTexData(int idx) {
  if (idx < 0 || idx >= TexInfoCount())
    return -1;
  return ReadI32(g.texinfoLump.data() + (size_t)idx * kSizeofTexInfo,
                 kTexInfo_TexData);
}

int TexDataCount() {
  return g.loaded ? (int)(g.texdataLump.size() / kSizeofTexData) : 0;
}

int TexDataMaterialName(int texdataIdx, char *buf, int maxlen) {
  if (!buf || maxlen <= 0)
    return 0;
  buf[0] = '\0';
  if (texdataIdx < 0 || texdataIdx >= TexDataCount())
    return 0;
  int nameStringTableID =
      ReadI32(g.texdataLump.data() + (size_t)texdataIdx * kSizeofTexData,
              kTexData_NameStringTableID);
  if (nameStringTableID < 0 ||
      (size_t)(nameStringTableID + 1) * 4 > g.texdataStringTable.size())
    return 0;
  int32_t stringOffset =
      ReadI32(g.texdataStringTable.data(), nameStringTableID * 4);
  if (stringOffset < 0 || (size_t)stringOffset >= g.texdataStringData.size())
    return 0;
  const char *src = (const char *)g.texdataStringData.data() + stringOffset;
  size_t maxRead = g.texdataStringData.size() - (size_t)stringOffset;
  int n = 0;
  while (n < maxlen - 1 && (size_t)n < maxRead && src[n] != '\0') {
    buf[n] = src[n];
    ++n;
  }
  buf[n] = '\0';
  return n;
}

bool TexDataReflectivity(int texdataIdx, float out[3]) {
  if (texdataIdx < 0 || texdataIdx >= TexDataCount())
    return false;
  const uint8_t *p = g.texdataLump.data() + (size_t)texdataIdx * kSizeofTexData;
  out[0] = ReadF32(p, kTexData_Reflectivity + 0);
  out[1] = ReadF32(p, kTexData_Reflectivity + 4);
  out[2] = ReadF32(p, kTexData_Reflectivity + 8);
  return true;
}

// Visibility
int VisClusterCount() {
  if (!g.loaded || g.visLump.size() < 4)
    return 0;
  return ReadI32(g.visLump.data(), 0);
}

bool ClusterVisible(int cluster, int other) {
  if (!g.loaded || g.visLump.size() < 4)
    return false;
  int numclusters = ReadI32(g.visLump.data(), 0);
  if (cluster < 0 || other < 0 || cluster >= numclusters ||
      other >= numclusters)
    return false;
  // pvs offset for this cluster: 4 + cluster*8 + 0 (pas is at +4)
  size_t ofs_table = 4 + (size_t)cluster * 8;
  if (ofs_table + 4 > g.visLump.size())
    return false;
  int pvs_ofs = ReadI32(g.visLump.data(), (int)ofs_table);
  if (pvs_ofs < 0 || (size_t)pvs_ofs >= g.visLump.size())
    return false;
  // Decompress RLE up to the byte covering 'other'.
  // byte==0: next byte = count of zero bytes to skip (each = 8 invisible
  // clusters) otherwise: literal byte covering 8 clusters
  const uint8_t *p = g.visLump.data() + pvs_ofs;
  const uint8_t *end = g.visLump.data() + g.visLump.size();
  int target_byte = other >> 3;
  int target_bit = other & 7;
  int cur = 0; // current byte index in uncompressed bitset
  while (p < end && cur <= target_byte) {
    if (*p == 0) {
      ++p;
      if (p >= end)
        break;
      cur += (int)(*p++); // skip this many zero bytes
    } else {
      if (cur == target_byte)
        return ((*p) >> target_bit) & 1;
      ++p;
      ++cur;
    }
  }
  return false;
}

// Cubemaps
int CubemapCount() {
  return g.loaded ? (int)(g.cubemapsLump.size() / kSizeofCubemap) : 0;
}

bool CubemapOrigin(int idx, float out[3]) {
  if (idx < 0 || idx >= CubemapCount())
    return false;
  const uint8_t *p = g.cubemapsLump.data() + (size_t)idx * kSizeofCubemap;
  // origin stored as int[3]; cast to float for caller
  out[0] = (float)ReadI32(p, kCubemap_Origin + 0);
  out[1] = (float)ReadI32(p, kCubemap_Origin + 4);
  out[2] = (float)ReadI32(p, kCubemap_Origin + 8);
  return true;
}

int CubemapSize(int idx) {
  if (idx < 0 || idx >= CubemapCount())
    return -1;
  return ReadI32(g.cubemapsLump.data() + (size_t)idx * kSizeofCubemap,
                 kCubemap_Size);
}

// Edges
int EdgeCount() {
  return g.loaded ? (int)(g.edgesLump.size() / kSizeofEdge) : 0;
}

bool EdgeVertices(int idx, int &v0, int &v1) {
  if (idx < 0 || idx >= EdgeCount())
    return false;
  const uint8_t *p = g.edgesLump.data() + (size_t)idx * kSizeofEdge;
  v0 = (int)ReadU16(p, 0);
  v1 = (int)ReadU16(p, 2);
  return true;
}

// Surfedges
int SurfedgeCount() {
  return g.loaded ? (int)(g.surfedgesLump.size() / kSizeofSurfedge) : 0;
}

int Surfedge(int idx) {
  if (idx < 0 || idx >= SurfedgeCount())
    return 0;
  return ReadI32(g.surfedgesLump.data(), idx * kSizeofSurfedge);
}

int SurfedgeVertex(int idx) {
  if (idx < 0 || idx >= SurfedgeCount())
    return -1;
  int se = ReadI32(g.surfedgesLump.data(), idx * kSizeofSurfedge);
  int edgeIdx = se >= 0 ? se : -se;
  if (edgeIdx >= EdgeCount())
    return -1;
  const uint8_t *p = g.edgesLump.data() + (size_t)edgeIdx * kSizeofEdge;
  // Positive surfedge = forward (v[0] is the start vertex).
  // Negative surfedge = backward (v[1] is the start vertex).
  return (int)ReadU16(p, se >= 0 ? 0 : 2);
}

// Vertexes
int VertexCount() {
  return g.loaded ? (int)(g.vertexesLump.size() / kSizeofVertex) : 0;
}

bool VertexPos(int idx, float out[3]) {
  if (idx < 0 || idx >= VertexCount())
    return false;
  const uint8_t *p = g.vertexesLump.data() + (size_t)idx * kSizeofVertex;
  out[0] = ReadF32(p, 0);
  out[1] = ReadF32(p, 4);
  out[2] = ReadF32(p, 8);
  return true;
}

// Faces
int FaceCount() {
  return g.loaded ? (int)(g.facesLump.size() / kSizeofFace) : 0;
}

static const uint8_t *FacePtr(int idx) {
  if (idx < 0 || idx >= FaceCount())
    return nullptr;
  return g.facesLump.data() + (size_t)idx * kSizeofFace;
}

int FacePlaneNum(int idx) {
  const uint8_t *p = FacePtr(idx);
  return p ? (int)ReadU16(p, kFace_PlaneNum) : -1;
}

int FaceFirstEdge(int idx) {
  const uint8_t *p = FacePtr(idx);
  return p ? ReadI32(p, kFace_FirstEdge) : -1;
}

int FaceNumEdges(int idx) {
  const uint8_t *p = FacePtr(idx);
  return p ? (int)ReadI16(p, kFace_NumEdges) : -1;
}

int FaceTexInfo(int idx) {
  const uint8_t *p = FacePtr(idx);
  return p ? (int)ReadI16(p, kFace_TexInfo) : -1;
}

int FaceDispInfo(int idx) {
  const uint8_t *p = FacePtr(idx);
  return p ? (int)ReadI16(p, kFace_DispInfo) : -1;
}

float FaceArea(int idx) {
  const uint8_t *p = FacePtr(idx);
  return p ? ReadF32(p, kFace_Area) : 0.0f;
}

bool FaceLightStyles(int idx, uint8_t out[4]) {
  const uint8_t *p = FacePtr(idx);
  if (!p)
    return false;
  out[0] = ReadU8(p, kFace_Styles + 0);
  out[1] = ReadU8(p, kFace_Styles + 1);
  out[2] = ReadU8(p, kFace_Styles + 2);
  out[3] = ReadU8(p, kFace_Styles + 3);
  return true;
}

int FaceOrigFace(int idx) {
  const uint8_t *p = FacePtr(idx);
  return p ? ReadI32(p, kFace_OrigFace) : -1;
}

int FaceLightOfs(int idx) {
  const uint8_t *p = FacePtr(idx);
  return p ? ReadI32(p, kFace_LightOfs) : -1;
}

// Convenience compound queries
bool FaceVertex(int faceIdx, int slot, float out[3]) {
  int first = FaceFirstEdge(faceIdx);
  int num = FaceNumEdges(faceIdx);
  if (first < 0 || num <= 0 || slot < 0 || slot >= num)
    return false;
  int vertIdx = SurfedgeVertex(first + slot);
  return VertexPos(vertIdx, out);
}

bool FaceCentroid(int faceIdx, float out[3]) {
  int first = FaceFirstEdge(faceIdx);
  int num = FaceNumEdges(faceIdx);
  if (first < 0 || num <= 0)
    return false;
  float sum[3] = {0, 0, 0};
  int count = 0;
  for (int i = 0; i < num; ++i) {
    float v[3];
    int vertIdx = SurfedgeVertex(first + i);
    if (vertIdx >= 0 && VertexPos(vertIdx, v)) {
      sum[0] += v[0];
      sum[1] += v[1];
      sum[2] += v[2];
      ++count;
    }
  }
  if (count == 0)
    return false;
  float inv = 1.0f / (float)count;
  out[0] = sum[0] * inv;
  out[1] = sum[1] * inv;
  out[2] = sum[2] * inv;
  return true;
}

int FaceMaterialName(int faceIdx, char *buf, int maxlen) {
  if (!buf || maxlen <= 0)
    return 0;
  buf[0] = '\0';
  int texInfoIdx = FaceTexInfo(faceIdx);
  if (texInfoIdx < 0)
    return 0;
  int texDataIdx = TexInfoTexData(texInfoIdx);
  if (texDataIdx < 0)
    return 0;
  return TexDataMaterialName(texDataIdx, buf, maxlen);
}

int NearestCubemap(const float pos[3]) {
  int count = CubemapCount();
  if (count == 0)
    return -1;
  int best = -1;
  float bestDist2 = 0.0f;
  for (int i = 0; i < count; ++i) {
    float origin[3];
    if (!CubemapOrigin(i, origin))
      continue;
    float dx = origin[0] - pos[0];
    float dy = origin[1] - pos[1];
    float dz = origin[2] - pos[2];
    float d2 = dx * dx + dy * dy + dz * dz;
    if (best < 0 || d2 < bestDist2) {
      best = i;
      bestDist2 = d2;
    }
  }
  return best;
}

int FindEntityByKeyValue(const char *key, const char *value, int startIdx) {
  if (!g.loaded || !key || !value)
    return -1;
  int count = (int)g.entities.size();
  for (int i = (startIdx < 0 ? 0 : startIdx); i < count; ++i) {
    auto it = g.entities[i].kv.find(key);
    if (it != g.entities[i].kv.end() && it->second == value)
      return i;
  }
  return -1;
}

// LeafFaces
int LeafFacesCount() {
  return g.loaded ? (int)(g.leafFacesLump.size() / 2) : 0;
}

int LeafFacesRange(int firstFace, int numFaces, int *outBuf, int maxOut) {
  if (!outBuf || maxOut <= 0)
    return 0;
  int total = LeafFacesCount();
  if (firstFace < 0 || numFaces <= 0 || firstFace >= total)
    return 0;
  if (firstFace + numFaces > total)
    numFaces = total - firstFace;
  int n = numFaces > maxOut ? maxOut : numFaces;
  const uint16_t *table = (const uint16_t *)g.leafFacesLump.data();
  for (int i = 0; i < n; ++i)
    outBuf[i] = (int)table[firstFace + i];
  return n;
}

// Worldlights
int WorldlightCount() {
  return g.loaded ? (int)(g.worldlightsLump.size() / kSizeofWorldlight) : 0;
}

static const uint8_t *WLPtr(int idx) {
  if (idx < 0 || idx >= WorldlightCount())
    return nullptr;
  return g.worldlightsLump.data() + (size_t)idx * kSizeofWorldlight;
}

bool WorldlightOrigin(int idx, float out[3]) {
  const uint8_t *p = WLPtr(idx);
  if (!p)
    return false;
  out[0] = ReadF32(p, kWL_Origin + 0);
  out[1] = ReadF32(p, kWL_Origin + 4);
  out[2] = ReadF32(p, kWL_Origin + 8);
  return true;
}

bool WorldlightIntensity(int idx, float out[3]) {
  const uint8_t *p = WLPtr(idx);
  if (!p)
    return false;
  out[0] = ReadF32(p, kWL_Intensity + 0);
  out[1] = ReadF32(p, kWL_Intensity + 4);
  out[2] = ReadF32(p, kWL_Intensity + 8);
  return true;
}

bool WorldlightNormal(int idx, float out[3]) {
  const uint8_t *p = WLPtr(idx);
  if (!p)
    return false;
  out[0] = ReadF32(p, kWL_Normal + 0);
  out[1] = ReadF32(p, kWL_Normal + 4);
  out[2] = ReadF32(p, kWL_Normal + 8);
  return true;
}

int WorldlightType(int idx) {
  const uint8_t *p = WLPtr(idx);
  return p ? ReadI32(p, kWL_Type) : -1;
}

int WorldlightStyle(int idx) {
  const uint8_t *p = WLPtr(idx);
  return p ? ReadI32(p, kWL_Style) : -1;
}

int WorldlightCluster(int idx) {
  const uint8_t *p = WLPtr(idx);
  return p ? ReadI32(p, kWL_Cluster) : -1;
}

bool WorldlightShadowCastOffset(int idx, float out[3]) {
  const uint8_t *p = WLPtr(idx);
  if (!p)
    return false;
  out[0] = ReadF32(p, kWL_ShadowOffset + 0);
  out[1] = ReadF32(p, kWL_ShadowOffset + 4);
  out[2] = ReadF32(p, kWL_ShadowOffset + 8);
  return true;
}

float WorldlightStopDot(int idx) {
  const uint8_t *p = WLPtr(idx);
  return p ? ReadF32(p, kWL_StopDot) : 0.0f;
}
float WorldlightStopDot2(int idx) {
  const uint8_t *p = WLPtr(idx);
  return p ? ReadF32(p, kWL_StopDot2) : 0.0f;
}
float WorldlightExponent(int idx) {
  const uint8_t *p = WLPtr(idx);
  return p ? ReadF32(p, kWL_Exponent) : 0.0f;
}
float WorldlightRadius(int idx) {
  const uint8_t *p = WLPtr(idx);
  return p ? ReadF32(p, kWL_Radius) : 0.0f;
}
float WorldlightConstantAttn(int idx) {
  const uint8_t *p = WLPtr(idx);
  return p ? ReadF32(p, kWL_ConstantAttn) : 0.0f;
}
float WorldlightLinearAttn(int idx) {
  const uint8_t *p = WLPtr(idx);
  return p ? ReadF32(p, kWL_LinearAttn) : 0.0f;
}
float WorldlightQuadraticAttn(int idx) {
  const uint8_t *p = WLPtr(idx);
  return p ? ReadF32(p, kWL_QuadraticAttn) : 0.0f;
}
int WorldlightFlags(int idx) {
  const uint8_t *p = WLPtr(idx);
  return p ? ReadI32(p, kWL_Flags) : 0;
}
int WorldlightTexInfo(int idx) {
  const uint8_t *p = WLPtr(idx);
  return p ? ReadI32(p, kWL_TexInfo) : -1;
}
int WorldlightOwner(int idx) {
  const uint8_t *p = WLPtr(idx);
  return p ? ReadI32(p, kWL_Owner) : -1;
}

// Static props (sprp game lump)
int StaticPropCount() { return g.loaded ? (int)g.staticProps.size() : 0; }

int StaticPropVersion() { return g.loaded ? g.staticPropVersion : 0; }

bool StaticPropOrigin(int idx, float out[3]) {
  if (idx < 0 || idx >= StaticPropCount())
    return false;
  const StaticProp &sp = g.staticProps[idx];
  out[0] = sp.origin[0];
  out[1] = sp.origin[1];
  out[2] = sp.origin[2];
  return true;
}

bool StaticPropAngles(int idx, float out[3]) {
  if (idx < 0 || idx >= StaticPropCount())
    return false;
  const StaticProp &sp = g.staticProps[idx];
  out[0] = sp.angles[0];
  out[1] = sp.angles[1];
  out[2] = sp.angles[2];
  return true;
}

int StaticPropSolid(int idx) {
  if (idx < 0 || idx >= StaticPropCount())
    return -1;
  return g.staticProps[idx].solid;
}

int StaticPropFlags(int idx) {
  if (idx < 0 || idx >= StaticPropCount())
    return -1;
  return g.staticProps[idx].flags;
}

int StaticPropSkin(int idx) {
  if (idx < 0 || idx >= StaticPropCount())
    return 0;
  return g.staticProps[idx].skin;
}

bool StaticPropFadeDist(int idx, float &outMin, float &outMax) {
  if (idx < 0 || idx >= StaticPropCount())
    return false;
  outMin = g.staticProps[idx].fadeMinDist;
  outMax = g.staticProps[idx].fadeMaxDist;
  return true;
}

float StaticPropForcedFadeScale(int idx) {
  if (idx < 0 || idx >= StaticPropCount())
    return 0.0f;
  return g.staticProps[idx].forcedFadeScale;
}

bool StaticPropLightingOrigin(int idx, float out[3]) {
  if (idx < 0 || idx >= StaticPropCount())
    return false;
  const StaticProp &sp = g.staticProps[idx];
  out[0] = sp.lightingOrigin[0];
  out[1] = sp.lightingOrigin[1];
  out[2] = sp.lightingOrigin[2];
  return true;
}

int StaticPropFlagsEx(int idx) {
  if (idx < 0 || idx >= StaticPropCount())
    return 0;
  return g.staticProps[idx].flagsEx;
}

int StaticPropModelName(int idx, char *buf, int maxlen) {
  if (!buf || maxlen <= 0)
    return 0;
  buf[0] = '\0';
  if (idx < 0 || idx >= StaticPropCount())
    return 0;
  int dictIdx = g.staticProps[idx].propType;
  if (dictIdx < 0 || dictIdx >= (int)g.staticPropNames.size())
    return 0;
  const std::string &name = g.staticPropNames[dictIdx];
  int n = 0;
  while (n < maxlen - 1 && (size_t)n < name.size()) {
    buf[n] = name[n];
    ++n;
  }
  buf[n] = '\0';
  return n;
}

// Fills outBuf with the BSP leaf indices this prop touches.
// Returns count written.
int StaticPropLeaves(int idx, int *outBuf, int maxOut) {
  if (idx < 0 || idx >= StaticPropCount() || !outBuf || maxOut <= 0)
    return 0;
  const StaticProp &sp = g.staticProps[idx];
  int n = 0;
  for (int i = 0; i < sp.leafCount && n < maxOut; ++i) {
    size_t li = (size_t)sp.firstLeaf + i;
    if (li >= g.staticPropLeafs.size())
      break;
    outBuf[n++] = g.staticPropLeafs[li];
  }
  return n;
}

// Index of the static prop whose origin is nearest to pos within maxDist
// (<= 0 = unlimited). -1 if none.
int NearestStaticProp(const float pos[3], float maxDist) {
  int best = -1;
  float bestSq = (maxDist > 0.0f) ? maxDist * maxDist : 3.4e38f;
  int count = StaticPropCount();
  for (int i = 0; i < count; ++i) {
    const StaticProp &sp = g.staticProps[i];
    float dx = sp.origin[0] - pos[0];
    float dy = sp.origin[1] - pos[1];
    float dz = sp.origin[2] - pos[2];
    float d2 = dx * dx + dy * dy + dz * dz;
    if (d2 < bestSq) {
      bestSq = d2;
      best = i;
    }
  }
  return best;
}

} // namespace BSPLumps
