# V2 implementation and verification

Dry Land placement first tries a fitted tilt up to 10 degrees without earthwork. A bounded
candidate search also considers the largest continuous dry patch, the mean gradient, a level
plane and a blend of dominant/patch gradients. Repair heights target the area-weighted ground height first, with a median and
two shallow offsets as fallbacks within the existing support band. Candidate work shares one clipped local surface patch,
median results, adjacency and original-surface outcomes. Plans without a scaffold are preferred over partial grading/support plans and ungraded frames.
The ordinary base conceals up to 0.1 units. Grading permits at most 0.5 units of fill on shallow
residual relief, retaining the 0.6 cut / 0.1 average cut limits and adding a 0.15 average combined
earthwork limit. Both signs of movement and the feathered border count toward the budget.
All candidates obey the unchanged total-angle, cut-depth, complete feathered-volume, water,
protected-foundation and at-least-half connected dry bearing limits. Water-overlapping placement keeps its
horizontal contract. See the [tilted placement engine captures](pictures/building-placement/tilt/README.md).

Floor contact is measured within the shallow base-height band. Foundation bearing extends down
continuous dry ground within the depth limit. Canonical cliff faces separate shelves; a supported
component must contain positive-area floor contact, so an unrelated lower shelf cannot inflate
support. At least half the footprint must belong to that connected bearing region. Positive
shared-edge intervals connect support; corner contact or disjoint portions of one edge do not.
The overlay partitions supported and unsupported regions without overlap and uses compact
position/color vertices. Circle acceptance is analytic; presentation uses a circular fragment mask.

`GameSession::planBuildingPlacement` supplies the shared placement rules and returns a structured
outcome, blocker, terrain result, collision shape and foundation. The preview caches only terrain
analysis keyed by exact pose, footprint, profile and a transient terrain/protection revision.
Ownership, costs, requirements and occupancy remain live. Previously explored fog does not expose
hidden occupancy through the preview; commit always checks the real world without trusting its cache.
The accepted foundation is built from that plan, avoiding a second terrain solve after spending.

Preview excavation uploads a bounded height/normal texture for vertex-shader presentation. It never
modifies persistent terrain, foundation ownership, navigation, vegetation or resource caches.
Scaffold feet sample the same proposed raster heights as the eventual accepted cut. Ghosts use an
entity-only draw path; overlay/scaffold buffers and ghost entities are reused. Cache statistics are
reset once per actual frame and accumulate across render passes.

Bounded [dry-ground levelling](pictures/building-placement/leveling/README.md) applies only after
acceptance; cancellation/destruction restores the owned original region. The frozen adjustment is
saved with its foundation, independent of construction progress. A committed cut refreshes only
nearby navigation heights. Preview and authority share exact planes and rotations; a queued click
owns its builder IDs, recipe, heading and resolved position independently of later cursor movement.
Right-click rotates 45 degrees; right-drag controls the camera; Escape cancels. Existing entity and
foundation rotation fields persist the heading without changing save format 1 or generation revision 7.

V2 foundations now use shared procedural metal scaffold geometry for placement previews and the
finished world: a closed thin deck, bed-anchored feet/columns and X braces where exposed bays allow
them. Dark steel receives subtle wear/rust and sparse hazard collars. Construction progressively
reveals members while the building plane remains fixed. Per-foundation GPU meshes are reused
across progress changes; geometry changes, removal and terrain replacement invalidate the relevant
cache. See the [engine capture gallery](pictures/building-placement/scaffolds/README.md).
The scaffold pass itself is presentation only. The separate levelling change above adjusts bounded
dry ground and persists its overlay; V1, water heights, resource budgets and save format remain unchanged.

Gentle-ground placement now uses an authored 0.25-unit support-height band plus each footprint's
slope limit. It accepts shallow natural relief that previously reported 0% support, while retaining
connected-support, strict cliff-overhang, water eligibility and foundation-depth checks. The floor
now follows the fitted plane on dry Land; terrain changes only through the bounded cut allowance.
Shallow remaining relief receives a foundation base.

