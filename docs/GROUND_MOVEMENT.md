# Ground traversal

V2 ground movement uses physical terrain clearance rather than an exclusion buffer
around every terrace boundary. Pathfinding and simulation movement call the same
`Terrain::permitsMovementSegment` predicate.

- The footprint is a circle, sampled at its centre and eight points on its rim.
- Continuous dry hills never block movement because of their slope angle. The initial
  mild tuning keeps full speed through 25 degrees and decreases linearly to 75% at
  50 degrees, retaining that positive floor on steeper ground.
- Authored cliff steps of at most 0.4 world units are traversable in either direction.
  Larger cliff boundaries remain barriers, including across diagonal junctions.
- V2 highland biome appearance and legacy mountain classification do not independently
  block dry land. Cliffs, water and universal barriers determine terrain access.
- Surface costs also affect speed, capped at a mild 85% minimum; surface and slope
  penalties combine by taking the slower value, without multiplying them.
- Building/resource collision, movement domains and the active map boundary still apply.

Navigation caches terrain edge results by movement profile and radius. Terrain grading
invalidates those results, occupancy and routes because it changes clearance and travel time.
Entity obstacle changes invalidate occupancy/routes but preserve terrain edge results.
Queries take the current terrain explicitly. Navigation does not retain a terrain pointer,
so moving a prepared session into PlayState cannot leave it referencing its previous owner.

These rules apply to V2 ground units, including the worker and tank. They do not change
tank chassis steering limits or flying drone traversal. V1 retains its existing traversal.

V2 route costs use the reciprocal of `Terrain::movementSpeedScaleAt`, the same terrain
speed factor used by foot and tank simulation. Flow-field construction and path extraction
both account for edge travel costs. The old additional height-change penalty is retained
only in V1. Grading refreshes cached speed costs as well as heights and clearance.

`assets/gameplay/terrain/v2.json` exposes `groundMovement` settings for slowdown start/full
angles, minimum slope speed, minimum surface speed and minimum route width. The speed floor must be positive.
The focused traversal check compares the mild curve (90% at 35 degrees, 75% at 50) with
a stronger alternative (84% at 35 degrees, 60% at 50). The mild curve is the initial default,
not a claim that gameplay tuning is final.

Continuous source contours in ordinary/hill cells resolve as ramps, including diagonals
and multi-level changes. Peak/basin boundaries and abrupt source discontinuities retain
canonical cliffs. Rendering and movement use the same edge ownership; no separate invisible
slope wall is added. Constructed connecting corridors target at least 7.5 world units of
width after cell rounding, giving the current tank footprint room for grid alignment.
Water and map bounds still constrain carving; final movement checks decide actual clearance.
Generation revision is 9; save format remains 1. Earlier V2 revisions
are rejected rather than reconstructed with different terrain. Building tilt/grading rules
are separate and unchanged.

Possessed ground vehicles use hull-relative throttle, reverse and steering, with acceleration
and braking. They share these terrain/collision rules with RTS movement but do not use its
route look-ahead and arrival steering. See [VEHICLE_CONTROLS.md](VEHICLE_CONTROLS.md).
