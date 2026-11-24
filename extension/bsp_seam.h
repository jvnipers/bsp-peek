#ifndef _INCLUDE_BSPPEEK_BSP_SEAM_H_
#define _INCLUDE_BSPPEEK_BSP_SEAM_H_

#include <vector>

// Main thread only.
namespace BSPSeam
{
	enum
	{
		CATCH_STANDING = 1,
		CATCH_DUCKED = 2,
		CATCH_WALL_NON_AXIAL = 4, // the wall stopping the hull has plane type >= PLANE_Z
		CATCH_EXACT = 8,          // pixelwalk for a setpos or teleport:
								  // PixelwalkCatch's hull stands only closer than a creep gets, PixelwalkEnd tests only on the face
	};

	// Runs movement's traces, through its trace list, over a grid of arrivals at the wall:
	// a catch is a floor at fraction 0 with no ground under the hull after the next bump.
	// head: the hull rises into the seam; a catch is a ceiling at fraction 0.
	// -1 unavailable, else CATCH_* bits. outStart: origin and velocity of the first catch.
	int Catch(const float wallPos[3], const float normal[3], float seamZ, float frametime, float gravity, float airaccelerate, float maxspeed,
			  float outSpeed[2], float outStart[6], bool head);

	// Texturebug at a box brush's bottom edge:
	// a hull the first clear float step off the face, its top passing the edge, gets the box's top at fraction 0.
	// Movement's trace list, so brush order counts; no start within 2u of ground; not on a prop's face.
	// -1 unavailable, else CATCH_STANDING / CATCH_DUCKED; outInfo: height, gap.
	int TexturebugCatch(const float wallPos[3], const float normal[3], float edgeZ, float frametime, float gravity, float maxspeed, float outInfo[2]);

	// Pixelwalk at a top edge:
	// a hull against the face 1u up (0.1u in front of wallPos to 1u behind, leaning back up to a ramp, a prop's too),
	// feet at edgeZ + 1/32, stands where 1u further out it neither stands nor lands from 1u up:
	// as close as a creep gets (0.9 ε off a prop's face), else only closer,
	// exactly on an axial face or two float steps past where a prop's starts clear (CATCH_EXACT).
	// -1 unavailable, else CATCH_STANDING / CATCH_DUCKED (| CATCH_EXACT);
	// outInfo: the strip's width, the feet's height, the origin's x and y (the standing hull's, else the ducked one's).
	int PixelwalkCatch(const float wallPos[3], const float normal[3], float edgeZ, float outInfo[4]);

	// Pixelwalk at the end of a covered top edge: a hull exactly on the face, slid from 17u off the end to 17u along the edge,
	// held (ground or stuck) from one point and stuck in the cover from a later one: the strip between; not on a prop's face.
	// along: into the edge.
	// -1 unavailable, else CATCH_STANDING / CATCH_DUCKED | CATCH_EXACT;
	// out: the strip's middle (origin x, y) and width, the standing hull's (else the ducked one's).
	int PixelwalkEnd(const float endPos[3], const float normal[3], const float along[2], float edgeZ, float out[3]);

	enum
	{
		EDGE_TOP = 1,       // else a bottom edge
		EDGE_CONTINUES = 2, // a face flush with this one goes on past the edge
		EDGE_NONWORLD = 4,  // that face is a displacement's, a prop's or an entity's
		EDGE_LEDGE = 8,     // why a top edge is listed: the face above set back 0.25 to 1u, or by more than float noise and under 1/64u;
							// or a displacement floor side or wall top end under a wall within 1u
		EDGE_END = 16,      // a covered top edge's end where no top edge on its line goes on past it, its cell 1u along it (PixelwalkEnd)
		EDGE_RAMP = 32,     // why a top edge is listed: a face leaning back, or a level row inside a displacement one
		EDGE_RESUMES = 64,  // no face flush above the top edge, but a hull over it within 1u of the face's plane meets something
		EDGE_ROW = 128,     // a row inside a displacement wall (a pixelwalk test too), its height where the point is
	};

	// EDGE_POINT_FLOATS a point: x, y, z, flat normal x, y, edge index, EDGE_* bits, then the ends (x, y) of the stretch of edge
	// it stands for, so touching stretches can be drawn as one line. Edges of world brushes, static brush entities, displacements and
	// props (bsppeek/seam.inc has which). The first call per map and spacing starts a build that EdgeBuildStep runs in steps;
	// until it's done, -1. Returns the total.
	constexpr int EDGE_POINT_FLOATS = 11;
	int WallEdgePoints(float spacing, int first, float *out, int maxOut);
	// Runs the edge build for up to `seconds`; false when none is left.
	bool EdgeBuildStep(double seconds);
	// The brushes of brush model `model` (0 the world), in their model's space.
	void ModelBrushes(int model, std::vector<int> &out);
	void OnMapEnd();

	// Sorted heights where the wall column changes:
	// brush edges, tops of faces leaning back the wall's way, displacement vertices, prop and entity bounds and collision mesh corners,
	// brush entities' brush edges, and where a ray into the wall meets another surface, entity or depth.
	int Candidates(const float wallPos[3], const float normal[3], float zMin, float zMax, float *out, int maxOut);
} // namespace BSPSeam

#endif
