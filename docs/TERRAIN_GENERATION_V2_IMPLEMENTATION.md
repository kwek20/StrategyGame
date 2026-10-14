# V2 implementation and verification

The design in `TERRAIN_GENERATION_V2.md` remains the target. This document distinguishes the first
playable implementation from future capabilities. Concept images guide appearance; verification
uses captures from the renderer, not generated illustrations.

## Reusable stages

- `TerrainV2Recipe` loads internal profiles and layout recipes from `assets/gameplay/terrain/v2.json`.
  It intersects relief constraints, resolves water policy, validates dimensions and opening capacity,
  and excludes unsupported competitive recipes before generation. Distances and heights use world units.
- `TerrainV2Plan` reserves player openings, two land approaches around a curved open ring, and seeded compact
  landmarks. Reservations constrain relief and scenery without roads. Opening envelopes include
  sampling-cell padding so processor sites retain their horizontal plane. Resource field footprints
  and nodes stay outside the usable construction envelope. Water reservations exist only when a
  recipe resolves positive water coverage.
- `TerrainV2Generator` samples broad natural fields through stable area samples. Openings and routes
  shape Layer A before elevation resolution. Relief, water and landmarks use separate seed streams.
  Supported runtime player count is the game's two-player registry.
- `TerrainTerraces` keeps source samples immutable, performs bounded score-improving sweeps, suppresses
  accidental isolated cells and records source error. It owns cardinal flat/ramp/cliff edges. Ramps
  follow reserved approaches and dominant source slopes; remaining ledges stay blocked. Junctions
  use triangle fans; alternating cliff corners cannot acquire an intermediate diagonal landing.
- `TerrainGenerationPipeline::assembleV2` serves generation and provisional repairs. It rasterizes
  onto the existing welded mesh, creates independent dry fields or local lake water, classifies
  materials and fingerprints geometry/movement. Queries and rendering use the same triangle diagonals.
  Runtime cell/transition dimensions align with the 0.375-unit raster. The current recipes use
  1.5-unit cells, half-unit elevation steps, 1.5-unit ramps and 0.375-unit cliff runs. Longer
  ramps need a wider transition representation rather than ineffective tuning values.
- `SurfaceGeometry` provides polygon clipping, analytic disk intersections and support components
  joined only through positive-length shared triangle edges. Corner contact and clipped-away edges
  cannot combine support islands. `TerrainSupport` clips
  final triangles against rotated rectangles or circles, measures connected support, applies strict
  overhang/depth rules and classifies full/partial wet tiles. Perimeter space derives from the declared
  interacting unit's radius, movement domain and interaction margin. Foundations never alter terrain
  or water. Support walls follow the unchanged bed; buildings and construction ghosts use their
  evaluated foundation plane. Shore requires explicit wet-fraction bounds and a minimum connected
  dry-support fraction; absent or incompatible contracts reject.
- Navigation checks owned cliff edges and conservative footprint clearance against a fine mask.
  Direct control checks movement segments. Resource land travel queries respect owned edges.
  Visual vegetation avoids reserved openings and approaches. V2 recipe forest settings independently
  tune density, cluster size, spacing and scale; V1 retains its own vegetation rules.

## Current layout scope

| Layout | Implemented recipe |
|---|---|
| Open Plains | Dry low relief, broad openings, sparse landmarks |
| Continental | Dry regional relief; not the future coastal/ocean variant |
| Rolling Hills | Dry successive bands and compact outcrops |
| Central Hill | Dry central elevation with surrounding approaches |
| Central Water | One irregular local lake, lowland openings and two land approaches |
| Archipelago | Definition retained; V2 launch unavailable pending transport |

V1 remains independently selectable and its global water configuration is unchanged. V2 reads its
own recipes, allowing Central Water to be wet while V1 stays dry. Profiles remain internal settings.

## Resources, repairs and persistence

Opening inventories remain definition-backed: 600 scrap and 400 oil per player with current nodes.
Abundance calculates additional fields without subtracting starting fields or imposing V1's minimum
additional allocation. Counts round to equal two-player batches and can be zero. Node compositions
and capacities are preserved; no extra opening nodes are added.

