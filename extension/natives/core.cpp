#include "natives.h"
#include "bsp_data.h"
#include "bsp_disp.h"
#include "bsp_lumps.h"
#include "bsp_props.h"
#include <IGameHelpers.h>
#include <cstdio>
#include <cstring>

void EnsureLumpsLoaded()
{
	// By name: a load between map end and the next map's OnMapInit holds the previous map.
	const char *mapname = gamehelpers->GetCurrentMap();
	if (!mapname || !mapname[0] || BSPLumps::IsLoaded(mapname))
	{
		return;
	}
	// One attempt per map: natives can run every tick.
	static char s_failedMap[128] = {0};
	if (std::strcmp(s_failedMap, mapname) == 0)
	{
		return;
	}
	char bspPath[260];
	smutils->BuildPath(Path_Game, bspPath, sizeof(bspPath), "maps/%s.bsp", mapname);
	char err[256] = {0};
	if (!BSPLumps::LoadFromMap(mapname, bspPath, err, sizeof(err)))
	{
		smutils->LogError(myself, "Lump load failed for '%s': %s", mapname, err);
		std::strncpy(s_failedMap, mapname, sizeof(s_failedMap) - 1);
	}
}

void EnsureDispLoaded()
{
	const char *mapname = gamehelpers->GetCurrentMap();
	if (!mapname || !mapname[0] || BSPDisp::IsLoaded(mapname))
	{
		return;
	}
	static char s_failedMap[128] = {0};
	if (std::strcmp(s_failedMap, mapname) == 0)
	{
		return;
	}
	char bspPath[260];
	smutils->BuildPath(Path_Game, bspPath, sizeof(bspPath), "maps/%s.bsp", mapname);
	char err[256] = {0};
	if (!BSPDisp::EnsureLoaded(mapname, bspPath, err, sizeof(err)))
	{
		smutils->LogError(myself, "Displacement load failed for '%s': %s", mapname, err);
		std::strncpy(s_failedMap, mapname, sizeof(s_failedMap) - 1);
	}
	else if (BSPDisp::DiskCount() > 0)
	{
		static char s_lastReportedMap[128] = {0};
		if (std::strcmp(s_lastReportedMap, mapname) != 0)
		{
			smutils->LogMessage(myself, "Loaded %d displacements for map '%s'", BSPDisp::DiskCount(), mapname);
			std::strncpy(s_lastReportedMap, mapname, sizeof(s_lastReportedMap) - 1);
		}
	}
}

// Aggregate health check across all subsystems.
// Distinguishes "map has no data" from "a signature broke after an update".
//   bits 0..3 : BSPData (see BSPData::SelfTest)
//   bit 4 (0x10): displacement engine reader ready
//   bit 5 (0x20): static-prop engine interfaces resolved
//   bit 6 (0x40): disk BSP lumps loaded for the current map
cell_t N_SelfTest(IPluginContext *, const cell_t *)
{
	int mask = BSPData::SelfTest();
	if (BSPDisp::EngineReady())
	{
		mask |= 0x10;
	}
	if (BSPProps::Ready() || BSPProps::SweepReady())
	{
		mask |= 0x20;
	}
	EnsureLumpsLoaded();
	if (BSPLumps::Loaded())
	{
		mask |= 0x40;
	}
	return mask;
}

// Engine build / patch version from the game dir's steam.inf (plain text, no engine interface).
// The datum for diagnosing gamedata sig drift after an update.
// Returns ServerVersion (0 if unreadable), writes a summary into buf.
cell_t N_EngineBuild(IPluginContext *pCtx, const cell_t *params)
{
	int maxlen = out_len(params[2]);
	char path[300];
	smutils->BuildPath(Path_Game, path, sizeof(path), "steam.inf");
	int serverVersion = 0;
	char patch[64] = {0};
	char product[64] = {0};
	FILE *f = fopen(path, "rb");
	if (f)
	{
		char line[256];
		while (fgets(line, sizeof(line), f))
		{
			int v;
			if (sscanf(line, "ServerVersion=%d", &v) == 1)
			{
				serverVersion = v;
			}
			else if (!strncmp(line, "PatchVersion=", 13))
			{
				sscanf(line + 13, "%63[^\r\n]", patch);
			}
			else if (!strncmp(line, "ProductName=", 12))
			{
				sscanf(line + 12, "%63[^\r\n]", product);
			}
		}
		fclose(f);
	}
	if (maxlen > 0)
	{
		char buf[256];
		snprintf(buf, sizeof(buf), "ServerVersion=%d PatchVersion=%s ProductName=%s", serverVersion, patch[0] ? patch : "?",
				 product[0] ? product : "?");
		pCtx->StringToLocal(params[1], maxlen, buf);
	}
	return serverVersion;
}

extern const sp_nativeinfo_t g_CoreNatives[] = {
	// Misc
	{"BSP_SelfTest", N_SelfTest},
	{"BSP_EngineBuild", N_EngineBuild},

	{nullptr, nullptr},
};
