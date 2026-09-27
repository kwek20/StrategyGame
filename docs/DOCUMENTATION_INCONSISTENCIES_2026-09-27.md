# Source/documentation audit — 2026-09-27

Status update: F1–F9 were subsequently resolved as described below.
Historical evidence describes the original audit snapshot; C1 is also resolved below.

Post-fix validation: the full Debug build and all 18 CTest tests passed on 2026-09-27 after the
whole-grid power redesign (255.51 seconds total). Power tests cover same-tick delivery, priorities,
upstream consumption, independent controls, finite/zero limits, storage ordering, source-only
generators, isolated relays, conservation, and deterministic multi-source routes. Save/load,
command/checksum, HUD layout, renderer integration, and drone regression tests also passed.

## Scope and method

Repository: `C:\Users\Brord\Desktop\Work\self\new`.
Base commit: `710a85d3d8cf73b1e6b40afdd0208f0b58d0b267` on `develop`.
The audit includes the existing staged power-grid, rendering, persistence, and drone-logistics changes, not just HEAD. Existing changes were preserved.

This is a focused audit of power distribution, drone work/recovery, resource delivery, persistence boundaries, definition-backed balance, and README feature claims. It is not an exhaustive audit of rendering, terrain generation, combat, or every roadmap item.

I read the installed TypeSafe skill and its live API, Choice, and citation-checking documentation. Jev was actually called through `POST https://api.typesafe.ai/v1/systemone`; `jev-latest` resolved to **jev-1.13.0**. It classified documentation/source evidence pairs, first in a shared batch and then in individual, narrower checks. Final findings are based on code inspection and executable evidence, not model confidence alone. The second checks included the proposed discrepancy and, where available, probe results, so they are verification prompts rather than blind independent discovery.

Nine findings are recorded below, plus one timing clarification. Five behaviors were reproduced by a standalone C++ probe linked against freshly built Debug simulation libraries. No game implementation was changed. The complete CTest suite and manual visual tests were not run for this audit.

Priorities: **P1** = address before claiming reliable economy/save continuation; **P2** = contract or implementation issue to resolve; **P3** = stale user documentation. File references and line numbers refer to the audited working tree.

## F1 — Resource-field data changes gameplay despite being described as diagnostics-only — RESOLVED

**Resolved on 2026-09-27.** Crossing resource-field boundaries after depletion is explicitly allowed.
`nextResourceNode()` now always selects the nearest remaining node of the same raw resource type
within twice the depleted archetype's field-definition maximum radius, using horizontal distance
and stable-ID ties. It no longer reads transient field geometry, so loading cannot change this
choice for identical entity state and definitions. No field definition or no eligible nearby node
leaves the existing delivery/idle behavior in place. No save-format change is needed.

The resource contract and recovery notes now document that policy. Regression coverage in
`tests/ResourceDeliveryTests.cpp` checks a nearer cross-field candidate with and without diagnostic
geometry through `replaceWorld()`, stable-ID ties, resource-type filtering, and the distance bound.
Validation: `strategy_game` builds; `resource_delivery_routing` and `drone_reliability` both pass
(62.43 s and 23.57 s respectively). The full suite was not rerun for this focused fix.
The original audit evidence below is retained as historical context; its line numbers refer to the
pre-fix source.

**Original finding: P1 — reproduced behavior; persistence/authority contract mismatch.**

- Documentation: `docs/agents.md:221–223` calls `ResourceLayout` transient diagnostics, not saved or checksummed. `docs/ROADMAP.md:243–245` requires deterministic resource results and drone state surviving save/load throughout its work cycle. `docs/Resource system.md:53–54` promises retargeting within the same generated field.
- Source: `src/simulation/GameSession.cpp:175–223` reads `resourceLayout_.fields` inside `nextResourceNode()` and uses containment to choose a new authoritative target. With no containing field, it instead searches within twice the definition's maximum field radius. `GameSession.cpp:1704` clears the layout during `replaceWorld()`. Save loading calls that path at `src/game/PlayState.cpp:161` and `:405`. `src/simulation/StateChecksum.cpp:66` includes the resulting gather targets, but the layout itself is absent from the checksum.
- Reproduction: use a Scrap field centered at `(0,0)` with radius 10, a depleted node at `(9,0)`, a remaining in-field node at `(0,0)`, and another node at `(15,0)`. Start a drone gathering the depleted node. With geometry present it selects the in-field node, ID 3. Restore identical entity data through `replaceWorld()`, which clears geometry: it selects the closer outside node, ID 4.
- Observed output: `field_retarget: with_geometry=3 without_geometry=4 same_field=3 other_field=4`.
- Impact: data omitted as supposedly diagnostic influences authoritative orders. Loading can change the next node selected. Overlapping footprints also identify geometric containment rather than exact originating field membership.
- Qualification: `docs/agents.md:69–74` explicitly documents the fallback, so this is not an undisclosed fallback implementation. That caveat conflicts with the diagnostics-only description and the broader continuation guarantee. The probe exercised the exact world-replacement boundary used by loading; it did not perform a complete disk save/load round trip.
- Suggested resolution: persist/checksum stable field-instance membership, or rebuild equivalent membership before simulation resumes. Preserve save format 1 as required by the project policy. Add a paired continuation test from before depletion, with one branch saved and loaded.

