# Map generation audit — 2026-10-04

## Outcome

**50 of 54 sampled configurations generate playable openings. Four Archipelago configurations fail
the required opening Scrap supply. Map generation does not receive an unconditional OK.**

The audit uses the actual `GameSession` generation path, not a substitute generator. It covers all
six selectable layouts and 10×10, 15×15 and 20×20 chunks, with three seed/abundance pairs per layout
and size:

| Requested seed | Resource abundance |
| --- | --- |
| 1592594996 (`0x5EED1234`) | 1.0 |
| 123 | 0.5 |
| 123123 | 2.0 |

This is 54 cases, not a full seed-by-abundance Cartesian product or a statistical reliability claim.
All 45 non-Archipelago cases passed. Archipelago passed five of nine.

## Unresolved generation failures

| Layout | Size | Seed | Abundance | Failure |
| --- | --- | --- | --- | --- |
| Archipelago | 10×10 | 1592594996 | 1.0 | Player 2 has no opening Scrap |
| Archipelago | 15×15 | 123 | 0.5 | Player 2 has no opening Scrap |
| Archipelago | 20×20 | 123 | 0.5 | Player 2 has no opening Scrap |
| Archipelago | 20×20 | 123123 | 2.0 | Player 1 has no opening Scrap |

Starting-position validation accepts dry, connected, buildable land independently of resource
suitability. Archipelago can create low terrain that meets those tests but lies below Scrap's
minimum normalized height of 0.24. A one-world-unit grid survey of the 16–55-unit opening annulus
found no height-eligible samples for the failing player in all four cases. Their sampled maximum
heights, in table order, were 0.204910, 0.191399, 0.200186 and 0.233219. The last case also has no
height-eligible samples near player 2 (maximum 0.206756). The existing 4096 candidate attempts produced zero
Scrap capacity there, against a requirement of 600. Increasing random placement attempts would not
address the terrain mismatch demonstrated by these samples.

The current documented contract explicitly stops on exhausted opening-resource placement and does
not retry terrain or relocate accepted starts to improve resources. A design choice was requested:
extend the existing bounded terrain retry to resource failures, or retain that contract and reject
these configurations. Pending that choice, the current contract is retained. No resource thresholds, corner rules, terrain seed inputs, water settings or
retry limits were weakened to make the audit pass. The four failures remain visible in the audit
test's nonzero exit status.

## Confirmed fixes

1. **Accepted-seed reproducibility.** After a terrain retry, resources used the original constructor
   seed while terrain and vegetation used the accepted seed. Resources now use `terrainSeed_` too.
   The audit checks both repeated requested-seed generation and reproduction from the accepted
   seed. Archipelago 10×10 exercises actual retries for seeds 123 and 123123, accepting 1534179274
   and 4158714513 respectively.
2. **Cancellation and error classification.** The terrain retry previously caught all runtime
   errors, including cancellation and configuration errors. A typed `StartingPlacementFailure`
   now identifies retryable start failures. Start selection checks cancellation during connectivity
   traversal and candidate search; unsupported map/player counts fail before traversing the map.
3. **Failure diagnostics.** Logs now identify requested and attempted seeds, layout and map size.
   Final terrain exhaustion includes the last placement reason and requested seed.
4. **Player-facing failure feedback.** Exhausted generation now clears loading and returns to match
   setup with “GENERATION FAILED. TRY ANOTHER SEED OR LAYOUT.” The existing configuration is retained.
   The message fits the renderer fixture at 1280×720 and 150% scale.
5. **Stale four-player regression.** The prior failing `game_systems` test directly demanded four
   starts from one water-enabled terrain. The failure occurred there, before the later default-match
   fixture. All 16 possible player-two corner candidates fail headquarters terrain checks. The test
   now exercises the same bounded deterministic retry-seed sequence as production, still requiring
   four connected starts and the configured opponent separation. Water-navigation tests retain the
   original water-enabled terrain. The game-system suite passed after this correction.

## Checks applied to each successful case

- Two separated starts, finite entity positions, and entities inside the match bounds.
- Starting units placed on terrain allowed by their movement profile.
- An actual worker navigation route reaches the opposing worker after resources are instantiated;
  merely receiving a fallback route is insufficient.
- At least 600 nearby Scrap and at least 600 reachable by the starting construction drone.
- Resource nodes are not submerged and do not overlap one another.
- Each successful default-seed 10×10 case is generated twice and has an identical authoritative
  checksum (five completed repeats; the corresponding Archipelago case is rejected).
- Cases that retry terrain reproduce the authoritative checksum from their accepted seed.

## Reproduction and evidence