The [building placement feedback pass](pictures/building-placement/README.md) adds explicit
simulation outcomes, Escape cancellation with right-drag camera control, footprint/support/foundation previews and
distinct support/depth messages with a cursor readout. It preserves the existing terrain
support ratios and generation revision. Bounded excavation is the explicit surface exception.
Player placement does not require usable perimeter interaction space.

Generation revision 7 moves composition to the field: many small individually harvestable targets
replace two or three baked compounds. Exact starting/expansion supplies and abundance-controlled
field counts are preserved. A field's seeded node mix partitions that budget exactly; every node
saves its neutral field membership and remaining supply. Depletion prefers another live node in
the same field, then retains the bounded nearby-source fallback. Hovering outlines visible field
members; shared ground links cannot cross field identities. Repeated nodes use persistent instanced
buffers, with field-local updates on depletion. See `RESOURCE_CLUSTERS.md` and the field-node gallery.
Save format remains 1; generation revision 7 requires a fresh match for older development V2 saves.

Generation revision 6 increases minimum player-start separation by one chunk (24 world units):
half the map width plus 24, configured by `landforms.openingSeparationPadding` in `v2.json`.
This gives minimum separations of 144, 204 and 264 on the three supported map sizes.
Existing matches keep their positions; new V2 matches use the revised spacing. Older V2 saves
require a fresh match because the generation revision has changed.

The design in `TERRAIN_GENERATION_V2.md` remains the target. This document describes the current
playable implementation and separates it from future capabilities. The seeded-landform update
below supersedes the earlier fixed opening/route-ring implementation. Concept images guide appearance;
verification uses captures from the renderer, not generated illustrations.

## Reusable stages

- `TerrainV2Recipe` loads internal profiles and layout recipes from `assets/gameplay/terrain/v2.json`.
  It intersects relief constraints, resolves water policy, validates dimensions and opening capacity,
  and excludes unsupported competitive recipes before generation. Distances and heights use world units.
- `TerrainV2Plan` chooses separated opening candidates across the active map, seeded field
  orientation/stretch/offset, irregular elliptical lake proportions and connected bent ridge segments
  with lower branches, sustained crests and asymmetric escarpments. Ridge centres have bounded retries;
  overlapping arms can follow the bank but fade out before entering water or protected openings.
  There is no four-sided route reservation or outer peak ring. A bounded
  corner capacity arrangement is retained only when random start selection exhausts its attempts.
  Water activates only when the resolved recipe has positive coverage.
- `TerrainV2Generator` samples broad natural fields through stable area samples. Plains and hills
  weight their regional signals differently. Layer A protects opening cores, adds active water and
  composes ridges before connections are found. Supported runtime player count remains two.
- `TerrainV2Access` searches the sampled dry landscape for low-cost connections, preferring valleys
  and gentle contours. It eases excessive elevation transitions locally and retains neighbouring
  heights wherever they already meet the connection profile. No roads or route-count equality are
  imposed. Its indexed access mask supports transition/scenery queries without scanning segments
  per sample. These are minimum connectivity safeguards, not the entire set of usable routes.
- `TerrainTerraces` keeps source samples immutable, performs bounded score-improving sweeps, suppresses
  accidental isolated cells and records source error. It owns cardinal flat/ramp/cliff edges. Connection samples retain their planned elevation during cleanup. Ramps
  follow selected passages and dominant source slopes; remaining ledges stay blocked. Junctions
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
  overhang/depth rules and classifies full/partial wet tiles. Perimeter interaction access does not
  gate player placement. Foundations never alter terrain
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
| Rolling Hills | Seeded regional bands, bent ridge groups, rocky shoulders and valleys |
| Central Hill | Dry central elevation with surrounding approaches |
| Central Water | One principal lake with variable proportions/bays and connected surrounding land; no prescribed approach count |
| Archipelago | Definition retained; V2 launch unavailable pending transport |