## F2 — A positive battery remainder can permanently stall repair/construction — RESOLVED

**Resolved on 2026-09-27.** At a construction or repair target, a drone that cannot afford the next
work step now enters the existing recharge/suspended-order path even with positive battery charge.
Construction uses the recipe's work-step battery cost; repair retains its current 1-unit cost.
The zero-charge recovery rule remains active for other situations. This does not implement a
general reserve threshold or change repair balance values (F7 remains open).

`tests/DroneReliabilityTests.cpp` covers both jobs with 0.5 charge: no unpaid work occurs, the order
and target are suspended, losing charger power strands the drone without losing the task, and
restoring power allows recharge and work resumption. Design, roadmap, and recovery docs now
describe this work-affordability trigger. Validation: `strategy_game` builds;
`drone_reliability` and `resource_delivery_routing` both pass (24.60 s and 52.37 s respectively).
The full suite was not rerun for this focused fix. Original audit evidence follows for historical context.

**Original finding: P1 — repair stall reproduced; gap in the documented reliable work/recharge loop.**

- Documentation: `docs/GAME_DESIGN.md:187–194` describes charging and deterministic work resumption. `docs/ROADMAP.md:238–247` describes the working drone loop and recovery exit criteria. Both also intentionally specify automatic recovery at zero charge.
- Source: `src/simulation/GameSession.cpp:950–952` starts recovery only at `charge <= 0`. Construction at `:1128` requires at least `dronePowerPerStep`; the failure branch at `:1158` recharges only if charge is already zero. Repair at `:1161–1166` requires at least 1 battery and has no recovery branch for a smaller positive amount. At the work location there need not be further movement to drain the remainder.
- Reproduction: put a drone at a damaged building, set its repair order and battery to 0.5, and advance 300 ticks. Its health contribution remains zero, battery remains 0.5, and `returningToCharge` remains false.
- Observed output: `fractional_repair: ticks=300 battery=0.5 health_change=0 returning=0`.
- Reachability: battery is a floating-point quantity and normal flight drains 0.5 per second (`assets/gameplay/units.json:38`; `GameSession.cpp:1512–1516`), so fractional positive remainders are legitimate gameplay states.
- Qualification: the code complies with the literal zero-only trigger. The inconsistency is with the broader reliability claim, not a claim that reserve-threshold charging was promised. Construction has the same source-level hole, but only repair was exercised in the probe.
- Suggested resolution: explicitly define and implement recovery when a pending work step is unaffordable, even if charge is positive. Add fractional-charge tests for construction and repair without introducing an unrelated reserve policy.

## F3 — Equal-hop power paths ignore the documented total path-length tie-breaker — RESOLVED

**Resolved on 2026-09-27.** The forest now accumulates squared horizontal segment lengths and
compares complete root-to-device ID sequences for equal-cost, equal-hop routes from the same root.
Generator selection remains ordered by hops, direct generator distance, and generator ID. The
same route comparison runs during fallback forest rebuilding. These values are derived each tick;
no authoritative save fields or format change are required.

Power-grid regressions cover the original 300-versus-50 route-cost case, equal-cost paths where
the lexicographically earlier path has a higher immediate-parent ID, adjacency-order reversal,
and per-tick conservation. Original audit evidence follows for historical context.

Validation: `strategy_game` builds and `power_grid_topology` passes in 11.72 s, including the
257-device/600-tick determinism, conservation, split/reconnect, and performance regression probe.
The full suite was not rerun for this focused fix.