Resource placement remains provisional until the whole allocation succeeds. Failure first removes
obstructing peak relief, then uses a constructive lowland fallback with deterministic shore geometry
and compact node patterns. Retry layouts use distinct deterministic streams. The final template's
geometry and eligibility use fixed streams, so the failed seed cannot alter its capacity. The
original seed is retained. Repairs can exceed ordinary source bounds and
report their level. Fallback preserves the lake, resource eligibility, separation, local capacity and
access rules without silently dropping fields. Its surface is independent of seed-driven relief.
Shipped preset capacities are tested explicitly even when ordinary generation succeeds.

Format 1 stores V2 terrain recipe revision 2 and the repair level, plus complete foundation identity,
shape, plane, progress and slab data. Mode, repair, foundations and the surface fingerprint contribute
to authoritative checksums. Restore stages the session's actual surface into the renderer.

## Initial implementation verification and remaining work

Verified on 2026-10-10 with a clean full Debug build, repository whitespace checks and clang-format
checks on new C++ files. The complete suite was run; its fallback seed-independence failure was
fixed and rerun. Final affected checks passed after the subsequent opening and triangle-support
fixes. All 23 test areas have passing coverage across the global run and affected reruns.

| Check | Final evidence |
|---|---|
| V2 generation | 45 repeated layout/size/seed cases; immutable sources, bounded cleanup, dry independence, edge/junction topology and cancellation |
| Support | Exact rectangle/disk areas, corner-only disconnection, clipped edge contacts, strict one-third boundary, foundation depth, Land/Water/Shore contracts and unchanged surfaces |
| V2 sessions/resources | 35 ordinary sessions plus 45 forced fallback allocations across five layouts, 10/15/20 sizes and abundance presets; exact field counts and populated-world ground routes |
| Joint openings | Both players fit processors and a charging pad alongside headquarters, workers and resources; ground exits remain open on all five supported recipes |
| Persistence | Setup toggle, complete foundations, deterministic terrain restoration, prototype rejection and format-1 round trips |
| V1 regression | Full map audit, game systems, resource delivery, drone reliability, power topology, terrain, vegetation and existing smaller checks passed |
| Renderer | Five recipe overviews, six inhabited Central Water/edge/whole-map views and 80 continuous camera-motion frames; no GL errors; integration and architecture checks passed |

The last resource/session matrix took 255.94 seconds in Debug. The final support/generation run
took 79.21 seconds; joint openings took 13.01 seconds. These are audit runtimes, not comparable
per-map or frame-time benchmarks. V2 preserves authoritative topology by drawing its fine terrain
mesh; a future performance pass must retain that geometry when introducing transition-aware LOD.

Actual screenshots are in the [engine capture gallery](pictures/terrain-v2-layouts/implementation/README.md).
Iteration captures and build/test logs remain under the ignored `build/debug/audits` directory.

Iteration corrected fallback packing/retry diversity, seed-dependent fallback eligibility, implicit
minimum expansion counts, resource overlap with early construction envelopes, boundary sampling,
shore transitions, water-surface clamping, camera-dependent terrain LOD, preview height and
corner-only support connectivity.

Remaining capabilities include ocean/coastal recipes, river reaches/confluences, island progression
and production Water/Shore building content. Shore support fractions are implemented as an explicit
contract, not inferred from partially wet ground. The building catalogue
currently contains Land contracts. Floating/bed geometry tests do not imply playable naval transport.

The initial template uses an outer approach ring. Developer-defined topology graphs, resource-site
reservations before sampling, capacity proofs for arbitrary new recipes and localized transactional
repairs remain extensions. Constructive fallback can flatten large land regions; track its frequency
rather than accepting that appearance during ordinary generation.

Static views, camera motion samples and GL checks cannot prove that every possible camera path is
artifact-free. Detailed salvage art and richer scenery composition need a separate content pass.
Node tuning remains deferred as specified. Populated-world routes and joint early building layouts
are tested. Per-building interaction routes with final foundation collision and broader
recipe-authoring validation remain audit targets beyond terrain-only perimeter checks.


## Visual refinement against the no-roads reference

