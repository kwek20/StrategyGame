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
Storage that is charging acts as a consumer and cannot also discharge during that tick.

## First-come-first-served allocation

Consumers are served outward from generators. For a given generator, the authoritative ordering
key is:

1. Fewest connection nodes (graph hops) from the generator.
2. Shortest horizontal world-space distance to that generator when hop counts are equal.
3. Lowest stable consumer entity ID when both values are equal.

World-space distance is compared using deterministic squared distance; a square root is not
required. Entity ID is only the final tie-breaker and must not replace the distance comparison.

A consumer receives as much of its remaining demand as the generator and traversed connections
can provide. A closer consumer may therefore be fully supplied while a more distant consumer is
underpowered or offline. This is intentional first-come-first-served behavior rather than equal or
proportional sharing.

Connection creation order, adjacency insertion order, hash-map order, rendering order, and frame
timing must never affect this ordering.

## Multiple generators

All enabled, operational generators participate in one deterministic allocation pass. A consumer
may be reachable from more than one generator. Candidate deliveries are ordered by:

1. Consumer hop count from the candidate generator.
2. Consumer world-space distance from the candidate generator.
3. Stable generator entity ID.
4. Stable consumer entity ID.

The first candidate delivers as much as its route permits. Later generators may supply any demand
that remains, but power already assigned in the tick is not displaced. This makes overlapping
generator coverage deterministic without treating connection creation time as authority.

Each physical edge and transfer-limited device tracks its remaining capacity across the complete
allocation pass. Two generators cannot each spend the same transfer capacity.

## Relays, loops, and paths

- Relays forward power but do not create it.
- A traversal never visits the same node twice for one generator wave.
- When multiple equal-hop paths exist, choose the path with the lowest total squared segment
  length, then compare the ordered sequence of stable entity IDs.
- Loops provide alternate routes and resilience but never permit circular flow.
- Destroying, disabling, or disconnecting a relay invalidates affected topology before the next
  allocation.

## Consumer priority

Consumer priority does not override generator-outward first-come-first-served allocation. The
existing runtime priority value and priority button predate this contract and must not remain as a
second, conflicting allocation rule.

During implementation they must either be removed or explicitly repurposed for a behavior that
does not reorder power delivery. Until then, the current priority-based solver is legacy behavior
and not the design target.

## Generation and storage order

Each tick uses this sequence:

1. Validate or rebuild dirty topology.
2. Reset transient flow and supplied-power values.
3. Allocate current generator output to consumers.
4. Discharge eligible storage to remaining consumer demand, using the same outward ordering.
5. Classify consumers as powered, underpowered, or offline.
6. Route unused generator output into eligible storage.
7. Emit state-change and failure events.

Storage rules:

- Consumers are supplied before storage is charged.
- Generation is used before stored energy is discharged.
- Storage cannot charge and discharge in the same tick.
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

## Deterministic invariants

The simulation must maintain:

```text
0 <= supplied <= demand
0 <= stored <= storage capacity
edge flow <= edge transfer capacity
device flow <= device transfer capacity
consumer use + storage increase <= generation + storage decrease
```

Reordering connections, changing rendering state, or saving and loading the same authoritative
state must not change allocation results.

## Implementation note

The current implementation already supports connected components, priorities, constrained routes,
storage, grid IDs, commands, persistence, and checksums. It currently allocates primarily by
consumer priority and entity ID. It must be changed during power-grid hardening to implement the
generator-rooted hop/distance ordering specified here, after which priority must no longer affect
allocation.
