# Tank factory prototype

Implemented scope: factory replacement, first unit definition, reliable deployment, rally points,
ground vehicle movement and discrete cannon combat (steps 1–6).

The Tank Factory replaces the unfinished Drone Factory in the construction palette. Construction
drones remain available from the command hub. Factory construction still costs 175 Alloy, uses
the existing 14×11 footprint and requires 140 construction power. Its operating demand remains
12 power. Existing powered production, queue cancellation and population limits apply.

| Light tank property | Initial value |
| --- | --- |
| Production cost | 125 Alloy + 25 Fuel |
| Production time | 20 seconds before country/production modifiers |
| Health | 350 |
| Ground speed | 4 units/second |
| Base vision radius | 18 units |
| Collision radius | 2.2 units |
| Hull turn rate | 90 degrees/second |
| Weapon definition | Light cannon; 32 damage, range 16, 60-tick cooldown; ground targets |

These are prototype values. There is no movement fuel drain or drone battery component.

## Deployment

With a rally point, deployment first tries the nearest point on the building boundary toward that
rally, offset outward by the unit radius and normal spawn clearance. Rotated footprints and flying
output use the same rule. If blocked, concentric-ring searches prefer the rally-facing direction
and alternate to either side, continuing across the playable map. Without a rally, the existing
default ring order applies. Crowded exits do not impose a fixed deployment-distance cap.
Every candidate checks map margins, the unit's ground footprint, water/cliffs and occupied space.
No particular factory side or doorway is required. The search does not require a path through the
occupied perimeter to the chosen deployment position.

A paid production order is removed only after its units have actually spawned. If the entire map
has no sampled legal position, it remains complete and retries twice per second. This exceptional
case never deletes the unit or charges the player again. Destroying the producer cancels its
pending deployment, including destruction during the completion tick.

## Rally points

Select the factory and use the configured contextual-order binding (default right-click) on terrain
or the minimap. A selected owned producer shows a rally flag and a Clear rally point action.
New tanks receive the current rally as a normal navigation order after deployment. Moving or
clearing the flag does not replace existing unit orders. Inaccessible destinations use normal
navigation handling; the flag is not a promise that terrain can be crossed.

The shared producer predicate enables these controls on command hubs and every town-center tier.
Their construction drones deploy through the same search using air traversal, then fly to the
rally. Possession eligibility is derived from unit definitions: all owned non-drone units can be
controlled, independent of where they were created or whether the match was loaded. The redundant
per-instance possession flag and starting-roster override have been removed.

Power Toggle connection mode remains a separate left-click interaction and suppresses rally
orders until completed or cancelled. Production shortcuts retain their original slots when the
Clear rally point action appears. The same mechanism supports other unit-producing buildings.

Rally state participates in authoritative checksums and saves. Format 1 now requires the nullable
`rallyPoint` field in production components; older saves without it are rejected under the existing
in-place development schema policy. The command wire version is 11; the new command is appended.

## Ground movement and combat

Strategy orders and collision use unit capabilities independently of direct-control eligibility.
Navigation checks the tank's full radius. The hull pivots before driving into a new path segment;
it cannot strafe along obstacle edges or inherit a pedestrian sprint boost. Empty production
buildings remain navigation and collision obstacles even when they hold no resources.

The cannon deals 32 damage per shot, with a two-second cooldown that continues while idle.
Stopping or reissuing an attack does not reset it. Attacks require an enemy ground target in live
vision, weapon range, barrel alignment and a clear firing line. Terrain and operational buildings
block shots using terrain samples and authored building volumes; units and removable vegetation
do not provide ballistic cover for strategy orders. The turret turns independently at 150 degrees
per second, with barrel elevation from -10 to +30 degrees. Hull turning remains 90 degrees/second.

When cover blocks a shot, the tank searches reachable firing positions around the target. That
route may briefly leave weapon range. Losing vision ends entity tracking and sends the tank toward
the last observed position; explored territory alone does not authorize shots at hidden enemies.
Damage is immediate, with a brief depth-tested tracer and muzzle flash for visible shots.
Accuracy is deterministic from the map seed, attacker ID and saved shot counter.

Save format 1 requires `shotsFired` and nullable `lastSeenTarget` alongside the existing cooldown.
They participate in authoritative checksums. Older saves missing these fields are rejected under
the existing development schema policy.

## Presentation and remaining work

Original prototype factory and tracked-tank geometry, with matching icons, is generated by
`tools/build_tank_assets.py`. Tank Hull, Tracks and Turret are separate named model nodes. Both
models preload with the match. Generated icon sources are preserved by the atlas rebuild tool.

The model has independently posed Turret and Barrel nodes, including recoil and terrain-relative
aim compensation. Visible shots produce a cannon flash, tracer, impact sparks and original cannon
audio. Destruction produces a fireball and smoke, including at low particle quality. These effects
are disposable presentation state; fog clips tracers and independently gates muzzle/impact effects.
The shared SDL audio mixer plays overlapping PCM cues with master/effects/mute controls. Empty
legacy cue assets remain silent. `tools/build_combat_audio.py` regenerates the three original WAVs.

In direct tank control, the mouse steers the turret, Left Mouse fires (hold to repeat at the normal
cooldown), and Right Mouse optically zooms around the same crosshair ray while held. The hull uses
movement input independently. Shots require barrel alignment; amber indicates aiming or reloading.
Manual shots trace from the actual cannon toward the crosshair, stopping at terrain, buildings or
the first eligible visible enemy ground unit. Friendly buildings block without taking friendly-fire
damage. Right Mouse never fires. Both bindings are configurable in Settings. Quick clicks survive
frame/fixed-tick boundaries, and losing focus or leaving possession clears weapon input.

Wire version 12 appended `AimWeaponCommand`; version 13 appended the top-bar `GrantResourcesCommand`; version 14 appends `DirectVehicleInputCommand` for independent throttle, steering and brake. Format 1 additionally requires turret yaw/pitch,
nullable direct aim and trigger state in combat components; checksums include these values.
Attack-move, hold-position, more unit content and wreck gameplay remain subsequent work.

## Focused verification

The tank-production check covers price, queue cancellation/refund, power loss and proportional
production, deployment beyond occupied rings, map edges, water avoidance, automatic rally movement,
changed/cleared rallies, wire validation, checksums and save/load. An isolated
renderer check covers model loading, presentation and tank picking. The rally-controls check
covers world/minimap clicks, clearing and precedence of power editing. The opening gameplay test is
intentionally omitted.

The focused movement/combat check covers bounded turning, tank clearance, target restrictions,
terrain/building cover, firing-position navigation, discrete cooldown timing, lost sight,
deterministic accuracy and saved/checksummed shot state. An isolated visual check verifies tracer
appearance, expiry and OpenGL errors.

Direct-control checks exercise held fire/cooldown, quick clicks, zoom and focus loss. A dummy-device
audio check verifies that combat cues produce nonzero mixed PCM rather than only recording events.