The previous six-unit cells and two-unit steps made the lake and peaks read as large blocks.
The current pass retains source relief but divides it into much smaller land contours. Water-bank
width is an explicit twelve-unit recipe value shared by generation and constructive fallback,
independent of terrain cell size. Opening transitions likewise retain an independently authored
twelve-unit blend instead of shrinking with finer cells. Lake depth is four units. Additional shoreline harmonics form
small bays, landmark contours vary around their radius, and recipes can place tiny interior islets.
Constructive resource fallback clears those islets and other discretionary landmarks.

Curved approach reservations reduce peak and mountain-shoulder influence rather than clamping a
straight strip to one height. This removes artificial road-like cuts while keeping the terrain's
source contours. The renderer uses a brighter V2 meadow/soil/stone palette and turquoise-to-blue
water with the existing texture detail. V1 shaders retain their previous palette. No textures or
third-party models were downloaded for this pass.

Two original low-poly conifer assets join the existing broadleaf scenery. Rebuild them with
`python tools/generate_terrain_conifers.py`; their generator and attribution note live in the repo.
Their rules are V2-only, and all trees still respect building/resource clearance and reserved routes.
Finer cells use four area samples rather than sixteen; large authored cells retain sixteen. Route
influence has a conservative interior early-out, and peak queries skip absent landmark influence.

The capture gallery now includes a populated whole-map view. Captures use the real renderer with
seed 7; camera motion exercises shore/cliff/map boundaries. The old V2 local saves are deliberately
rejected because their regenerated terrain no longer matches their stored entities; save format
remains 1. Start a fresh V2 match for the revised terrain.


The V2 session matrix is split into five CTest entries, one per supported layout, so independent
resource/navigation/save audits can run concurrently. Each entry still covers all three map sizes,
low/high abundance, both player openings, and three abundance levels for forced fallback. Save
fixtures use layout-specific temporary paths. The repeated source-generation matrix likewise has
one CTest entry per layout (300-second
Debug windows). Geometry, support and save contracts retain a separate 120-second check.
These timeouts are audit bounds, not per-map runtime targets. The global run exposed the old
monolithic audit windows as too short for the finer source grid; case coverage was retained
when splitting the matrices.


## Final visual-pass verification (2026-10-10)

The complete Debug build passed without compiler warnings. Clang-format checks on the changed new
modules/tests and `git diff --check` passed. All 32 current CTest entries have passing coverage
across the global run and final affected reruns. The global run's monolithic V2 audits timed out;
their complete case matrices were retained and split by layout, then passed. No assertions were
relaxed. The short support/save check also updates the obsolete-recipe rejection expectation for
terrain revision 2.

| Final check | Evidence |
|---|---|
| Repeated heightfields | All five layout entries passed; 45 layout/size/seed cases repeated, source bounds and dry-water independence retained; final batch 235.13 seconds |
| Geometry/support/save | Passed in 25.34 seconds; includes strict area/depth rules, unchanged foundations, inactive-water fallback, seed-independent repair and obsolete V2 save rejection |
| Sessions/resources/routes | All five layout entries passed; 35 ordinary sessions and 45 forced fallback allocations; every ordinary result reports repair level 0; final batch including openings 238.87 seconds |
| Joint building openings | Both players' processor/charging-pad arrangements and exits passed after the twelve-unit opening blend; 23.51 seconds |
| Rendering | Five terrain overviews, six inhabited Central Water views and 80 motion frames passed; final captures inspected at the center, all four map edges and whole-map scale; no GL errors |
| V1/global regression | All 20 existing non-V2 areas passed in the global run, including the full map audit (423.89 seconds), game systems (259.61 seconds), asset imports, UI, navigation-related simulation and rendering |

Detailed logs remain in ignored `build/debug/audits/`: `v2-visual-validation.log`,
`v2-visual-session-final.log`, `v2-visual-fields-final.log`, `v2-visual-last.log` and
`v2-visual-build-verified.log`. Audit times are Debug test-matrix runtimes, not individual map-load
benchmarks. The before/after engine capture gallery is the visual evidence for this refinement.

## Natural-material presentation (2026-10-11)

V2 now retains its material texture colours, uses subdued ground and water palettes,
and renders a cached terrain backdrop beyond the active map into matching atmospheric
haze. Terrain generation and playable boundaries are unchanged. See
[presentation details, captures and verification](TERRAIN_V2_PRESENTATION.md).
