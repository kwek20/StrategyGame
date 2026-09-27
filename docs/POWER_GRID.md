# Power grid

This document defines the authoritative power-distribution rules. Rendering, interface code, and
audio may present these results but must not alter them.

## Design goals

- Power originates at generator entities and flows outward through explicit connections.
- Distribution rewards compact grids: devices closer to generation receive power first.
- A shortage degrades the outer part of a network before the inner part.
- Results are deterministic and independent of container iteration order, frame rate, rendering,
  and the order in which equivalent connections were created.
- The same commands and starting state must produce the same allocation and checksum on every
  peer.

## Authoritative units and timing

- Generation, demand, transfer, charging, discharging, and storage are simulation values evaluated
  on integer simulation ticks.
- Power is network capacity, not a player stockpile currency.
- Definitions may use friendly display units, but the hardened simulation will convert them to a
  fixed integer power unit at the definition boundary.
- Presentation may display W, kW, MW, or GW without changing authoritative values.

## Connections and flow direction

Connections describe physical links and are stored symmetrically so either endpoint can discover
the link. Power allocation over those links is directional.

For every allocation tick, each enabled and operational generator is a flow root. The simulation
walks outward from that generator through connected relays and devices. A particular allocation
may move power only away from its generator root; it may not reverse direction, circulate through
a loop, or use a consumer as a new source.

Storage that is discharging acts as a source for that tick and follows the same outward-flow rules.
A storage-capable generator may charge and discharge during the same tick. Discharge is controlled
by an authoritative per-device toggle exposed in the generator menu; disabling discharge does not
disable generation, charging, connections, or the rest of the device.

A storage-only device becomes a source root only while it has stored energy, discharge is enabled,
and its definition permits a positive discharge rate. Otherwise it remains an ordinary downstream
node: it can receive and forward generated power and may continue charging. Stored energy by itself
does not make a discharge-disabled device a root.

## Buffered one-hop flow

Every power device has a finite transit buffer. Generation and storage discharge enter the local
buffer. During one simulation tick, buffered power can cross at most one connection. Power received
in the current tick may satisfy that receiving device immediately, but it cannot be forwarded until
the next tick. A node's `transferLimit` is its total outgoing capacity per tick; it does not restrict
how much that node may receive.

For example, with a full generator `G`, `G.transferLimit = 2`, `N1.transferLimit = 1`, and the chain
`G - N1 - N2`:

```text
tick 1: G full, N1 buffer 2, N2 buffer 0
tick 2: G full, N1 buffer 3, N2 buffer 1
```

This propagation delay is authoritative, saved, and checksummed. It makes distance through the
actual connection graph meaningful instead of powering an entire connected component instantly.

When an output cannot serve every downstream branch, its authoritative ordering key is:

1. Priority: high, then medium, then low.
2. Shortest horizontal world-space distance to the supplying generator.
3. Lowest stable entity ID when both values are equal.

World-space distance is compared using deterministic squared distance; a square root is not
required. Entity ID is only the final tie-breaker and must not replace the distance comparison.

A downstream node receives as much as the sender's buffered energy, remaining output capacity, and
the receiver's remaining buffer/demand permit. This is intentional priority-based first-come-first-
served behavior rather than equal or proportional sharing.

Connection creation order, adjacency insertion order, hash-map order, rendering order, and frame
timing must never affect this ordering.

## Multiple generators

All enabled, operational generators participate in the deterministic multi-source forest. A node
may be reachable from more than one generator. Its supplying root is selected by:

1. Consumer hop count from the candidate generator.
2. Consumer world-space distance from the candidate generator.
3. Stable generator entity ID.
4. Stable consumer entity ID.

The selected generator's power propagates into that territory one connection per tick. Before flow,
the solver compares each territory's available buffered/generated/discharge power with its demand.
If a primary territory is deficient while another connected generator territory has real surplus,
the deficient root yields and the forest is rebuilt from the viable fallback roots. The yielded
generator remains a power-producing node; it simply no longer prevents surplus power from reaching
its consumers. Fallback uses the same hop, distance, and stable-ID ordering and is recalculated
deterministically without using connection creation time.

## Relays, loops, and paths

- Relays forward power but do not create it.
- A traversal never visits the same node twice for one generator wave.
- When multiple equal-hop paths exist, choose the path with the lowest total squared segment
  length, then compare the ordered sequence of stable entity IDs.
- Loops provide alternate routes and resilience but never permit circular flow.
- Destroying, disabling, or disconnecting a relay invalidates affected topology before the next
  allocation.

## Consumer priority

Every power device has one of three priorities: low, medium, or high. Priority controls which
downstream branch receives a constrained sender's output first. It does not bypass connections,
remove propagation delay, increase transfer limits, or pull power backward toward a generator.

A branch's effective priority is the highest priority of any device downstream in that branch.
This value propagates toward the root before allocation, so a high-priority factory behind a
low-priority relay still makes that relay branch high priority. A relay's own local demand is
satisfied locally before its remaining buffer is forwarded.

## Generation and storage order

Each tick uses this sequence:

