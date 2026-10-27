# Direct vehicle controls

Ground units with a positive movement turn rate use vehicle controls while possessed.
Characters retain camera-relative movement, and construction drones remain indirect-only.
Aircraft bindings remain reserved; this change does not add a flight controller.

| Default input | Behaviour |
| --- | --- |
| W | Accelerate forward along the hull |
| S | Brake forward motion, then reverse along the hull |
| A / D | Turn the hull's nose left / right; pivot at rest |
| W or S with A / D | Drive through a curve |
| Space held | Brake both tracks; overrides throttle and steering |
| Mouse | Aim the turret independently of driving |
| LMB | Fire; hold to repeat at the weapon's cooldown |
| RMB held | Zoom around the firing location |

Reverse does not rotate the hull toward the camera. Steering has the same nose rotation
in forward and reverse. Opposing throttle brakes to zero before changing direction.
Releasing throttle decelerates quickly. W+S and A+D cancel their respective input axes.
All driving, firing and zoom bindings are editable under Settings > Keybinds > Vehicles.

## Light tank tuning

Values live in `assets/gameplay/units.json`, under `movement`:

- Forward top speed: 4 world units/second, before terrain and upgrade modifiers.
- Reverse speed: 60% of resolved forward speed.
- Acceleration: 8 world units/second squared (0.5 seconds to forward top speed).
- Braking: 16 world units/second squared (0.25 seconds from forward top speed).
- Released-throttle deceleration: 10 world units/second squared (0.4 seconds to stop).
- Hull turning: 90 degrees/second, including stationary pivoting.

Space prevents pivoting while braking, allowing a stable firing stop. Normal slope and
difficult-ground speed modifiers still apply, as do water, cliffs, object collision and map
boundaries. Collision clears drive speed rather than storing momentum against an obstacle.
Direct control does not add automatic separation steering that could redirect the player's hull.

The possessed-unit HUD displays actual signed-drive speed as a positive magnitude in world
units/second, plus FORWARD, REVERSE, STOPPED or BRAKING. Its hints use the configured bindings.

## Simulation contract

`DirectVehicleInputCommand` carries independent throttle and steering axes in [-1, 1], plus a
brake flag. Wire version 14 appends this command at variant index 30. Character input remains
`DirectUnitInputCommand`; vehicles reject that direction-vector command.

`VehicleControl` integrates signed speed and hull rotation at the fixed simulation tick.
RTS movement curves through moderate heading changes, slowing while turning and approaching
the destination. Sharp reversals over 75 degrees still pivot. Bounded route look-ahead skips
unnecessary grid corners only when terrain and static footprint clearance permit it. If a
curved step is obstructed, the hull pivots until aligned before retrying the same route.
Releasing possession clears drive speed and inputs before RTS control resumes.

Save format remains 1. Unit components now require `driveSpeed` and `vehicleBrake`; old development
saves missing these fields are rejected under the project's in-place schema policy. Both fields
participate in the authoritative checksum. Save snapshots round-trip in-progress braking/reverse
state. Entering a saved match locally releases possession and clears held driving input, as it
already does for weapon input.
