#pragma once

#include <cstdint>
#include <vector>

// Bit-exact port of CS:GO engine.so 740 brush clipping and movement brush order, in its instruction order.
// Needs SSE float math (-mfpmath=sse): x87 keeps excess precision.
namespace collide
{
	constexpr float DIST_EPSILON = 0.03125f;
	constexpr uint16_t NULL_SURFACE = 0xFFFF;

	struct Plane
	{
		float normal[3];
		float dist;
		uint8_t type;
		uint8_t signbits;
		uint8_t pad[2];
	};

	struct Side
	{
		const Plane *plane;
		uint16_t surfaceIndex;
		uint8_t bevel;
		uint8_t thin;
	};

	// invDelta is 1.0f / delta, FLT_MAX for a zero component.
	struct Ray
	{
		float start[3];
		float end[3];
		float extents[3];
		float delta[3];
		float invDelta[3];
		bool isPoint;
	};

	// worldSurfaceIndex is written separately from surface and can disagree with it.
	struct Trace
	{
		Plane plane;
		float fraction;
		float fractionLeftSolid;
		int contents;
		bool allSolid;
		bool startSolid;
		bool dispHit;
		uint16_t surface;
		uint16_t worldSurfaceIndex;
	};

	// Ray_t::Init plus CM_BoxTraceAgainstLeafList's TraceInfo_t setup.
	Ray MakeRay(const float start[3], const float end[3], const float mins[3], const float maxs[3]);

	void ClearTrace(Trace &tr);

	// CM_ClipBoxToBrush<false>
	void ClipBoxToBrush(const Ray &ray, Trace &tr, const Side *sides, int numSides, int contents);

	// CM_ClipBoxToBrush<true>
	void ClipRayToBrush(const Ray &ray, Trace &tr, const Side *sides, int numSides, int contents);

	// IntersectRayWithBoxBrush. surfaces in -X, -Y, -Z, +X, +Y, +Z order.
	bool ClipBoxToBoxBrush(const Ray &ray, Trace &tr, const float mins[3], const float maxs[3], const uint16_t surfaces[6], int contents);

	// A negative child is leaf -1 - child.
	struct Node
	{
		const Plane *plane;
		int children[2];
	};

	struct Leaf
	{
		int contents;
		uint16_t firstLeafBrush;
		uint16_t numLeafBrushes;
		uint16_t firstLeafDisp;
		uint16_t numLeafDisps;
	};

	struct DispBounds
	{
		float mins[3];
		float maxs[3];
		int counter;
	};

	struct Bsp
	{
		const Node *nodes;
		const Leaf *leafs;
		const uint16_t *leafBrushes;
		int numBrushes;
		const uint16_t *leafDisps;
		const DispBounds *disps;
		int numDispCounters;
	};

	struct TraceList
	{
		float mins[3];
		float maxs[3];
		std::vector<int> leafs;
		std::vector<uint16_t> brushes;
		std::vector<uint16_t> disps;
		bool foundNonSolidLeaf;
	};

	// SetupLeafAndEntityListBox's world part: player movement clips brushes in this order.
	void BuildTraceList(const Bsp &bsp, const float boxMins[3], const float boxMaxs[3], TraceList &out);

	// SetupMovementBounds. Always the standing hull, even when ducked.
	void MovementListBox(const float origin[3], const float velocity[3], float maxspeed, float frametime, float stepsize, float boxMins[3],
						 float boxMaxs[3]);

	enum class SeamOrder : uint8_t
	{
		LowerFirst,
		UpperFirst,
		LowerMissing,
		UpperMissing,
		BothMissing,
	};

	struct SeamOrderQuery
	{
		float origin[3];
		float along[3];
		float alongMin, alongMax;
		float maxspeed, frametime, stepsize;
		float speedMax; // sv_maxvelocity clamps per axis, so up to sqrt(3) * 3500
		uint16_t lower, upper;
	};

	struct SeamOrderCell
	{
		float along;
		float speedMin, speedMax;
		SeamOrder order;
	};

	// Every order the movement list can put lower and upper in, over the query's positions and speeds.
	void SeamOrders(const Bsp &bsp, const SeamOrderQuery &query, std::vector<SeamOrderCell> &out);
} // namespace collide
