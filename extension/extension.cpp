#include "extension.h"
#include "bsp_data.h"

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
  gameconfs->CloseGameConfigFile(gameconf);

  sharesys->AddNatives(myself, g_BSPNatives);
  sharesys->RegisterLibrary(myself, "bsppeek");

  void *base = BSPData::DebugGetBase();
  smutils->LogMessage(
      myself,
      "Loaded. g_BSPData base = %p  | offs: brushes %d/%d  planes %d/%d "
      " leafs %d/%d",
      base, BSPData::DebugGetOff("numbrushes"),
      BSPData::DebugGetOff("map_brushes"), BSPData::DebugGetOff("numplanes"),
      BSPData::DebugGetOff("map_planes"), BSPData::DebugGetOff("numleafs"),
      BSPData::DebugGetOff("map_leafs"));
  return true;
}

void BSPPeek::SDK_OnUnload() { BSPData::Shutdown(); }

void BSPPeek::SDK_OnAllLoaded() {}

bool BSPPeek::QueryRunning(char *error, size_t maxlen) { return true; }

void BSPPeek::OnMapStartHook() { BSPData::OnMapStart(); }
