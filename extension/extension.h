#ifndef _INCLUDE_BSPPEEK_EXTENSION_H_
#define _INCLUDE_BSPPEEK_EXTENSION_H_

#include "smsdk_ext.h"

class BSPPeek : public SDKExtension
{
public:
	bool SDK_OnLoad(char *error, size_t maxlen, bool late) override;
	void SDK_OnUnload() override;
	void SDK_OnAllLoaded() override;
	bool QueryRunning(char *error, size_t maxlen) override;

	// Notified when a new map's BSP collision data is ready to query.
	void OnCoreMapStart(edict_t *pEdictList, int edictCount, int clientMax) override;
};

extern BSPPeek g_BSPPeek;
extern const sp_nativeinfo_t g_BSPNatives[];

#endif