V1 remains independently selectable and its global water configuration is unchanged. V2 reads its
own recipes, allowing Central Water to be wet while V1 stays dry. Profiles remain internal settings.

## Resources, repairs and persistence

### Resource-site appearance pass (2026-10-21, revision 5)

Resources retain their existing inventories, target counts, collider radii and access rules. A separate
composition stream biases target arrangement along an authored local direction (`directionalBias` in
`terrainV2Composition`), with ordinary placement/fallback checks still in charge. V1 placement is unchanged.

`world/ResourceSiteMap` builds a CPU-only, irregular site mask from stable resource identities and
positions. Its density also guides canopy clearance. `render/ResourceSiteTexture` owns the GPU cache:
unchanged inputs reuse pixels/texture, while changes rasterize and upload nearby tiles. Ground is
shaded using existing soil/rock textures, clipped away from water and steep faces, and excluded from
remembered/hidden terrain. It changes no terrain heights, collisions, resource budgets or save fields.
The mask is reconstructed after loading. Depleted resource entities retain their ground scars;
destroying/moving a source updates its old/new region and proximity links.

The baked models add six small Scrap silhouettes, restrained metal/rust, a large broken-frame
landmark, lower Oil seep geometry with pump equipment, and subdued mineral exposures in host rock.
See [20 engine captures](pictures/terrain-v2-layouts/implementation/resource-sites/README.md). Targets
still deplete as whole compounds; finer collision, progressive visual erosion and per-piece removal
remain separate design work.

The complete Debug suite passed **33/33** in 818.61 seconds. Six affected tests passed again after
the final canopy/cache/adapter refinements (16.86 seconds). New CPU and GPU readback checks cover
reconstruction, unchanged frames, identity order, local removal/movement, complete clearing, bounded
inputs and restoration of pixel-upload state. Final Debug and Release builds were warning-free;
the Release gallery, 80 moving-camera frames and profile run completed without OpenGL errors.

The final largest-map probe measured **8.77 ms render-only** on the GTX 1070 (2554×1389, Central Water,
seed 7, 20 chunks, 108 targets, repair 0, 100 measured frames after 20 warm-up frames). Site preparation
averaged 0.019 ms CPU; terrain, vegetation and world GPU query averages were 2.35/5.04/0.45 ms.
No measured site-tile uploads or vegetation cache rebuilds/uploads occurred. These are changed
resource/vegetation workloads, not an isolated shader comparison; simulation, UI and swap are excluded.

An initial ~87 ms measurement used AMD integrated graphics, so it cannot be compared with the earlier
GTX 1070 baseline. The game and renderer fixture now export executable-level high-performance GPU
hints and log the actual graphics device. On this machine those hints selected the GTX 1070; driver
and user settings can override them. No system settings were changed. See the performance audit.

Save format remains 1; the V2 generation revision is now 5. Start a fresh V2 match after rebuilding.

Opening inventories remain definition-backed: 600 scrap and 400 oil per player with current nodes.
Abundance calculates additional fields without subtracting starting fields or imposing V1's minimum
additional allocation. Existing field-count rounding to multiples of player count is retained and
counts can be zero; this does not pair field locations or guarantee equal expansion opportunities. Harvest-target counts and capacities are preserved; V2 maps each legacy node to a compound deposit,
with larger footprints and dense baked satellite piles. No extra opening inventory is added. V2 removes matched travel-cost
constraints and paired expansion-region placement. Starting fields retain intended-player access;
additional fields use seeded locations. Required V2 fields also require reachable land, so flying
delivery cannot place them on disconnected decorative islands. V1 retains its earlier distribution.

