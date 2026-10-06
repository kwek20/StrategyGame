# Budgeted resource distribution — 2026-10-06

## Implemented behaviour

New maps reserve exactly **600 Scrap and 400 Oil per player**. Starting fields count toward the
map allocation; shortages and fairness repairs cannot inflate the resource total. These opening
amounts are provisional, configurable inventories rather than established economic balance.

Starting Scrap is two medium nodes; starting Oil is two small nodes. Expansion fields contain
1,000 Scrap (small/medium/large), 600 Oil (two medium), or 200 Uranium (two small). Every map
allocates Uranium to contested sites. Fields are irregular arrangements of individually harvestable,
exhaustible nodes, rather than a continuous tile carpet.

The total field count is area × density × abundance, rounded to a complete player group with at
least one field per player. Reserved starting fields count toward it. The remaining fields receive
their fixed expansion inventories. Larger maps and abundance settings change the number of sites;
they do not alter opening supply or individual node capacity.

Biome, moisture, normalized height and regional potential influence site preference. Safe dry
land is eligible as fallback; water, forbidden tags, excessive slope, boundaries and collisions
remain hard restrictions. This resolves the earlier Archipelago opening-height exclusion without
changing terrain generation or moving player starts.

Starting placement prefers nearby gatherer travel costs with no maximum-distance cutoff. Paired
supply access must either have a minimum ratio of 0.75 or differ by at most 12 travel-cost units.
Reserved nodes are closer by travel cost to their intended player. Expansion sites stay beyond
starting access areas and a configured expansion travel cost of 65.

All fields maintain a 10-unit clear gap between footprints. Conservative whole-field neighborhoods
bound any sliding radius-18 window to at most 2,000 Scrap, 1,200 Oil, or 400 Uranium. Whole field
inventories commit together; rejected candidates cannot silently remove nodes. Expansion failures
relocate the same inventory with up to eight deterministic attempts. Entities are instantiated only
after all families succeed.

## Validation

- Full Debug build passed.
- All 19 regular CTest targets passed (332.61 seconds while the map audit ran concurrently).
- The map audit passed **54/54 cases**, previously 50/54: all six layouts, 10/15/20 chunk sizes,
  seed 1592594996 at abundance 1, seed 123 at 0.5, and seed 123123 at 2.
- All four previously failing Archipelago cases passed. No resource-layout retry was required in
  these sampled cases; existing terrain retries for invalid starting positions remain independent.
- Repeated generated matches and accepted-terrain-seed replays matched authoritative checksums.
- Audit assertions check exact opening inventories, total budgets, complete fields, field gaps,
  concentration limits, node collisions, neutral resource placement, and actual drone navigation
  to every reserved node. Starting worker connectivity also remains covered.
- Focused tests check fixed opening amounts and unchanged opening positions across abundance,
  invalid inventory/cap rejection, responsive cancellation, expansion search beyond the old
  55-unit cutoff (forced 60-unit start exclusion), and late-family failure after all eight retries
  leaving no partial resource entities in the caller's world.
- Exported top-down plots were inspected for all six layouts. Starting supplies are compact and
  distinct; expansion fields have clear gaps and individually readable nodes. These plots inspect
  allocation geometry, not an in-game art/rendering approval.

Across 108 player starts in the audit, Scrap access costs were 14.49–21.66 (median 17.83), and Oil
15.66–24.49 (median 20.07). Some pairs use the absolute-difference tolerance rather than the ratio
because the base costs are small.

## Ten-minute logistics probe results

All six scenarios passed (three map sizes × two separately exercised chains), for both players.
Both reserved nodes depleted completely in each scenario. Command Hub processing yielded about
300 Alloy from 600 Scrap or 200 Fuel from 400 Oil, consistent with its 0.5 conversion yield.
No extra resource or drone entities were introduced. The existing 150 Alloy starting stockpile
made the final Scrap-scenario stockpile approximately 450 Alloy.

First observed depletion at minute-resolution checkpoints, seconds from match start (P1/P2):

| Map size | Scrap exhausted | Oil exhausted |
|---|---:|---:|
| 10×10 | 300/300 | 240/240 |
| 15×15 | 300/300 | 180/180 |
| 20×20 | 240/300 | 240/180 |

The timings reflect unopposed gathering through the starting hub; they are not expansion or
competitive balance targets.

## Repeatable checks and artifacts

Definitions: `assets/gameplay/resource_fields.json`.
Contract: `docs/Resource system.md`, terrain sections 8–9.

```powershell
cmake --build build/debug --config Debug -j8 -- /p:LinkIncremental=false
ctest --test-dir build/debug -C Debug -E '^map_generation_audit$' --output-on-failure -j4
ctest --test-dir build/debug -C Debug -R '^map_generation_audit$' --output-on-failure
$env:STRATEGY_MAP_AUDIT_MODE='opening-economy'
try { & build/debug/Debug/map_generation_audit_tests.exe } finally { Remove-Item Env:STRATEGY_MAP_AUDIT_MODE }
powershell -NoProfile -ExecutionPolicy Bypass -File tools/render_resource_audit.ps1
```

Audit outputs: `build/map-generation-audit/results.csv`, per-case logs, field/node/start CSVs and
terrain rasters. The plotting script writes `resource-layouts.png`; `-Seed`, `-MapSize` and `-Output`
select additional views. `opening-economy.csv` records minute-by-minute supply and delivered output.

## Limits and next balance work

The audit samples a finite matrix; seed and abundance are paired rather than a full Cartesian
sweep. Water generation remains disabled by the current global configuration. Optional water
behaviour is covered by existing tests, not the 54-case resource audit.

The ten-minute probe exercises each starting chain separately using the real starting drone and
Command Hub on 10/15/20 maps. It validates resource reach, repeated gathering, delivery and
emergency conversion, without spawning extra nodes or units. It does not establish first-factory,
processor construction, expansion timing, combat exposure or complete match balance. Those need
representative infrastructure and competitive playtests before treating the provisional budgets
as final.
