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
		CATCH_EXACT = 16,         // pixelwalk: only with the hull exactly on the face
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

	// Pixelwalk at a top edge: a hull pressed against the face, feet at edgeZ + 1/32, stands where 1u further out it doesn't:
	// one float step off (a creep), else only exactly on the face with the top not standable 1u further in (CATCH_EXACT).
	// -1 unavailable, else CATCH_STANDING / CATCH_DUCKED (| CATCH_EXACT);
	// outInfo: the strip's width, the feet's height, the standing origin's x and y.
	int PixelwalkCatch(const float wallPos[3], const float normal[3], float edgeZ, float outInfo[4]);

	// Pixelwalk at the end of a covered top edge: a hull exactly on the face, slid from 17u off the end to 17u along the edge,
	// held (ground or stuck) from one point and stuck in the cover from a later one: the strip between. along: into the edge.
	// -1 unavailable, else CATCH_STANDING / CATCH_DUCKED | CATCH_EXACT; out: the strip's middle (origin x, y) and width.
	int PixelwalkEnd(const float endPos[3], const float normal[3], const float along[2], float edgeZ, float out[3]);

	enum
	{
		EDGE_TOP = 1,       // else a bottom edge
		EDGE_CONTINUES = 2, // a face flush with this one goes on past the edge
		EDGE_NONWORLD = 4,  // that face is a displacement's, a prop's or an entity's
		EDGE_LEDGE = 8,     // top edge: the face above is set back 0.25 to 1u (a ledge)
		EDGE_END = 16,      // a covered top edge's end, its cell 1u along the edge from it (PixelwalkEnd)
		EDGE_RAMP = 32,     // top edge of a face leaning back (a ramp), no face flush above: only PixelwalkCatch
	};

	// EDGE_POINT_FLOATS a point: x, y, z, flat normal x, y, edge index, EDGE_* bits, then the ends (x, y) of the stretch of edge
	// it stands for, so touching stretches can be drawn as one line. Edges of world brushes, static brush entities, displacements and
	// props (bsppeek.inc has which). Built on the first call per map and spacing; returns the total.
	constexpr int EDGE_POINT_FLOATS = 11;
	int WallEdgePoints(float spacing, int first, float *out, int maxOut);
	void OnMapEnd();

	// Sorted heights where the wall column changes:
	// brush edges, displacement vertices, prop and entity bounds, and where a ray into the wall meets another surface, entity or depth.
	int Candidates(const float wallPos[3], const float normal[3], float zMin, float zMax, float *out, int maxOut);
} // namespace BSPSeam

#endif