```powershell
cmake --build build/debug --config Debug --target map_generation_audit_tests -j 8
ctest --test-dir build/debug -C Debug -R '^map_generation_audit$' --output-on-failure
```

For the failing layout alone:

```powershell
$env:STRATEGY_MAP_AUDIT_LAYOUT = 'archipelago'
build/debug/Debug/map_generation_audit_tests.exe
Remove-Item Env:STRATEGY_MAP_AUDIT_LAYOUT
```

The executable writes CSV results and per-case diagnostics under `build/map-generation-audit/`.
`baseline-results.csv` preserves the first full 54-case pass; `results-archipelago.csv` records the
focused rerun including accepted-seed replay and height surveys. The test fails if any sampled case
fails. Normal regression tests are run separately with `-E '^map_generation_audit$'`, so a green
normal suite does not conceal the failed map audit.

Final normal regression run: **19/19 passed**, 283.88 seconds, including `game_systems`
(247.08 seconds). The separate Archipelago rerun still fails four of nine cases; both cases that
retry terrain pass accepted-seed replay. `git diff --check` passes. The generation-failure message
was rendered and visually inspected at 150% scale.

## Follow-up investigation — 2026-10-05

The Archipelago layout intentionally lowers non-island terrain toward `waterLevel - 0.08`.
With global water generation disabled, the generation pipeline clears water and excludes water
biomes before classifying that low terrain. It can therefore become dry, buildable starting land,
while Scrap's independent minimum height remains 0.24. This explains the otherwise surprising
combination of accepted starts and no eligible nearby Scrap.

A test-only `STRATEGY_MAP_AUDIT_MODE=retry-probe` experiment tries subsequent deterministic seeds for
the four failed configurations, preserving all terrain, resource and start rules. It counts nested
start-selection terrain retries against an eight-generation comparison budget. It also verifies
navigation, opening supply, resource non-overlap and accepted-seed replay for any successful match.
This experiment does not enable resource-failure retries in production.

**Result: all four configurations recovered within two or three terrain generations**, including
the rejected original terrain and nested start-selection retries. Verification replay generations
are additional test work and are not included in this recovery count.

| Size | Original seed | Successful accepted seed | Terrain generations |
| --- | --- | --- | --- |
| 10×10 | 1592594996 | 3367082360 | 3 |
| 15×15 | 123 | 1534179274 | 2 |
| 20×20 | 123 | 1534179274 | 2 |
| 20×20 | 123123 | 3975848253 | 3 |

Every recovered match passed the same opening-supply, actual navigation, resource-overlap and
accepted-seed checksum checks. The probe exits successfully and records its results in
`build/map-generation-audit/resource-retry-probe.csv`.

```powershell
$env:STRATEGY_MAP_AUDIT_MODE = 'retry-probe'
build/debug/Debug/map_generation_audit_tests.exe
Remove-Item Env:STRATEGY_MAP_AUDIT_MODE
```

Recommendation: extend terrain acceptance to include opening-resource and fairness validation,
using one shared generation budget. Keep each terrain's accepted starts fixed; discard the whole
candidate world when its resources cannot satisfy the rules. Reset entities, foundations and
navigation state before trying the next terrain, and retain the final accepted seed. Do not nest
two independent eight-attempt production loops: the probe is evidence of recovery viability, not
that proposed production implementation. This changes the explicit resource-failure policy and is
pending the requested design choice. The four production audit failures therefore remain open.

The final full Debug build passed using `/p:LinkIncremental=false` after an MSVC incremental-link
error was cleared. The follow-up adds only a test experiment and documentation; production code is
the same version that passed the 19 normal regression tests above. The accepted-seed fix changes
resource placement for newly generated matches that retry terrain; existing saved entities are
not regenerated or migrated.

## Scope limits

Water generation is globally disabled in the current layout definitions. “Central Water” and
“Archipelago” were audited as currently configured dry layouts. Water-enabled navigation and
four-player placement have separate existing coverage; full water-enabled layout playability,
river appearance, custom maps, and every possible seed are not certified by this audit. Water and
vegetation presentation remain outside this change.

Reported case timings include generation, navigation checks, optional duplicate generation and,
for resource failures, an additional terrain survey. They are not isolated generation benchmarks.
The runtime `gamedata/match_setup.json` change predates this audit and is preserved. No game was
saved, no user configuration was rewritten, and no commits were created.

## 2026-10-06 resource redesign follow-up

The budgeted distribution described in [RESOURCE_DISTRIBUTION_2026-10-06.md](RESOURCE_DISTRIBUTION_2026-10-06.md)
replaces the earlier minimum-capacity/additive-compensation policy. The same 54-case audit now
passes 54/54, including the four Archipelago resource failures recorded above. The earlier results
remain as historical diagnosis, not the current generator's status.