V2 composition is configured per field under `terrainV2Composition`: a radius range, node spacing
and a mapping from legacy node IDs to compound deposit IDs. Registry validation requires identical
resource types and capacities for each mapping. Each compound mesh contains 22–58 pieces in uneven
lobes, with larger cores and smaller satellites; satellite geometry is baked together, not submitted
as individual draw commands. Scrap, oil and uranium retain their original harvest-target counts.
Fields pack their compounds adjacently instead of independently scattering isolated nodes.

Colliders and selection bounds cover each complete deposit. Placement checks its perimeter against
terrain/water, other deposits and opening construction reserves. Trees use authored stand spacing
as canopy clearance around world objects, exposing deposits without changing navigation collisions.
Field lookup and depletion retargeting recognize both legacy and compound IDs, including after load.
Pieces currently disappear together when their owning deposit is depleted; progressive visual erosion
and individually destructible satellites remain future node/source design work.

### Revision 4 verification (2026-10-14)

The full Debug build and all 32 CTest entries passed (936.73 seconds). After the final test-harness
cleanup, the complete Debug build and five affected generation/opening/definition/renderer/architecture
checks passed again (33.15 seconds). Release game and fixture builds completed without compiler warnings.
The fixture now checks both intended shader configurations: reload/recovery in Debug, unchanged
programs with reload disabled in Release. Failure diagnostics identify the first failed check.

The final Release run produced [18 reviewed captures](pictures/terrain-v2-layouts/implementation/cliffs-and-clusters/README.md),
completed 80 moving-camera frames and emitted no OpenGL errors. The largest-map render-only probe
(seed 7, Central Water, 20 chunks, 2554×1389, GTX 1070) averaged **9.47 ms** over 100 frames after
20 warm-up frames. It included terrain, vegetation and 108 resource targets, with zero vegetation
chunk rebuilds or uploaded batches during measurement. Repair level was 0. The earlier 13.2 ms run
overlapped the CPU-heavy test suite and is not the isolated result. Timings exclude simulation, UI
and swap throttling; they do not establish full-game FPS. Use `STRATEGY_RENDER_PROFILE=1` with the
Release renderer fixture to reproduce the probe; `STRATEGY_TERRAIN_SCREENSHOTS` adds the gallery.

Resource placement remains provisional until the whole allocation succeeds. Failure first removes
obstructing peak relief, then uses a constructive lowland fallback with deterministic shore geometry
and compact node patterns. Retry layouts use distinct deterministic streams. The final template uses
fixed eligibility streams and removes incidental ridge relief. Its lake capacity is bounded by the accepted opening positions, which remain attached to the original seed.
Fallback geometry is repeatable for that seed, not required to be identical across different seeds.
Repairs can exceed ordinary source bounds and report their level. Fallback preserves the lake, resource eligibility, separation, local capacity and
access rules without silently dropping fields. Its surface is independent of seed-driven relief.
Shipped preset capacities are tested explicitly even when ordinary generation succeeds.

Format 1 stores V2 terrain recipe revision 5 and the repair level, plus complete foundation identity,
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
| Support | Exact rectangle/disk areas, corner-only disconnection, clipped edge contacts, inclusive half-support boundary, foundation depth, Land/Water/Shore contracts and unchanged surfaces |
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
are tested. Broader recipe-authoring validation remains an audit target. Per-building interaction
routes are runtime navigation concerns, not player-placement acceptance gates.


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


## Seeded landforms and rendering (2026-10-14)

The revised planner implements the accepted large-scale variation direction. Internal landform
ranges live in `assets/gameplay/terrain/v2.json`; obsolete corner inset, route-ring width and unused
peak-radius settings are removed. Opening cores start at radius 30 with the existing blend and sampling padding. The
opening capacity fixture places the 20-by-20 command hub plus a drone factory, two processors,
charging pad, generator and storage together, largest footprint first. This tests coexistence,
placement and ground exits; it does not yet simulate a full powered opening build or prove every
future unit formation's clearance.

