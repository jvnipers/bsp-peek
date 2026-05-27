#include "extension.h"
#include "bsp_data.h"
#include "bsp_disp.h"
#include <IGameHelpers.h>

BSPPeek g_BSPPeek;
SMEXT_LINK(&g_BSPPeek);

bool BSPPeek::SDK_OnLoad(char *error, size_t maxlen, bool late) {
  IGameConfig *gameconf = nullptr;
  if (!gameconfs->LoadGameConfigFile("bsppeek.games", &gameconf, error, maxlen))
    return false;

  if (!BSPData::Init(gameconf, error, maxlen)) {
    gameconfs->CloseGameConfigFile(gameconf);
    return false;
  }

  char dispEngErr[256] = {0};
  if (!BSPDisp::InitEngine(gameconf, dispEngErr, sizeof(dispEngErr))) {
    smutils->LogMessage(myself, "Engine displacement reader disabled: %s",
                        dispEngErr);
  } else {
    smutils->LogMessage(myself, "Engine displacement reader initialized");
  }

  gameconfs->CloseGameConfigFile(gameconf);

  sharesys->AddNatives(myself, g_BSPNatives);
  sharesys->RegisterLibrary(myself, "bsppeek");

  void *base = BSPData::DebugGetBase();
  smutils->LogMessage(
      myself,
      "Loaded. g_BSPData base = %p  | offs: brushes %d/%d  planes %d/%d"
      "  leafs %d/%d",
      base, BSPData::DebugGetOff("numbrushes"),
      BSPData::DebugGetOff("map_brushes"), BSPData::DebugGetOff("numplanes"),
      BSPData::DebugGetOff("map_planes"), BSPData::DebugGetOff("numleafs"),
      BSPData::DebugGetOff("map_leafs"));
  return true;
}

void BSPPeek::SDK_OnUnload() {
  BSPData::Shutdown();
  BSPDisp::Clear();
  BSPDisp::ShutdownEngine();
}

void BSPPeek::SDK_OnAllLoaded() {}

bool BSPPeek::QueryRunning(char *error, size_t maxlen) { return true; }

void BSPPeek::OnCoreMapStart(edict_t *pEdictList, int edictCount,
                             int clientMax) {
  BSPData::OnMapStart();

  const char *mapname = gamehelpers->GetCurrentMap();
  if (!mapname || !mapname[0])
    return;
  char bspPath[260];
  smutils->BuildPath(Path_Game, bspPath, sizeof(bspPath), "maps/%s.bsp",
                     mapname);
  char err[256] = {0};
  if (BSPDisp::EnsureLoaded(mapname, bspPath, err, sizeof(err))) {
    smutils->LogMessage(myself, "Loaded %d displacements for map '%s'",
                        BSPDisp::DiskCount(), mapname);
  } else {
    smutils->LogError(myself, "Displacement load failed: %s", err);
  }
}