1. Validate or rebuild dirty topology.
2. Reset transient flow and supplied-power values.
3. Add current generation and eligible storage discharge to local transit buffers.
4. Consume local demand, then send remaining buffered power one connection outward.
5. Classify consumers as powered, underpowered, or offline.
6. Route unused generator output into eligible storage.
7. Emit state-change and failure events.

Every participating device records rebuildable last-tick diagnostics for starting buffer,
accepted generation, storage discharge, consumption, outward and inward transfer, storage charge,
curtailment, and ending buffer. Generation that cannot enter the transit buffer is recorded as
curtailed rather than silently disappearing.

Storage rules:

- Consumers are supplied before storage is charged.
- Generation is used before stored energy is discharged.
- Storage-capable generators may charge and discharge in the same tick while discharge is enabled.
- Disabling discharge prevents stored energy from entering the transit buffer but leaves charging
  enabled.
- Charge rate, discharge rate, storage capacity, and network transfer capacity are all enforced.
- Stored power is clamped to its valid range and power must never be created by rounding.

## Operational states

- **Powered:** supplied power meets demand.
- **Underpowered:** supplied power is greater than zero but below demand.
- **Offline:** no power is supplied, the device is disabled, or it has no valid route to a source.
- **Not applicable:** the entity is not a power device.

Individual gameplay systems decide how an underpowered ratio affects processing, charging,
sensors, production, or weapons. Those policies must use the authoritative supplied and demanded
values; they may not calculate a separate grid result.

## Connection validity

A power connection is valid only when both endpoints:

- Exist and have power components.
- Have the same owner.
- Are operational and permitted to connect.
- Are within connection range.
- Have available connection slots.

Invalid commands produce an explicit rejection reason. Destruction removes reciprocal links
immediately. Connections are not silently recreated after rebuilding unless a later design adds an
explicit automatic-reconnection rule.

Connection validation and mutation are owned by `PowerGridSystem`, not by `GameSession`. Zero
connection slots, disabled endpoints, duplicate links, enemy ownership, non-operational endpoints,
range failures, and missing links all return distinct authoritative outcomes. `World` destruction
removes reciprocal links at the mutation boundary, while gameplay destruction also emits one
connection-removed event per severed link. Disabling a device preserves its physical links but
removes it from active topology until it is enabled again; destroying and rebuilding an entity does
not recreate its old links.

## Deterministic invariants

The simulation must maintain:

```text
0 <= supplied <= demand
0 <= stored <= storage capacity
device flow <= device transfer capacity
0 <= transit buffer <= transit capacity
consumer use + storage increase <= generation + storage decrease
```

The stronger per-device conservation identity is:

```text
starting buffer + requested generation + discharged storage + received transfer
= consumed + charged storage + sent transfer + ending buffer + curtailed generation
```

Reordering connections, changing rendering state, or saving and loading the same authoritative
state must not change allocation results.

## Implementation note

The authoritative solver is isolated in `PowerGridSystem`. It owns sorted per-player topology
snapshots containing nodes, edges, connected components, stable grid IDs, and monotonic revisions.
Connect, disconnect, enable/disable, construction completion, destruction, entity creation, and
world replacement explicitly invalidate affected snapshots; clean grids reuse their snapshot
without scanning or hashing the complete world topology.

The allocator uses generator-rooted outward forests, finite device transit buffers, one-hop-per-tick
delivery, and low/medium/high branch priority. Equal-priority branches use generator distance and
stable entity ID as deterministic tie-breakers.

## Diagnostics

F7 toggles the dedicated power-debug view. It is mutually exclusive with the F4 terrain and F6
water diagnostics. The view shows local-player grid connections and device state without changing
simulation results:

- Slightly filled translucent green world-space discs show every local device's definition-backed
  connection range, including relay poles. Their terrain-following outlines keep overlapping reach
  legible without hiding the world.
- Cyan directional links carried power during the latest simulation tick.
- Green links are connected and ready, orange links are underpowered, and red links are broken.
- Node crosses show effective downstream branch priority in magenta, yellow, and grey.
- The panel reports aggregate generation, demand, supply, transit buffers, storage, and grid count.
- The panel reports latest-tick generation, discharge, consumption, charge, curtailment, and
  sent/received totals.
- Cursor inspection reports the nearest device's grid root, parent, priority, buffer, output limit,
  and previous-tick sent/received amounts. It shows both configured and effective branch priority,
  plus primary and active roots when generator fallback occurs.

The root, parent, and flow counters are rebuildable presentation diagnostics. They are deliberately
excluded from saves and authoritative state checksums.

The power-grid test target also acts as a repeatable headless stress profile. It simulates two
identical 257-device, 32-branch grids for 600 ticks, compares stable allocation fingerprints after
every tick, checks per-device conservation, and splits then reconnects an active branch. The printed
elapsed time is diagnostic; only a deliberately broad 30-second regression ceiling is enforced so
ordinary machine and CI variance cannot masquerade as a gameplay failure.

Placing any building with a power device does not display the full debug range layer. Instead, the
preview draws one green terrain-following line to the nearest enabled, operational local endpoint
that has a free connection slot and overlaps both devices' connection ranges. No line is drawn when
there is no valid endpoint. This is placement guidance only and does not silently create a
connection or change authoritative placement validity.
