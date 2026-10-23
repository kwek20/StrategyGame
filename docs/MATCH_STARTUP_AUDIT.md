# Match entry after “click to start”

Investigated 2026-10-22. The original measurements below describe the previous behavior.
The presentation-preparation implementation is recorded at the end of this document.

The loading screen currently announces readiness before preparing the first gameplay frame. The
largest remaining job is rasterizing the resource ground-presentation texture: scrap/uranium earth,
cracked oil ground, connections between nodes and compact oil wetness. Imported models and generated
world data are already loaded; this derived presentation cache is still empty.

## Evidence

The running game was `build/debug/Debug/strategy_game.exe`. The audit used the saved setup without
modifying it: V2 Continental, seed 1, 15 chunks per side, normal saved player countries, starting
resources scale 2 and abundance 1.5. The resulting world contained 958 entities and 6,929 vegetation
instances. Timings use a hidden 1280×720 OpenGL window on the GTX 1070, with explicit GPU completion
between measured rendering stages. The user's game remained running, so these are indicative local
timings rather than isolated benchmarks. Driver/disk caches were not purged.

| Work after confirmation | Debug | Release |
| --- | ---: | ---: |
| Resource ground texture, initially empty | 3,274–4,566 ms across two runs | 463–469 ms |
| Same resource texture, unchanged next frame | 1.5–2.9 ms | 0.18–0.22 ms |
| Vegetation cache initialization and draw | 73–93 ms | 12–21 ms |
| Resource instance cache/world draw | 20–22 ms | 2–3 ms |
| Construct PlayState with the prepared session | <1 ms | <1 ms |
| Simulation update with the maximum 0.25-second input | 34–39 ms | 1.7–1.8 ms |
| Complete cold PlayState render including HUD, measured separately | 5,040 ms | 716 ms |
| Complete second PlayState render | 27 ms | 7.4 ms |

The complete render is an independent measurement after clearing presentation caches; do not add
it to the component rows. The previously running game log records a 3,606 ms slow frame at
13:30:51. The reported seven seconds was not reproduced exactly; the measurements establish a
multi-second first-frame stall, dominated by resource ground painting in Debug, rather than account
for every second of that observation. Confirmation also has an intentional 0.28-second fade.

World generation and terrain/model uploads were timed separately and completed before confirmation.
In the latest run, generation took 15.7 seconds in Debug / 1.70 seconds in Release. These are not
post-click work. Asset streaming ran concurrently with generation, matching the application path.

## Call path

1. `src/app/Application.cpp:381` stages generated terrain, waits for terrain uploads and match asset
   completion, and then displays `loading.click_to_start` at line 411.
2. Confirmation fades the loading screen for 0.28 seconds, then constructs PlayState from the
   already generated GameSession at line 418. This constructor does not regenerate the map.
3. `src/game/PlayState.cpp:1595` calls `prepareResourceSites` before its first terrain draw. This
   reaches `Renderer::prepareResourceSites`, `ResourceSiteTexture::update`, and
   `ResourceSiteMap::update`. Cache identity was cleared during terrain staging; the full texture
   must be built before the first gameplay image can finish.
4. Vegetation and resource-node presentation/instance buffers are also initialized on first use.
   The gameplay HUD is created and populated during rendering, including its RML/image work.
   These contribute additional entry work but are substantially smaller in the component timings.

## Why resource painting is expensive

`src/world/ResourceSiteMap.cpp` sorts sources, builds individual brushes and same-field links, marks
the full map dirty on initialization, and rasterizes local brush lists into an RGBA texture. For
this map the resolution is 480×480. Source pairing scans all pairs before filtering. For each
pixel/brush sample, `sample()` repeats two noise evaluations that depend only on the pixel position.
It also evaluates brushes which eventually contribute zero. The existing cache makes unchanged
frames cheap, but does not help the first rasterization.

The CPU painting occurs synchronously on the rendering thread. The loading screen's match-asset
completion counter does not include it, nor does it include render-cache/HUD preparation or the
driver's first-use shader variants. Current `render.total` can catch the stall, but resource painting
has no dedicated profiler scope because it runs before `drawTerrain`'s terrain scope.

## Recommended changes, in order

1. Make “ready” mean ready to present gameplay: prepare the resource-site texture, starting-camera
   vegetation/resource buffers, HUD and first-use shader state while the loading screen remains
   visible. Prefer a reusable match-presentation preparation stage, rather than special cases in
   each draw function. Do not advance simulation or consume input while preparing. Check GPU
   completion/readiness before offering confirmation. This moves necessary work before the click;
   it does not itself reduce total preparation time.
2. Reduce rasterization cost: compute shared noise once per pixel, reject samples outside a
   conservative brush bound, and index nearby nodes by field/spatial bucket instead of scanning
   every source pair. Preserve source ordering and exact image semantics; compare output pixels
   or review any intentional difference. Keep incremental tile invalidation and depletion scars.
