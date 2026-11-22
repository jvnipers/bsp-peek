#if defined _bsppeek_seam_included
    #endinput
#endif
#define _bsppeek_seam_included

// Seam, texturebug and pixelwalk tests through movement's trace list, the wall edges to run them on, and overlay passes.

// Bits returned by BSP_SeamCatch.
#define BSPP_CATCH_STANDING       1
#define BSPP_CATCH_DUCKED         2
#define BSPP_CATCH_WALL_NON_AXIAL 4     // the wall stopping the hull has plane type >= PLANE_Z
#define BSPP_CATCH_UNDER_CEILING  8     // BSP_HeadSeamCatch: a flat overhang or displacement edge over the hull
#define BSPP_CATCH_EXACT          16    // for a setpos or teleport: BSP_PixelwalkCatch's stands only exactly on the face, BSP_PixelwalkEnd tests only there

// Whether a seam at seamZ catches a player against the wall, by movement's own traces over a grid of arrivals.
// wallPos: on the face. wallNorm: horizontal unit normal. speed: the range of the catching samples' vertical speeds.
// start: origin and velocity of the first catch. Returns 0, BSPP_CATCH_* bits, or -1 unavailable.
native int BSP_SeamCatch(const float wallPos[3], const float wallNorm[3], float seamZ, float frametime, float gravity,
                         float airaccelerate, float maxspeed, float speed[2], float start[6]);

// BSP_SeamCatch for a rising player's head.
native int BSP_HeadSeamCatch(const float wallPos[3], const float wallNorm[3], float seamZ, float frametime, float gravity,
                             float airaccelerate, float maxspeed, float speed[2], float start[6]);

// Sorted heights in zMin..zMax where the wall column changes, and tops of brush faces leaning back the way the wall faces (ramps,
// walls leaning back),
// for BSP_SeamCatch and the other tests. Returns the count.
native int BSP_SeamCandidates(const float wallPos[3], const float wallNorm[3], float zMin, float zMax, float[] out, int maxOut);

// Whether a box brush's bottom edge at edgeZ gives a falling player a texturebug, by movement's own traces (brush order counts),
// from a start not within 2u of ground.
// Returns BSPP_CATCH_STANDING / BSPP_CATCH_DUCKED bits, or -1 unavailable. info: the brush's height, the smallest gap at the wall.
native int BSP_TexturebugCatch(const float wallPos[3], const float wallNorm[3], float edgeZ, float frametime, float gravity,
                               float maxspeed, float info[2]);

// Whether a hull pressed against a wall stands on a strip at edgeZ that it neither stands on nor lands on from 1u up 1u further out (a pixelwalk),
// by movement's own traces, against the face the hull meets 1u up (leaning back up to a ramp; a prop's too),
// 0.1u in front of wallPos to 1u behind: as close as a creep gets, else only exactly on an axial face (BSPP_CATCH_EXACT).
// Feet at edgeZ + 1/32.
// Returns BSPP_CATCH_STANDING / BSPP_CATCH_DUCKED bits (| BSPP_CATCH_EXACT), 0 none, -1 unavailable.
// info: the strip's width (0 when exact), then the feet's height and origin x, y of the standing hull (the ducked one if only it stands).
native int BSP_PixelwalkCatch(const float wallPos[3], const float wallNorm[3], float edgeZ, float info[4]);

// Pixelwalk at the end of a covered top edge on an axial wall (a BSPP_EDGE_END point):
// a hull exactly on the face, slid from 17u off the end along the edge, held from one point and inside the cover from a later one,
// standing in between (landing from 2u up, a 2u rise moving) where the top isn't a floor 1u into the wall or 1u out.
// along: unit, from the end into the edge.
// Returns BSPP_CATCH_STANDING / BSPP_CATCH_DUCKED | BSPP_CATCH_EXACT, 0 none, -1 unavailable.
// out: the strip's middle (origin x, y) and its width, the standing hull's (the ducked one's if only it stands).
native int BSP_PixelwalkEnd(const float endPos[3], const float wallNorm[3], const float along[3], float edgeZ, float out[3]);

// BSP_WallEdgePoints bits, the 7th float of a point.
#define BSPP_EDGE_TOP       1     // a face's top edge, else its bottom edge
#define BSPP_EDGE_CONTINUES 2     // a face goes on past the edge, flush with this one, else the wall ends there
#define BSPP_EDGE_NONWORLD  4     // with CONTINUES: the face past the edge is a displacement's, a prop's or an entity's
#define BSPP_EDGE_LEDGE     8     // top edge: the face above set back 0.25 to 1u or under 1/64u;
                                  // or a displacement floor side or wall top end under a wall within 1u (BSP_PixelwalkCatch only)
