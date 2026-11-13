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
		CATCH_UNDER_CEILING = 8,  // head: an overhang over the hull
	};

	// Runs movement's traces, through its trace list, over a grid of arrivals at the wall:
	// a catch is a floor at fraction 0 with no ground under the hull after the next bump.
	// head: the hull rises into the seam; a catch is a ceiling at fraction 0.
	// -1 unavailable, else CATCH_* bits. outStart: origin and velocity of the first catch.
	int Catch(const float wallPos[3], const float normal[3], float seamZ, float frametime, float gravity, float airaccelerate, float maxspeed,
			  float outSpeed[2], float outStart[6], bool head);

	// Sorted heights where the wall column changes:
	// brush edges, displacement vertices, prop and entity bounds, and where a ray into the wall meets another surface, entity or depth.
	int Candidates(const float wallPos[3], const float normal[3], float zMin, float zMax, float *out, int maxOut);
} // namespace BSPSeam

#endif