3. Separate CPU map preparation from GL upload so initial painting can run on an owned immutable
   snapshot in the loading worker. Upload on the GL thread; retain cancellation and generation
   identity checks so an abandoned map cannot overwrite a newer one. Avoid introducing this
   complexity before measuring the simpler rasterization improvements.
4. Add explicit loading-stage and resource-paint timings. Reset the main frame clock after loading:
   it currently includes generation and confirmation waiting time. The simulation clamps this to
   0.25 seconds, so it is not a seven-second catch-up loop, but the first delta also affects camera
   and entry-fade logic and should not include time spent waiting at a loading screen.

Verify all supported sizes and abundance levels in Debug and Release, first and subsequent matches,
save/load, cancellation, different driver-cache states and the actual starting view. Target no
seconds-long preparation after confirmation; measure confirmation-to-first-present separately from
the deliberate fade and total loading duration. Confirm that prewarming does not leak fog information,
change resources/terrain, or invalidate vegetation/resource destruction caches.

## Reproduction artifacts

The standalone audit harness and generated project live under ignored build output:

- `build/debug/audits/MatchStartupAudit.cpp`
- `build/debug/match_startup_audit.vcxproj`
- `build/debug/audits/match-startup-debug.log`
- `build/debug/audits/match-startup-release.log`

It links the existing game libraries, uses the saved setup read-only, times individual renderer
operations, then resets presentation caches and measures actual PlayState rendering with the HUD.
It does not interact with the running game or write its configuration. No production source, test
registration or gameplay behavior was changed to collect these timings.


## Implemented: prepare presentation before confirmation

New-match entry and saved-game entry from the main menu now construct the actual PlayState before
confirmation and retain that same object through activation. StateStack accepts ownership of an
already prepared state. There is no reconstruction, terrain restaging or cache reset after the click.
The main frame/FPS clocks reset after the loading transition, excluding generation and confirmation
waiting from the first simulation delta.

PlayState has an explicit presentation-only preparation sequence:

1. Stage its authoritative terrain/foundations and finish bounded terrain uploads.
2. Paint the resource-site map incrementally, then upload the completed texture on the GL thread.
3. Prepare vegetation and resource-node render buffers for the actual starting view and fog state.
4. Construct/populate/render the actual gameplay HUD, retaining its document and images.
5. Render the complete starting view through the normal gameplay rendering path.
6. Insert and poll a nonblocking GPU fence before reporting readiness.

No simulation update or gameplay input is dispatched during these steps. The loading screen is
drawn over preparation work before presentation. UI screen activation keeps the loading document
in front while preserving the prepared HUD underneath. Confirmation remains unavailable until the
GPU fence completes. The existing loading/entry fades remain intentional transitions.

ResourceSiteMap now supports begin/advance preparation using the original painter. Each unit of
work is a 32×32 tile. ResourceSiteTexture batches up to 16 tiles or approximately six milliseconds
per loading frame, yielding between tiles. This is a scheduling target, not a hard duration limit:
a dense individual tile or a first-use cache/shader operation can take longer. Pair construction,
noise evaluation, brush semantics, resolution and resource-field appearance are unchanged. The initial preparation pass
left this arithmetic unchanged; the subsequent optimization below removes that repeated work.

Escape discards pending tile work/uploads/fences; a subsequent match starts from a fresh identity.
Preparation has no detached jobs capable of writing into a newer map. Saved-world reconstruction
runs on the loading worker, with cancellation propagated through terrain, navigation and vegetation
reconstruction; OpenGL and HUD work remain on the render thread. The in-match quick-load shortcut
is not a match-entry screen and retains its existing behavior.

Verification covers exact pixel parity between synchronous and sliced painting, interrupted/replaced
jobs, GPU texture parity, ownership-preserving state activation, V1 and V2 entry, saved-game worker
restoration/cancellation, authoritative checksums remaining unchanged during preparation, and a
first gameplay frame with zero resource-site, vegetation or resource-node cache uploads. Actual
loading and readiness screens were captured and visually checked; the HUD stays hidden while loading.

Current build/test and measurement logs use `build/debug/audits/startup-*`. The original audit harness
also measures `presentation_preparation_before_confirmation`, `prepared_first_gameplay_frame` and
first-frame upload counters. It reads the user's saved setup without modifying it.


### Follow-up measurements

The hidden-window harness remeasured the saved V2 Continental setup (15 chunks, seed 1, starting
resources scale 2, abundance 1.5; 958 entities and 6,929 vegetation instances) and a 20-chunk,
abundance-2 case (2,236 entities, 13,334 vegetation instances). These are single-run local timings
at 1280×720 on the GTX 1070 with existing driver/disk caches, no application VSync pacing and GPU
completion around measured renders. They exclude the deliberate confirmation/entry fades and do
not claim end-to-end click latency. The cold comparison intentionally bypasses preparation.

