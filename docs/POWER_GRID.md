# Power grid

This is the authoritative power-distribution contract. Rendering and UI present the simulation
result; they never calculate or mutate allocation.

## Whole-grid calculation each tick

The simulation evaluates each connected grid on every simulation tick (30 Hz). Power can cross
multiple connections in that tick. There are no transit buffers and no per-hop propagation delay.
Only explicit storage retains energy between ticks. Unused generation is curtailed.

Generation, demand, transfer limits, and storage charge/discharge rates are per-tick values.
Storage capacity limits retained energy. Values must be finite and nonnegative; zero means zero,
never unlimited. The implementation currently uses floating-point values and deterministic
ordering; fixed-point arithmetic remains future determinism hardening.

## Connections and limits

Physical connections are symmetric, explicit, and owner-specific. Allocation chooses directed,
acyclic routes from sources over those connections.

- Generators never accept incoming power, including when exhausted or when output is disabled.
  Producer/generator definitions remain source-only even when their current generation is zero.
- A device's transferLimit caps its total outgoing power across all connections and allocation
  passes in that tick. It does not cap its own intake or local use.
- A zero transferLimit prevents forwarding.
- Intermediate buildings may consume power and forward the remainder.
- Connections require operational, enabled endpoints of the same owner, overlapping connection
  ranges, and free slots. Invalid commands return explicit failure reasons.
- Disabling a device removes it from active topology but retains physical links. Destruction
  immediately removes reciprocal links; rebuilding does not recreate them.

## Priorities and splitting output

Allocate consumer demand in this order: **high, medium, low**. Priority changes allocation only
when available generation, storage discharge, or transfer capacity cannot meet all demand.

Within a priority pass, each sender splits available outgoing power equally among its requesting
downstream connections. A branch that needs less than its share returns the unused share for
redistribution among the remaining requesting branches. Satisfied or non-requesting connections
take no share. This applies to generators and forwarding buildings.

Upstream consumption must be satisfied before forwarding to a downstream consumer, even when
the downstream consumer has higher priority. A high-priority request therefore also requests the
power needed by its upstream consumers.

Example: generator produces 6, upstream low-priority building needs 4, downstream high-priority
building needs 5. The upstream building receives 4 and the downstream building receives 2 in the
same tick. Disable the upstream building's consumption and the downstream building receives 5.
A separate medium-priority branch receives power only after the reachable high-priority request
and its upstream prerequisites have been served.

For two equal-priority branches requiring 5 each, a generator producing 6 supplies 3 to each.
If one branch needs only 1, it receives 1 and the other receives 5.

## Independent controls

All controls are authoritative, serializable commands and are included in save state/checksums.

- **Device enabled:** participates in the active network.
- **Consumption enabled:** allows local powered work and demand. Turning it off pauses powered
  work but preserves intake, forwarding, generation, and eligible storage charging.
- **Output enabled:** allows forwarding to other devices. Turning it off preserves incoming
  power and local use/charging. It does not disable local generation or storage use.
- **Discharge enabled:** permits use of stored energy. Turning it off preserves generation,
  charging, intake, and forwarding.
- **Priority:** low, medium, or high.

Generators retain their no-intake rule regardless of these controls.

## Generation and storage

Each tick:

1. Rebuild dirty topology and reset supplied-power and flow diagnostics.
2. Establish finite generation, discharge, demand, and output budgets for every device.
3. Serve high-priority demand, then medium, then low, including upstream prerequisites.
   Within each priority tier use generation first, then eligible stored energy.
4. Only when every enabled consumer demand in that connected component is satisfied, route
   remaining generation into storage subject to available routes, output capacity, charge rate,
   and storage capacity.
5. Curtail unused generation, classify device states, and emit state-change events.

A consumer blocked behind an output-disabled device or a transfer bottleneck still prevents
storage charging in its connected component. A disconnected component is evaluated separately.
Consumption-disabled demand does not block charging.

Discharge is limited to actual consumer need, stored energy, and the definition's discharge rate.
Stored energy is never discharged merely to charge another store. A store that discharged this
tick cannot recharge in the same tick. A generator with integrated storage charges from its own
surplus only, because generators do not receive power. A storage-only device can receive and charge
from the grid; it becomes a source during discharge passes when usable discharge remains.

## Routes, multiple sources, and determinism

The solver constructs an outward forest using currently available sources and residual output
capacity. Choose a source by shortest connection-hop count, then squared horizontal
target-to-source distance, then stable source ID. For equal-hop paths from that same source,
choose the lowest sum of squared horizontal segment lengths, then compare the complete
root-to-device entity-ID sequence lexicographically.

After a source or forwarding capacity is exhausted, rebuild routes so another reachable source
can supply remaining demand in the same tick. A consumer may therefore receive from several
sources. Generators never become intermediate receiving nodes to enable fallback.

Each allocation path is acyclic. Physical loops provide alternate routes. Different passes may
use different directions over a link; diagnostics display net flow. Sources, receivers, and
outgoing transfers retain shared per-tick budgets across all passes, so rerouting cannot create
energy or reset a transfer limit.

Topology snapshots are sorted, revisioned, and reused while clean. Connection changes,
enable/disable, construction completion, destruction, entity creation, and world replacement
invalidate affected snapshots. Allocation still runs each tick on the whole grid.

## Operational states and accounting

- **Powered:** enabled consumption meets positive demand; for zero-demand devices, a source was
  reachable through usable output paths at the start of allocation.
- **Underpowered:** positive demand receives some but not all required power.
- **Offline:** no supply, no reachable source for a passive device, consumption disabled, or
  device disabled/non-operational. A consumption-disabled device may still forward energy.
- **Not applicable:** no power component.

Gameplay systems use these authoritative values. Processors require full power to accept cargo
and convert it; other systems retain their own documented degradation policies.

Per-device invariants (within numeric tolerance):

```text
0 <= supplied <= enabled local demand
0 <= stored <= storage capacity
0 <= sent <= enabled output transferLimit
accepted generation + discharged storage + received = consumed + charged storage + sent
requested generation = accepted generation + curtailed generation
```

Transit buffers/capacities are absent from definitions, entity state, saves, and checksums.
Save format remains 1 under the project's in-place schema policy. Old saves missing the new
consumptionEnabled/outputEnabled fields are rejected; no migration is provided.
Root/parent, branch priority, and per-edge flow counters are transient diagnostics, excluded from
saves/checksums and rebuilt on simulation ticks.

## Presentation and validation

The power menu exposes priority, device enable, consumption, output, and storage-discharge controls.
The HUD power overview and F7 diagnostics show supply/demand, storage, output limits, and USE/OUT
switch states. F7 shows generation, discharge, consumption, storage charge, curtailment, transfer
totals, and directional net flow. No power-buffer values are displayed.
Root/parent identify the most recently used allocation route; primary root records the initial
route, and fallback indicates use of another source.

F7 is mutually exclusive with F4 terrain and F6 water diagnostics. Definition-backed green
terrain-following range discs include relay poles. Placement shows one proposed link to the
nearest valid endpoint; it does not automatically connect anything.

Focused tests cover immediate multi-hop delivery, transfer limits including zero, equal sharing,
upstream consumption, independent switches, priority, storage ordering/rates/capacity, source-only
generators, isolated relays, fallback, conservation, and deterministic routing.
The stress test compares two 257-device grids over 600 ticks, including split/reconnect mutations,
with per-tick fingerprints and conservation checks and a broad 30-second Debug regression ceiling.
