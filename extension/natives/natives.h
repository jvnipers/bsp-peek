#ifndef _INCLUDE_BSPPEEK_NATIVES_H_
#define _INCLUDE_BSPPEEK_NATIVES_H_

#include "extension.h"
#include <sp_vm_api.h>

// Load this map's file data on first use (one attempt per map).
void EnsureLumpsLoaded();
void EnsureDispLoaded();

// Caps plugin-supplied buffer sizes so a bogus value can't throw bad_alloc.
static inline int out_len(cell_t n)
{
	const int kMax = 1 << 24;
	return n > kMax ? kMax : n;
}

static inline void cell_to_float3(IPluginContext *pCtx, cell_t addr, float out[3])
{
	cell_t *src = nullptr;
	pCtx->LocalToPhysAddr(addr, &src);
	out[0] = sp_ctof(src[0]);
	out[1] = sp_ctof(src[1]);
	out[2] = sp_ctof(src[2]);
}

// Write a float[3] back to a plugin-supplied by-ref cell array.
static inline void float3_to_cell(IPluginContext *pCtx, cell_t addr, const float v[3])
{
	cell_t *out = nullptr;
	pCtx->LocalToPhysAddr(addr, &out);
	if (out == pCtx->GetNullRef(SP_NULL_VECTOR))
	{
		return; // NULL_VECTOR
	}
	for (int i = 0; i < 3; ++i)
	{
		out[i] = sp_ftoc(v[i]);
	}
}

// Like float3_to_cell, but writes zeros when the producing call failed.
static inline void float3_to_cell_ok(IPluginContext *pCtx, cell_t addr, const float v[3], bool ok)
{
	cell_t *out = nullptr;
	pCtx->LocalToPhysAddr(addr, &out);
	if (out == pCtx->GetNullRef(SP_NULL_VECTOR))
	{
		return;
	}
	for (int i = 0; i < 3; ++i)
	{
		out[i] = sp_ftoc(ok ? v[i] : 0.0f);
	}
}

// Returns bytes actually written. The producers' snprintf lengths overshoot on truncation.
static cell_t string_to_cell(IPluginContext *pCtx, cell_t addr, int maxlen, const char *src)
{
	size_t written = 0;
	pCtx->StringToLocalUTF8(addr, maxlen, src, &written);
	return (cell_t)written;
}

#endif
