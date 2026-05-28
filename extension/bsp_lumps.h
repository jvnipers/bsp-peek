#ifndef _INCLUDE_BSPPEEK_BSP_LUMPS_H_
#define _INCLUDE_BSPPEEK_BSP_LUMPS_H_

#include "smsdk_ext.h"
#include <cstdint>

// BSP file lump readers. Parses on map load, exposes via accessors.
// Source: BSP file format (LUMP_*).
namespace BSPLumps {

// Lifecycle
void Shutdown();
// Reloads all lumps from given .bsp path. Idempotent per mapname.
bool LoadFromMap(const char *mapname, const char *bspPath, char *err,
                 size_t errLen);
void Clear();
bool Loaded();

// BSP header
int BSPVersion();
int BSPRevision(); // map revision (vbsp -version)
// Returns lump file-offset/length/version into out vars. lumpId in [0, 64).
bool LumpInfo(int lumpId, int &outOffset, int &outLength, int &outVersion);

// Entities (LUMP_ENTITIES = 0, raw text)
int EntityRawLen();
int EntityRawCopy(char *buf, int maxlen); // returns bytes copied (incl. null)
int EntityCount();
int EntityClassname(int idx, char *buf, int maxlen);
bool EntityOrigin(int idx, float out[3]); // false if "origin" missing
int EntityKeyValue(int idx, const char *key, char *buf, int maxlen);

// Texinfo (LUMP_TEXINFO = 18) + Texdata (LUMP_TEXDATA = 2)
int TexInfoCount();
int TexInfoFlags(int idx);   // SURF_* flags
int TexInfoTexData(int idx); // texdata index, -1 if invalid
int TexDataCount();
int TexDataMaterialName(int texdataIdx, char *buf, int maxlen);
bool TexDataReflectivity(int texdataIdx, float out[3]);

// LeafFaces (LUMP_LEAFFACES = 16, uint16 array)
int LeafFacesCount();
// Read leaf-face indices for leaf at firstFace..firstFace+numFaces from
// BSPData. Returns count written (clamped to maxOut). Uses caller-supplied
// first/num so this is a pure lookup against the leaffaces table.
int LeafFacesRange(int firstFace, int numFaces, int *outBuf, int maxOut);

// Worldlights (LUMP_WORLDLIGHTS = 15, LDR; LUMP_WORLDLIGHTS_HDR = 54)
int WorldlightCount(); // total across both LDR + HDR if present (LDR first)
// Fills basic fields. Returns false if invalid.
bool WorldlightOrigin(int idx, float out[3]);
bool WorldlightIntensity(int idx, float out[3]);
bool WorldlightNormal(int idx, float out[3]);
int WorldlightType(int idx);  // emit_t: 0=surface 1=point 2=spot 3=skylight 4=quake 5=skyambient
int WorldlightStyle(int idx); // light style index
int WorldlightCluster(int idx);
// Extended fields (per dworldlight_t in CSGO; offsets beyond cluster+style)
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

} // namespace BSPLumps

#endif
