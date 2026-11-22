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
		CATCH_EXACT = 16, // pixelwalk for a setpos or teleport: PixelwalkCatch's stands only exactly on the face, PixelwalkEnd tests only there
	};

	// Runs movement's traces, through its trace list, over a grid of arrivals at the wall:
	// a catch is a floor at fraction 0 with no ground under the hull after the next bump.
	// head: the hull rises into the seam; a catch is a ceiling at fraction 0.
	// -1 unavailable, else CATCH_* bits. outStart: origin and velocity of the first catch.
	int Catch(const float wallPos[3], const float normal[3], float seamZ, float frametime, float gravity, float airaccelerate, float maxspeed,
			  float outSpeed[2], float outStart[6], bool head);

	// Texturebug at a box brush's bottom edge: a hull one float step off the face, its top passing the edge, gets the box's top at fraction 0.
	// Movement's trace list, so brush order counts; no start within 2u of ground. -1 unavailable, else CATCH_STANDING / CATCH_DUCKED;
	// outInfo: height, gap.
	int TexturebugCatch(const float wallPos[3], const float normal[3], float edgeZ, float frametime, float gravity, float maxspeed, float outInfo[2]);

	// Pixelwalk at a top edge: a hull pressed against the face (up to 1u behind wallPos, leaning back up to a ramp), feet at
	// edgeZ + 1/32, stands where 1u further out it doesn't: as close as a creep gets, else only exactly on an axial face (CATCH_EXACT).
	// -1 unavailable, else CATCH_STANDING / CATCH_DUCKED (| CATCH_EXACT);
	// outInfo: the strip's width, the feet's height, the origin's x and y (the standing hull's, else the ducked one's).
	int PixelwalkCatch(const float wallPos[3], const float normal[3], float edgeZ, float outInfo[4]);

	// Pixelwalk at the end of a covered top edge: a hull exactly on the face, slid from 17u off the end to 17u along the edge,
	// held (ground or stuck) from one point and stuck in the cover from a later one: the strip between. along: into the edge.
	// -1 unavailable, else CATCH_STANDING / CATCH_DUCKED | CATCH_EXACT; out: the strip's middle (origin x, y) and width, the
	// standing hull's (else the ducked one's).
	int PixelwalkEnd(const float endPos[3], const float normal[3], const float along[2], float edgeZ, float out[3]);

	enum
	{
		EDGE_TOP = 1,       // else a bottom edge
		EDGE_CONTINUES = 2, // a face flush with this one goes on past the edge
		EDGE_NONWORLD = 4,  // that face is a displacement's, a prop's or an entity's
		EDGE_LEDGE = 8,     // top edge: the face above set back 0.25 to 1u or under 1/64u; or a displacement floor side or wall top
							// end under a wall within 1u
		EDGE_END = 16,      // a covered top edge's end where no top edge on its line goes on, its cell 1u along it (PixelwalkEnd)
		EDGE_RAMP = 32,     // top edge of a face leaning back, or a level row inside a displacement one: PixelwalkCatch too
	};

	// EDGE_POINT_FLOATS a point: x, y, z, flat normal x, y, edge index, EDGE_* bits, then the ends (x, y) of the stretch of edge
	// it stands for, so touching stretches can be drawn as one line. Edges of world brushes, static brush entities, displacements and
	// props (bsppeek/seam.inc has which). Built on the first call per map and spacing; returns the total.
	constexpr int EDGE_POINT_FLOATS = 11;
	int WallEdgePoints(float spacing, int first, float *out, int maxOut);
	void OnMapEnd();

	// Sorted heights where the wall column changes:
	// brush edges, tops of faces leaning back the wall's way, displacement vertices, prop and entity bounds, and where a ray into the
	// wall meets another surface, entity or depth.
	int Candidates(const float wallPos[3], const float normal[3], float zMin, float zMax, float *out, int maxOut);
} // namespace BSPSeam

#endif
