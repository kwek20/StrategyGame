# Ground traversal

V2 ground movement uses physical terrain clearance rather than an exclusion buffer
around every terrace boundary. Pathfinding and simulation movement call the same
`Terrain::permitsMovementSegment` predicate.

- The footprint is a circle, sampled at its centre and eight points on its rim.
- Continuous ground is walkable through 40 degrees. Speed decreases smoothly from
  full speed at 25 degrees to 65% at 40 degrees.
- Authored cliff steps of at most 0.4 world units are traversable in either direction.
  Larger cliff boundaries remain barriers, including across diagonal junctions.
- V2 highland biome appearance and legacy mountain classification do not independently
  block dry land. Physical slope, cliffs, water and universal barriers determine access.
- Existing difficult-ground slowdown remains; it does not multiply the slope penalty.
- Building/resource collision, movement domains and the active map boundary still apply.

Navigation caches terrain edge results by movement profile and radius. Terrain grading
invalidates those results, occupancy and routes because it can change slope eligibility.
Entity obstacle changes invalidate occupancy/routes but preserve terrain edge results.
Queries take the current terrain explicitly. Navigation does not retain a terrain pointer,
so moving a prepared session into PlayState cannot leave it referencing its previous owner.

These rules apply to V2 ground units, including the worker and tank. They do not change
tank chassis steering limits or flying drone traversal. V1 retains its existing traversal.
