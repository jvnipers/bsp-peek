#include "bsp_lumps.h"
#include "bsp_util.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

using BSPUtil::ReadF32;
using BSPUtil::ReadI32;
using BSPUtil::ReadU16;

namespace BSPLumps {

namespace {

// BSP file format constants (CSGO).
constexpr int kIdent = (('P' << 24) + ('S' << 16) + ('B' << 8) + 'V');
constexpr int kNumLumps = 64;
constexpr int kHeaderSize = 1036; // 4+4 + 16*64 + 4

constexpr int LUMP_ENTITIES = 0;
constexpr int LUMP_TEXDATA = 2;
constexpr int LUMP_LIGHTING = 8;
constexpr int LUMP_WORLDLIGHTS_LDR = 15;
constexpr int LUMP_LEAFFACES = 16;
constexpr int LUMP_TEXINFO = 18;
constexpr int LUMP_TEXDATA_STRING_DATA = 43;
constexpr int LUMP_TEXDATA_STRING_TABLE = 44;
constexpr int LUMP_LIGHTING_HDR = 53;
constexpr int LUMP_WORLDLIGHTS_HDR = 54;

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

struct State {
  bool loaded = false;
  char loadedMap[128] = {0};

  int32_t version = 0;
  int32_t revision = 0;
  LumpEntry lumps[kNumLumps] = {};

  std::string entityRaw;
  std::vector<Entity> entities;

  std::vector<uint8_t> texinfoLump;
  std::vector<uint8_t> texdataLump;
  std::vector<uint8_t> texdataStringTable; // array of int32 offsets
  std::vector<uint8_t> texdataStringData;  // packed null-terminated names

  std::vector<uint8_t> leafFacesLump; // array of uint16

  std::vector<uint8_t> worldlightsLump; // LDR or HDR (LDR preferred)
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
  g.texinfoLump.clear();
  g.texdataLump.clear();
  g.texdataStringTable.clear();
  g.texdataStringData.clear();
  g.leafFacesLump.clear();
  g.worldlightsLump.clear();
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

} // namespace BSPLumps