Central Water samples its ordinary coverage without the old corner-ring radius clamp, then bounds
its full irregular outline against the map. Optional water is resolved before shaping. Starts are
selected on suitable dry capacity, with separation and finite candidate attempts. The protected
cores remain flat; their outlines are still circular rather than the proposal's eventual irregular
multi-patch construction neighbourhoods. The ridge shoulder and branching functions preserve the
fine 1.5-unit cells and half-unit height steps.

Rendering keeps the natural material and cached map backdrop from the preceding pass. Shared V2
atmospheric ranges now preserve more landscape contrast at overview scale, consistently across
terrain, water and models. V2 vegetation selection reaches that fog range rather than stopping at
the former 300-unit cutoff and leaving a bare visible horizon. V1 keeps its former selection range. The fine terrain topology and vegetation GPU cache are unchanged;
no additional per-frame terrain generation or material texture reads were introduced.

Revision 3 rejects previous V2 terrain revisions before restoring entities. Save format remains 1,
V1 remains selectable, and changing terrain during construction remains prohibited. Begin a fresh
V2 match to see this generation; already-running matches retain their generated surface.

### Verification for this update

The initial geometry/opening/render checks passed, then the expanded seven-building capacity check
exposed a greedy ordering failure. Reserving the largest building first retains all required
footprints without prescribing player build order. A visible connection strip was reduced by
retaining neighbouring natural elevations instead of assigning the entire strip its centre height.

| Final check | Evidence |
|---|---|
| Global regression | All 32 CTest entries passed in 1,030.12 seconds, including all five field/session matrices, V1 map audit, game systems, resources, navigation, persistence and rendering |
| Seed composition | Twelve Small Central Water plans repeat exactly, vary lake area and retain separated dry opening cores; different dry seeds change landmark placement and shape |
| Opening capacity | All five layouts fit the expanded joint building plan and preserve populated-world ground exits |
| Post-cleanup checks | Generation/support/save, joint openings, renderer integration, definitions and render architecture passed again (5/5, 31.91 seconds) after unused tuning-field removal and connection-grid validation |
| Final presentation | Nine terrain overviews (five layouts plus extra seeds for Central Water/Rolling Hills), six inhabited centre/edge/whole-map views and 80 motion frames passed; captures inspected; no GL errors |
| Builds | Final Debug and Release game targets compiled successfully without compiler warnings |

The later horizon-distance adjustment affects presentation only and received the final capture/motion
rerun. It does not change the generation, resource or save results covered by the global suite.
Logs and temporary probe sources remain under ignored `build/debug/audits/v2-composition*`.
The [seeded-landform engine gallery](pictures/terrain-v2-layouts/implementation/seeded-landforms/README.md)
retains the same-scale seed comparisons and inhabited views.

### Render-cost probe

On the GTX 1070 at 2554 by 1389, Central Water seed 7, 20 by 20 chunks, a Release render-only probe
averaged **9.34 ms** over 100 measured frames after 20 warm-up frames. Vegetation averaged 0.265 ms
CPU and 5.62 ms GPU, with 11,290 submitted instances and zero chunk rebuilds/instance uploads during
measurement. The extended horizon submits more vegetation than the old cutoff, so this is a changed
map/render workload rather than an isolated cache-speed comparison. Grouped persistent buffers remain
in use. The probe omits simulation, UI and swap, and synchronizes with `glFinish`; it is not a
full-game 60 FPS claim or a p95 latency result. Its temporary fixture omits unrelated cache and
Debug-only shader-reload assertions; the actual Debug integration suite covers those checks. The
measurement loop completed without GL errors, and temporary profiling changes were removed.

Remaining proposal work includes truly irregular opening envelopes, terrain-aware ranking of
opening candidates rather than protected capacity sites, localized transactional resource repair
(the current escalation still removes broad landmark relief before fallback), supported clearing,
oceans/river networks and island transport. The current unit definitions provide a worker and flying
construction drone; competitive army behaviour needs later combat-unit and player testing.


### Placement angle/height follow-up (2026-10-23)

