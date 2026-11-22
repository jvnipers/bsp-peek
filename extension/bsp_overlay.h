#ifndef _INCLUDE_BSPPEEK_BSP_OVERLAY_H_
#define _INCLUDE_BSPPEEK_BSP_OVERLAY_H_

#include "smsdk_ext.h"
#include <mutex>

// The seam tests at every BSPSeam::WallEdgePoints point, for a set of movement values (tick interval, gravity, airaccelerate, maxspeed):
// on the game thread in steps, and on a worker while the engine's game server hibernates (no game frame runs; the engine's world,
// displacement, prop and partition traces are thread-safe for a thread with a tier0 thread id). Console commands that create or
// remove entities while it hibernates are not guarded. Finished passes are saved per map, extension build, spacing and values.
namespace BSPOverlay
{
	enum
	{
		KIND_PIXELSURF = 1,
		KIND_CEILING = 2,
		KIND_HEADSEAM = 4,
		KIND_TEXTUREBUG = 8,
		KIND_PIXELWALK = 16,
		KIND_PIXELWALK_EXACT = 32,
	};

	// Points nearest origin first (null: in point order). With useCache this map's pass for these values goes on (or is done),
	// else a saved one is loaded; without, it starts over. 1 done, 0 started, -1 no points.
	int Start(const float values[4], float spacing, const float *origin, bool useCache);
	// The started pass's untested points nearest origin first.
	void Nearest(const float origin[3]);
	// Runs the pass for up to seconds; returns the points left, -1 without a pass.
	int Step(float seconds);
	// The pass's kinds per point (KIND_*, -1 untested); returns the total.
	int Kinds(int first, int *out, int maxOut);
	// Values the worker also tests on every map while the server hibernates, saved for later.
	void Queue(const float values[4], float spacing);
	// Held by the game thread around anything that builds or reads the edge points or a pass.
	std::unique_lock<std::mutex> GameThreadLock();

	// The game server's hibernation flag (gamedata "sv", "sv_hibernating") and the model cache, for the worker.
	void Init(IGameConfig *gc);
	void OnLoad(const char *extensionPath);
	void OnUnload();
	void OnMapStart(const char *mapname, const char *bspPath, const char *cacheDir);
	void OnMapEnd();
} // namespace BSPOverlay

#endif