**Original finding: P2 — reproduced algorithm mismatch.**

- Documentation: `docs/POWER_GRID.md:102–103` requires equal-hop paths to use lowest total squared segment length, followed by the ordered sequence of stable entity IDs.
- Source: `src/simulation/PowerGridSystem.cpp:275–311` compares hop count, direct target-to-generator distance, generator ID, and immediate parent ID. For paths from the same generator, the direct distance is identical. No accumulated segment cost or full path-ID sequence is compared.
- Reproduction: generator `(0,0)`, target `(10,0)`, lower-ID relay 2 at `(0,10)`, higher-ID relay 3 at `(5,0)`. Connect both two-hop routes. Their summed squared lengths are 300 and 50 respectively. The solver chooses parent 2.
- Observed output: `equal_hop: chosen_parent=2 long_parent=2 cost=300 short_parent=3 cost=50`.
- Impact: ID allocation selects the longer route; with differing relay limits or downstream traffic, this can affect power delivery.
- Suggested resolution: implement the documented accumulated cost and full-path tie-breaker, or explicitly revise the contract if immediate-parent ID ordering is intended. Add this two-route case to the power tests.

## F4 — A transfer limit of zero allows power transfer — RESOLVED

**Resolved on 2026-09-27 by the whole-grid redesign.** All output budgets use the finite
transferLimit directly; zero blocks forwarding. Transit buffers were removed. Definitions reject
non-finite power values. Regression tests verify zero output and immediate multi-hop delivery.

Historical evidence:

**P2 — reproduced; latent definition/contract mismatch.**

- Documentation: `docs/POWER_GRID.md:50–51` defines `transferLimit` as total outgoing capacity per tick. Its invariant at `:186` requires device flow not to exceed transfer capacity. No zero-means-unlimited exception is documented.
- Source: `src/simulation/PowerGridSystem.cpp:420–422` uses the entire transit buffer whenever `transferLimit <= 0`. `src/gameplay/GameplayCatalogue.cpp:784` reads the optional limit, and `:810` rejects negative values but permits zero.
- Reproduction: a generator with 10 generation, zero transfer limit, and one consumer demanding 4 sends 4 power in one tick.
- Observed output: `zero_transfer: sent=4 supplied=4`.
- Qualification: the checked-in ordinary grid devices have positive limits; this is an accepted-data boundary issue rather than evidence that their current limits are routinely exceeded.
- Suggested resolution: define zero explicitly. Either enforce zero output, require strictly positive limits for forwarding devices, or document an unlimited sentinel and adjust the invariant accordingly.

## F5 — An isolated relay is classified as Powered without any source — RESOLVED

**Resolved on 2026-09-27.** Zero-demand devices now require source reachability through usable
output paths to appear Powered. Isolated relays are Offline; a focused regression test covers this.
Consumption-disabled devices are Offline for local work but may still forward power.

Historical evidence:

**P2 — reproduced; documentation itself needs a state-rule clarification.**

- Documentation: `docs/POWER_GRID.md:150` says a device with no valid route to a source is Offline. However, `:148` also defines Powered as supplied power meeting demand, which is ambiguous for zero-demand devices.
- Source: `src/simulation/PowerGridSystem.cpp:489–495` marks every zero-demand participating device Powered, regardless of whether its forest has a root. `assets/gameplay/power.json:26–33` defines the ordinary relay without consumption or generation.
- Reproduction: simulate one enabled, isolated relay with zero demand/generation and an empty buffer.
- Observed output: `isolated_relay: powered=1 root=0 supplied=0`.
- Impact: Powered does not reliably mean connected to usable generation; source-less relay diagnostics can appear healthy.
- Suggested resolution: choose an explicit rule for passive devices and residual buffered energy. Align the state classifier, overlay wording, and documentation, then cover isolated and disconnected relays in tests.

## F6 — Economy/design prose still forbids the priority-first allocator that is implemented — RESOLVED

**Resolved on 2026-09-27.** POWER_GRID, GAME_DESIGN, Resource system, ROADMAP, and agent recovery
documentation now agree: whole-grid allocation each tick, high/medium/low priorities with upstream
consumption first, equal sharing among same-tier requesting connections, no transit buffers, and
storage charging only after all enabled consumer demand is supplied.