The bounded candidate search now ranks directions by residual surface relief before accepting a
fit. On a genuinely planar slope it uses angle alone. Small local bumps inside the contact-height
tolerance no longer bypass ground adjustment: the first cut target uses area-weighted ground
height, followed by the existing median and raised-intercept alternatives when required by the
unchanged cut/support limits. A valid uncut plan remains a fallback. Preview cost/collision
rejections retain their geometric pose on explored terrain. Render regression tests compare
actual ghost and committed depth and verify a visible tilt relative to a horizontal mesh.
See [captures](pictures/building-placement/angle-height-2026-10-23/README.md).


### Increased placement tilt (2026-10-23)

The live building catalogue now allows ten degrees of total tilt. The definition validator,
V2 candidate clamps and legacy foundation clamp share the same ten-degree hard limit.
Lower per-profile limits remain supported, including the original five-degree regression fixtures.
A live-catalogue probe and an actual ghost-versus-placed render probe cover an approximately
9.46-degree slope with angle-only placement. Water and excavation rules are unchanged.

Validation of the increased limit: all four targeted Release checks passed (placement planning,
building placement, terrain V2 and renderer integration, 20.83 seconds). The actual rendered
slope was 9.46232 degrees, with 32,561 pixels differing from a horizontal pose, and preview /
placed depths matching. Debug and Release game builds completed successfully.


### Continuous hillside foundations and half support (2026-10-23)

Land now requires at least 50% connected dry bearing, with exact equality allowed. Bearing
extends down continuous ground within the foundation depth; floor contact remains a separate
shallow band. Canonical cliff faces split bearing patches, and the selected patch must include
positive-area floor contact, preventing a disconnected lower shelf from inflating support.
Small steep ramp triangles no longer suppress the overall hillside fit. Base/scaffold selection
and overlay geometry share these quantities. Total tilt remains capped at ten degrees; cut,
water, occupied-foundation protection and depth limits retain their previous rules.

A generated Continental seed-1 scan improved from 19/209 terrain-valid sites with no angled
placements to 178/209 valid with 135 tilted by more than one degree. The representative
(-64, -160) site fits at about 4.1 degrees; its real ghost and committed render depths match.
Exact half thresholds cover rectangles and analytic disks, with disconnected lower-shelf,
rough-hill contact/bearing, low-budget repair and depth regressions. Full Release validation
passed all 36 tests; final search pruning passed the four affected checks and three Debug
placement checks. Both game builds are updated. Large previews still have occasional sub-60-FPS
frames (about 18.2 ms p95 for a moving hub cut in the measured scene).
See [captures](pictures/building-placement/hillside-support-2026-10-23/README.md).


### Bounded cut-and-fill pads (2026-10-23)

Accepted grading records now include `levelingMaximumFill`. The same per-vertex prediction
function supplies the temporary GPU raster and committed terrain; both positive and negative
updates invalidate affected terrain, navigation and render-cache regions. Remaining operations
replay from base heights in stable source-ID order with strongest-displacement composition.
Fill limits participate in terrain protection revisions, renderer preview/foundation cache keys,
persistence validation and authoritative checksums. Progress changes do not reapply earthwork.

`terrain_grading` covers actual production limits, a shallow depression with cut and fill,
no raised metal frame, preview/commit identity, cancellation, deterministic overlap replay,
protected neighbours, water, tight budgets, shorter mixed foundations, cache invalidation and
save/load, including the current ten-degree grading limit. Renderer integration compares
original/preview/committed/restored depth on a building pad and asserts that no scaffold mesh exists.
Full Release validation passed all 37 tests; all four affected Debug checks also passed.
See the [grading captures](pictures/building-placement/grading-2026-10-23/README.md).

Format remains 1, terrain generation revision remains 7. Older development saves without the new
fill field are intentionally not migrated; start a new match. User runtime setup/saves were not
rewritten or deleted by this work.
