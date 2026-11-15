#ifndef _INCLUDE_BSPPEEK_BSP_SEAM_H_
#define _INCLUDE_BSPPEEK_BSP_SEAM_H_

// Main thread only.
namespace BSPSeam
{
	enum
	{
		CATCH_STANDING = 1,
		CATCH_DUCKED = 2,
		CATCH_WALL_NON_AXIAL = 4, // the wall stopping the hull has plane type >= PLANE_Z
		CATCH_UNDER_CEILING = 8,  // head: a flat overhang over the hull
	};

	// Runs movement's traces, through its trace list, over a grid of arrivals at the wall:
	// a catch is a floor at fraction 0 with no ground under the hull after the next bump.
	// head: the hull rises into the seam; a catch is a ceiling at fraction 0.
	// -1 unavailable, else CATCH_* bits. outStart: origin and velocity of the first catch.
	int Catch(const float wallPos[3], const float normal[3], float seamZ, float frametime, float gravity, float airaccelerate, float maxspeed,
			  float outSpeed[2], float outStart[6], bool head);

	// Texturebug at a box brush's bottom edge: a hull one float step off the face, its top passing the edge, gets the box's top at fraction 0.
	// Movement's trace list, so brush order counts. -1 unavailable, else CATCH_STANDING / CATCH_DUCKED; outInfo: height, gap.
	int TexturebugCatch(const float wallPos[3], const float normal[3], float edgeZ, float frametime, float gravity, float maxspeed, float outInfo[2]);

	enum
	{
		EDGE_TOP = 1,       // else a bottom edge
		EDGE_CONTINUES = 2, // a face flush with this one goes on past the edge
		EDGE_NONWORLD = 4,  // that face is a displacement's, a prop's or an entity's
	};

	// EDGE_POINT_FLOATS a point: x, y, z, flat normal x, y, edge index, EDGE_* bits, then the ends (x, y) of the stretch of edge
	// it stands for, so touching stretches can be drawn as one line. Top edges only under a displacement, prop or entity:
	// a world brush above has its own bottom edge. Built on the first call per map and spacing; returns the total.
	constexpr int EDGE_POINT_FLOATS = 11;
	int WallEdgePoints(float spacing, int first, float *out, int maxOut);
	void OnMapEnd();

	// Sorted heights where the wall column changes:
	// brush edges, displacement vertices, prop and entity bounds, and where a ray into the wall meets another surface, entity or depth.
	int Candidates(const float wallPos[3], const float normal[3], float zMin, float zMax, float *out, int maxOut);
} // namespace BSPSeam

#endif