Historical evidence:

**P2 — stale and internally conflicting documentation; current implementation matches the newer power document.**

- Documentation: `docs/Resource system.md:501–505` says consumers are served by hop count and distance and that priority must not override spatial order. `docs/GAME_DESIGN.md:221–228` mixes the newer priority behavior with an instruction to remove or repurpose the existing priority control.
- Source: `src/simulation/PowerGridSystem.cpp:356–371` propagates downstream priority, and `:425–449` sorts priority before distance. `tests/PowerGridSystemTests.cpp:245–296` explicitly checks priority and propagation through relays.
- Counterpart documentation: `docs/POWER_GRID.md:64–76` and `:108–118` correctly specify priority-first branch allocation and downstream propagation.
- Impact: a developer following the resource/design text could remove deliberate, tested behavior.
- Suggested resolution: reconcile the older passages with the accepted priority-first contract, unless the design decision is intentionally being reversed. Do not change the allocator merely to satisfy one stale paragraph.

## F7 — Repair balance values are hard-coded outside the definitions — RESOLVED

**Resolved on 2026-09-27.** Unit archetypes now have an optional typed repair definition with
`healthPerTick` and `batteryPerTick`. The construction drone retains 2 HP for 1 battery per tick.
Repair commands and work execution require this capability; execution and recharge checks use
its configured cost. Validation rejects non-finite/nonpositive values, missing batteries, and
steps exceeding full battery capacity. The final partial heal costs a full step; a full target
costs nothing. No new saved entity state was introduced; Alloy costs remain future work.

Validation: full Debug build succeeded; drone reliability, definition validation, and persistence
tests all passed (42.48 seconds). Tests exercise custom 7 HP/2.5 battery steps, health capping,
insufficient positive charge, absent repair capability, and invalid definitions. The full 18-test
suite result above predates this F7 change; these three relevant tests were rerun afterward.

Historical evidence:

**P2 — confirmed architectural-rule mismatch.**

- Documentation: `docs/agents.md:430` says gameplay values belong in definitions. `docs/ROADMAP.md:182–189` requires definition-backed balance values.
- Source: `src/simulation/GameSession.cpp:1161–1162` hard-codes minimum battery 1, battery expenditure 1, and healing 2 per simulation step. These repair values are not retrieved from the actor's definition, a recipe, or a resolved gameplay stat.
- Impact: repair balance cannot be tuned through the advertised gameplay-data boundary; repair also participates in F2's fractional-charge stall.
- Qualification: the absence of an Alloy repair cost is explicitly acknowledged as intended future economy work in `docs/GAME_DESIGN.md:173–174`; that missing cost is not a new inconsistency.
- Suggested resolution: add typed definition-backed repair cost/rate fields and validate them, then have the simulation consume those values.

## F8 — README lists obsolete generated resources — RESOLVED

**Resolved on 2026-09-27.** README now names naturally generated Scrap, Oil, and Uranium
resource fields and separately describes Synthetic production by powered Synthetic Mines.
Verified against the current resource-generation rules, building definitions, and simulation.

Historical evidence:

**P3 — confirmed stale current-feature claim.**

- Documentation: `README.md:13` advertises generated wood, stone, and gold.
- Source/data: `assets/gameplay/resources.json:4–7` defines Scrap, Oil, Uranium, and Synthetic as raw cargo resources. `assets/gameplay/rules.json:23` selects `scrap_field`, `oil_field`, and `uranium_field` for natural generation.
- Suggested resolution: name the current natural resources and explain Synthetic production separately. Decorative stone assets are not the documented old stone economy.

## F9 — README says box selection includes buildings — RESOLVED

**Resolved on 2026-09-27.** README now describes left-drag selection as selecting only the
player's own units, excluding buildings. Verified against the current selection call and
the renderer's owner/unit-kind filters. No gameplay changes were needed.

Historical evidence:

**P3 — confirmed stale control description.**

- Documentation: `README.md:93` says left drag selects units and buildings.
- Source: `src/game/PlayState.cpp:1229–1236` calls `unitsInScreenRectangle()`. `src/render/Renderer.cpp:2553–2554` excludes entities whose kind is not `EntityKind::unit` and entities owned by another player.
- Counterpart documentation: `docs/agents.md:248` correctly describes units-only drag selection.
- Suggested resolution: change the README to describe owned-unit box selection. No selection-code change is needed to match the recovery contract.