| Case | Cold gameplay render without preparation | Prepared first gameplay render | Preparation before prompt |
| --- | ---: | ---: | ---: |
| Saved setup, Release | 566 ms | 8.23 ms | 729 ms |
| Saved setup, Debug | 3,607 ms | 15.48 ms | 5,217 ms |
| 20 chunks / abundance 2, Release | 1,182 ms | 44.91 ms | 1,667 ms |

All three prepared first frames uploaded zero resource-site tiles, zero vegetation batches and
zero resource-node batches. Longest individual preparation steps measured 13.7 ms, 72.0 ms and
16.0 ms respectively. Preparation scheduling removes the seconds-long post-confirmation stall;
it does not make painting itself faster or establish a sustained 60-FPS result on the largest map.

Validation: all 35 Release tests passed (133.98 seconds), with the updated renderer executable
rebuilt before its full-suite run. Focused Release state/resource/renderer checks also passed
(3/3, 14.22 seconds); the same Debug checks passed (3/3, 85.53 seconds). Both game configurations
built successfully. Sliced CPU painting and GPU upload matched the synchronous reference pixels.
Detailed benchmark logs: `startup-prepared-release.log`, `startup-prepared-debug.log` and
`startup-prepared-largest-release.log` under `build/debug/audits/`.


## Implemented: resource-paint optimization

ResourceSiteMap now computes the two shared noise samples at most once per pixel, only when a
brush could contribute. Cached conservative world-space brush bounds and a normalized-distance
bound reject zero-contribution samples before noise/square-root work. Noise can expand the ellipse
by at most 0.265; a 1.3 reach preserves the entire original irregular edge.

Link construction uses spatial buckets keyed by field, resource family and position, with separate
six-unit field and 22-unit legacy/unassigned search scales. Candidate indices are sorted to retain
the original source-ID pair order. An extra bucket ring deliberately covers floating-point cases
where subtraction rounds a slightly over-limit separation down to the accepted exact limit. The
original distance/radius eligibility calculation still makes the final decision.

Texture resolution, channel encoding, appearance, local dirty-tile marking, exhausted-node scars,
sliced loading and unchanged-world cache reuse are preserved. This also speeds the same CPU mask
used to keep vegetation away from resources; it does not change that mask or GPU draw cost.

Tests keep a frozen, independent copy of the painter from immediately before this optimization in
`tests/reference/ResourceSiteMapReference.*`. Seeded overlapping-field cases and targeted boundaries
compare initial, incremental and sliced output byte for byte, including negative bucket boundaries,
exact six-/22-unit links, coincident nodes, tiny/maximum-size deposits, family/field edits, movement
and node removal. A near-zero/exact-link case failed with a nine-bucket search before the extra ring
was added, confirming that the regression fixture exercises the rounding issue.

An opt-in CPU benchmark runs through `STRATEGY_RESOURCE_PAINT_BENCHMARK=1` with
`resource_site_tests.exe`. It compares the frozen reference and production implementation on the
same 768 nodes in 64 fields, a 480×480 texture, and one-node removal; outputs must match on every
run. Reported times are medians of three runs rather than timing assertions.


### Painting optimization measurements

| CPU benchmark, median of 3 | Original | Optimized |
| --- | ---: | ---: |
| Initial paint, Release | 181.74 ms | 10.48 ms |
| One-node removal repaint, Release | 12.50 ms | 1.51 ms |
| Initial paint, Debug | 1,261.39 ms | 123.00 ms |
| One-node removal repaint, Debug | 90.12 ms | 22.77 ms |

The existing hidden-window match audit was then rerun using the same saved setup and the same
20-chunk / abundance-2 override as above. These before/after values are individual runs rather
than medians, with the same timing limitations as the previous audit:

| Initial resource texture, CPU paint + GL upload/completion | Before | After |
| --- | ---: | ---: |
| Saved setup, Release | 460.44 ms | 24.07 ms |
| Saved setup, Debug | 3,222.45 ms | 287.13 ms |
| 20 chunks / abundance 2, Release | 1,053.31 ms | 69.12 ms |

Presentation preparation before the prompt measured 260 ms Release / 2,318 ms Debug on the saved
setup (previously 729 / 5,217 ms), and 442 ms on the larger Release case (previously 1,667 ms).
Entity/vegetation counts stayed 958/6,929 and 2,236/13,334 respectively. Prepared first frames
continued to upload zero resource-site tiles, vegetation batches and resource-node batches.

This reduces loading work and CPU spikes when source edits require repainting. Unchanged fields
already reuse their texture; GPU shader cost and steady-state FPS are not improved by this change.
Exhausted deposits retain their existing ground scars and do not automatically trigger painting.

Validation: final Release resource-pixel, renderer, vegetation and resource-field tests passed
(4/4, 14.19 seconds); final Debug resource-pixel and renderer tests passed (2/2, 85.40 seconds).
Release and Debug game builds succeeded without new compiler warnings. Logs are
`paint-benchmark-{release,debug}.log`, `paint-final-{release,debug}-tests.log` and
`paint-match-{release,debug,largest-release}.log` under `build/debug/audits/`.
