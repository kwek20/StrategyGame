# Group arrivals and shared rally movement

Move orders and unit-producer rallies use `findUnitArrivalPath` in `simulation/UnitMovement`.
The requested point is preferred. If occupied or reserved by a friendly unit's move order,
the planner searches nearby rings for a reachable, non-overlapping arrival position.
Ground spacing uses both unit radii plus a passing lane so early arrivals cannot seal off
the remaining slots; flying units use modest visual spacing.
Flight and ground arrivals are independent. Units already settled in the area occupy space.

Reservations derive from current authoritative move destinations. Stop, replacement orders,
destruction and arrival naturally release them; there is no second persistent reservation table.
Directly controlled units reserve their current location, not an old strategic destination.
The search is deterministic in command order, bounded to a nearby area, and limits failed
route queries. A failed search follows existing order handling; no new rejection UI is added.
This is loose group arrival spacing, not rigid formations or synchronized group travel.

RTS tanks turn while driving through moderate heading changes, using bounded clear-segment
look-ahead at waypoint transitions. Slowing near corners/destinations prevents orbiting a
waypoint. Terrain, static obstacles and current unit footprints constrain shortcuts. Obstructed arcs fall back to
pivoting, and sharp reversals still pivot before driving. Physical collision still applies.
Nearby traffic receives local passing steering. A blocked unit can also request a temporary
route around current ground-unit footprints, at most once per retry interval. These traffic
occupancy masks and flow fields are query-local; they never invalidate the permanent static
occupancy cache or leave stale unit blockers behind. Temporary congestion retains the order.
Base unit speeds, direct-control driving, slope rules and visibility rules are unchanged.

Producers deploy from the existing rally-facing boundary point, expanding outward if crowded.
Each unit receives a separate arrival position and starts facing its route (outward when no
rally exists). Deployment uses the same circular terrain clearance as actual movement.
A completed paid order still waits and retries if no legal deployment position exists; units
are not discarded or charged again. Changing a rally affects subsequent output only.

No command format or save-schema change is needed. Destinations already belong to unit state;
route look-ahead and planning caches remain transient. Focused coverage lives in
`UnitMovementTests`, `TankCombatTests` and `TankProductionTests`.