## C1 — Clarify what “instant conversion” means at tick boundaries — RESOLVED

**Resolved on 2026-09-27.** Processor conversion now runs in a dedicated phase after all unit
deliveries and destruction cleanup, using the tick's power allocation. Eligible cargo at a surviving,
operational processor converts on its delivery tick, independent of entity creation order.
The stockpile is available for commands processed on the next tick. No processing-duration timer
or new saved state was added. Regression coverage creates the drone before and after the processor
and checks delivery-tick output, plus buffered-cargo retention and conversion on power recovery.

Final validation: full Debug build succeeded. Resource delivery tests passed (104.49 seconds),
drone reliability passed (42.80 seconds), and the game-system rerun passed (214.85 seconds).
Existing delivery/capacity assertions were updated to expect same-tick output while preserving
capacity limits and power-shortage checks. Diff checks passed. The full 18-test suite was not
rerun for this focused timing change.

Historical evidence:

**Qualified observation, not counted as a confirmed gameplay defect.**

`docs/Resource system.md:58–60` says conversion happens instantly after unloading; later prose also calls it near instantaneous. In `src/simulation/GameSession.cpp:890–933`, processors consume buffered input during the entity loop. Cargo is appended later in the drone's iteration at `:1358`.

If the processor precedes the drone in `world_.entities()`, newly delivered cargo is converted on the next tick. If the processor follows the drone, it can convert on the same tick. Thus conversion timing can differ by one 30 Hz tick based on entity ordering. This was source-traced, not separately runtime-tested. Clarify “no processing-duration timer; conversion by the next simulation tick,” or introduce a consistent processing phase if exact same-tick behavior is required.

## Jev evidence and limitations

The first request used shared evidence for eleven questions. The recheck sent each evidence group independently with a narrower question. The zero-charge case was a consistency control, not a finding.

| Case | Initial Jev choice | Focused Jev choice | Focused confidence |
| --- | --- | --- | ---: |
| F1 field metadata | consistent | contradiction | 0.68 |
| F2 fractional battery | consistent | behavioral_risk | 0.52 |
| F3 equal-hop routes | contradiction | contradiction | 0.32 |
| F4 zero transfer | consistent | contradiction | 0.27 |
| F5 isolated relay | consistent | contradiction | 0.18 |
| F6 priority prose | contradiction | contradiction | 0.47 |
| F7 repair constants | consistent | contradiction | 0.68 |
| F8 resources README | consistent | contradiction | 0.76 |
| F9 selection README | consistent | contradiction | 0.77 |
| C1 conversion timing | consistent | behavioral_risk | 0.72 |
| Zero-only recovery control | consistent | consistent | 0.94 |

These confidence numbers measure the model's choice distribution, not the probability that the report is correct. In particular, the model initially missed straightforward README mismatches. The report preserves that disagreement and relies on explicit source conditions and executable results for the retained conclusions.

TypeSafe references used: [API](https://docs.typesafe.ai/api), [Choice](https://docs.typesafe.ai/primitives/choice), [citation-check pattern](https://docs.typesafe.ai/cookbooks/citation_check). The API credential is not stored in this report or repository.

## Validation and continuation

Completed:

1. Inspected the dirty/staged working tree and current commit.
2. Built current `strategy_simulation` and its dependencies successfully with `cmake --build build/debug --config Debug --target strategy_simulation -j 8`.
3. Built and ran a standalone C++ probe against those Debug libraries. The five observed outputs are recorded under F1–F5; the process exited successfully.
4. Performed direct source/data checks for F6–F9 and the qualified C1 timing observation.
5. Used Jev for initial and focused evidence classification, preserving contrary/uncertain results.

Scratch probe source, CMake files, and secret-free Jev request/response JSON are retained outside the game repository under `C:\Users\Brord\Documents\Codex\2026-09-26\i-ha\work` (`audit-probe`, `jev-audit-request.json`, `jev-audit-response.json`, `jev-recheck-response.json`). The scripts accept a credential at invocation and do not embed it. The probe injects transient field geometry only in its fixture; it does not modify production code.

Recommended next work: F1–F9 and C1 are resolved.
Keep planned networking, Data/Authority loops, water/vegetation refinement, fixed-integer power
conversion, and later combat milestones distinct from contradictions in current implemented behavior.