#define BSPP_EDGE_END       16    // a covered top edge's end, where no top edge on its line goes on;
                                  // its stretch runs 1u from it along the edge (BSP_PixelwalkEnd)
#define BSPP_EDGE_RAMP      32    // top edge of a face leaning back, or a level row inside a displacement one:
                                  // BSP_PixelwalkCatch as well as what the other bits give
#define BSPP_EDGE_FLOATS    11

// Points about every `spacing` units along the horizontal edges of wall faces where a seam can be, for map-wide seam tests,
// from world brushes, the brushes of func_brush, func_wall and func_wall_toggle entities that are solid, displacements and props.
// "Flush" is measured with a small box, which meets a vphysics model where movement does:
// - brushes: bottom edges unless a floor meets them; top edges under a displacement, prop or entity,
//   or inside a world face that goes on lower (a world brush starting at the edge has its own bottom edge there);
// - vphysics brush entities collide 0.5u inside some faces: their edges are where movement meets them; a top edge moved down carries the seam
//   (bits TOP | CONTINUES when a face flush with it goes on above), a bottom edge moved up gets no CONTINUES;
// - displacements: top edges with a face flush above, unless a world brush's bottom edge is on the same plane there;
// - solid static props, prop_dynamic and prop_dynamic_override:
//   top edges of their collision mesh under a displacement, prop or entity (under a world brush a prop never hovers);
// - pixelwalks: LEDGE, RAMP and END points (see the bits).
// Built once per map: entities that move, toggle or go away later aren't followed.
// Per point BSPP_EDGE_FLOATS floats:
// x, y, z on the face, flat normal x, y, edge index (points of one edge are consecutive, its END points after all edges),
// BSPP_EDGE_* bits, then the ends (x, y, x, y) of the stretch of edge the point stands for: touching stretches make one line.
// Built on the first call per map and spacing (main thread). Copies up to maxOut points from `first`:
// `out` must hold maxOut * BSPP_EDGE_FLOATS floats (maxOut 0 returns only the total). Returns the total.
// Throws for spacing under 1 or a negative first or maxOut.
native int BSP_WallEdgePoints(float spacing, int first, float[] out, int maxOut);

// Overlay passes:
// the seam tests at every BSP_WallEdgePoints point for one set of movement values ({tick interval, gravity, airaccelerate, maxspeed}),
// as BSP_SeamCatch, BSP_HeadSeamCatch, BSP_TexturebugCatch, BSP_PixelwalkCatch and BSP_PixelwalkEnd give them by the point's bits,
// and a point that misses next to one that catches tested again up to 16u toward it where a side wall stands at the edge.
// A pass runs in BSP_OverlayStep calls, and on a worker thread while the server hibernates (BSP_OverlayHibernate).
// Finished passes are saved under data/bsppeek/overlay per map, BSP-Peek build, spacing and values.
#define BSPP_OVERLAY_PIXELSURF       1
#define BSPP_OVERLAY_CEILING         2
#define BSPP_OVERLAY_HEADSEAM        4
#define BSPP_OVERLAY_TEXTUREBUG      8
#define BSPP_OVERLAY_PIXELWALK       16
#define BSPP_OVERLAY_PIXELWALK_EXACT 32

// Starts the pass for these values, points nearest origin first (NULL_VECTOR: in point order).
// With useCache a saved pass is loaded, and a pass the worker began for these values goes on from there.
// Returns 1 when done (loaded), 0 started, -1 without points.
native int  BSP_OverlayStart(const float values[4], float spacing, const float[] origin, bool useCache);

// Runs the pass for up to seconds. Returns the points left, -1 without a pass.
native int  BSP_OverlayStep(float seconds);

// The pass's kinds per point (BSPP_OVERLAY_* bits, -1 untested), from `first`. Returns the total.
native int  BSP_OverlayKinds(int first, int[] kinds, int maxKinds);

// Values the worker tests too on every map, after the started pass, while the server hibernates; saved for a later start.
native void BSP_OverlayQueue(const float values[4], float spacing);

// Call from OnServerEnterHibernation (true) and OnServerExitHibernation (false):
// the worker runs only in between, and when 10 s pass with no frame
// (the server hibernating since before SourceMod loaded, which then fires no enter).
native void BSP_OverlayHibernate(bool hibernating);
