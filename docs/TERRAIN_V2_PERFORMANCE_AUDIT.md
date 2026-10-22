# V2 rendering performance investigation

Date: 2026-10-11. Baseline: `f2d1e8b` (`RENDER v2`).

The first optimization should be caching vegetation transforms and GPU instance data, followed by
camera-frustum culling and vegetation LOD. The finer terrain does not need to become blocky again
to recover frame time. Debug CPU overhead is substantial; Release must be the performance baseline.
The initial investigation retained no production optimization. The follow-up below implements
the recommended vegetation cache.

## Implemented follow-up: destruction-aware vegetation caching

The subsequent implementation uses stable, deterministic instance IDs and per-chunk revisions.
`removeInstance` and `clearWithin` increment revisions only for affected chunks. Source chunks
are exposed read-only so callers cannot bypass invalidation. Field copies/replacements receive
distinct transient cache identities; these identities do not enter simulation checksums or saves.

`VegetationRenderCache` owns one persistent transform buffer per presentation. Each chunk has a
reserved range within that buffer. Destruction compacts and uploads only affected chunk ranges;
the remaining allocation stays reserved until field replacement. Empty ranges are omitted from
draws. Current mutation APIs only remove instances; adding vegetation later must explicitly
rebuild the field identity or implement range growth/relocation.

Visible chunk ranges are grouped into indirect draws per model mesh. This avoids the CPU cost of
drawing each cached chunk separately. The small indirect command lists are streamed each frame;
unchanged instance matrices are not. This is CPU-prepared indirect drawing, not GPU culling.
Existing distance selection and appearance are preserved.

Model readiness/base-height changes rebuild the corresponding cached chunks. Staging/regenerating
terrain clears the cache; V1 foundation deformation invalidates intersecting chunk bounds, including
the extra terrain samples used for tilt. Future gameplay destruction and its persistence remain
separate from this presentation cache.

Tests cover stable IDs after removal, selective/no-op revisions, field replacement, matrix contents
read back from GPU buffers, asset completion, selective terrain invalidation, unchanged neighboring
ranges, empty ranges, cache lifetime and repeated frames without instance uploads. Inhabited V2
captures and continuous camera movement were also exercised.

At 2554x1389, seed-7 Central Water on 20x20 chunks in Release, the final grouped-cache probe averaged
**0.237 ms vegetation CPU**, **4.93 ms vegetation GPU elapsed**, and **8.34 ms world rendering with
GPU completion**. It submitted the same 9,987 distance-selected decorative instances and recorded
zero chunk rebuilds/instance uploads across 100 measured frames after 20 warm-up frames. The earlier
baseline was 3.32 ms CPU / 4.91 ms GPU / 11.20 ms total under the same scene/camera conditions, with
40 measured frames. These are separate local runs, not a full-game FPS guarantee. Logs are in
`build/debug/audits/vegetation-cache/grouped-release-large-profile.log`. The Release fixture's
Debug-only hot-reload assertions still cause exit code 1; the measurement is not a Release test pass.

A preliminary cache using separate draws for each chunk measured 4.98 ms vegetation CPU on this
large scene, so that approach was replaced. The final path preserves localized updates while
keeping model/material draw submission grouped.

Final validation: the complete Debug build and Release game build succeeded without compiler
warnings. All **32 Debug CTest checks passed** in 832.88 seconds, including V1/V2 generation,
game systems, terrain queries, resource delivery, drone reliability, persistence, vegetation,
renderer integration and rendering architecture. The final inhabited captures and 80 camera-motion
frames passed without GL errors. Temporary profiling hooks were removed; raw measurements and
captures remain under ignored `build/debug/audits/vegetation-cache`.

## Measurement method and limits

The renderer integration fixture was temporarily instrumented with CPU clocks and OpenGL
`GL_TIME_ELAPSED` queries. Each case ran 60 frames, discarded 20 warm-up frames, and averaged the
remaining 40. GPU completion was enforced between frames. The instrumented sources and raw logs
are retained locally under ignored `build/debug/audits/fps-investigation`; tracked sources were
restored afterward.

Hardware: NVIDIA GeForce GTX 1070, driver 582.28. Scene: seed 7, Central Water, opposite starting
bases and generated resources/vegetation. Resolutions: 1280x720 and 2554x1389. Cameras: lake-center
play view, outer-edge play view, whole-map overview. Both 10x10 and 20x20 chunk maps were sampled in
Release; Debug and a cache experiment were sampled on 10x10. The clean Debug run is
`debug-uncontended.log`; the earlier `debug.log` overlapped compilation and is not a baseline.

These are world-rendering probes in a hidden SDL window, without simulation updates, gameplay UI,
exploration uploads, particles, swap/vsync or a populated late-game battlefield in the measured
render loop. They demonstrate costs and headroom, **not verified full-game FPS**. CPU submission
and GPU elapsed timings overlap and must not be added. Forced GPU completion affects throughput.
Frame averages do not establish p95/p99 frame pacing.

The Release fixture finishes with exit code 1 because its shader hot-reload assertions expect a
Debug-only feature (`STRATEGY_SHADER_HOT_RELOAD`). The profiling section completes; this is not a
passing Release integration-test result. Debug probes finish successfully, with no reported GL
errors. Initial asset/shader warm-up and screenshot readbacks are excluded from reported timings.

## Results

All rows below use the normal terrain/water/vegetation/world passes at 2554x1389. CPU columns refer
to the vegetation pass; GPU columns refer to OpenGL elapsed queries.

| Build / scene | World render, including GPU completion | Vegetation CPU | Vegetation GPU elapsed | Terrain + water GPU elapsed |
| --- | ---: | ---: | ---: | ---: |
| Debug, 10x10, lake-center | 21.68 ms | 14.71 ms | 2.74 ms | 2.28 ms |
| Release, 10x10, lake-center | 4.25 ms | 0.90 ms | 1.49 ms | 1.26 ms |
| Release, 20x20, lake-center | 11.20 ms | 3.32 ms | 4.91 ms | 1.94 ms |
| Release, 20x20, outer edge | 8.09 ms | 2.40 ms | 3.32 ms | 1.37 ms |
| Debug, 10x10, temporary cached batches, lake-center | 4.88 ms | 0.14 ms | 1.62 ms | 1.32 ms |

The small map contains 2,482 decorative instances, including 1,274 trees. The large map contains
10,857 decorative instances, including 5,875 trees. These are map totals, not visible counts.
The broadleaf source models have 552 and 384 triangles respectively, so replacing every model
is not the first intervention.

The temporary cache stored the existing grounded transform batches once and reused them; it
still uploaded instance matrices through the current model draw path. It demonstrates the CPU
cost of repeated preparation, not a production cache implementation. For 20x20 Release at the
lake center, the same experiment reduces vegetation preparation to about 0.20 ms and retains
the roughly 5 ms GPU cost. Only the first fixed camera is a valid direct comparison: the prototype
deliberately freezes distance selection, so later camera results cannot establish correct culling.

A separate 120-tick idle-opening probe, excluding the first 20 ticks, measured 0.0755 ms mean and
0.1135 ms maximum per tick on 20x20 Release. Small-map Debug measured 0.678 ms mean / 1.154 ms
maximum. This does not cover moving armies, pathfinding congestion or a developed economy.

## Implementation priorities

1. **Cache static vegetation preparation and GPU instance buffers.**
   `Renderer::drawVegetation` currently traverses instances, resolves models/definitions, queries
   terrain and rebuilds matrices every frame. `Model::drawInstanced` then calls `glBufferData` for
   each mesh, repeating the same transform upload across a model's meshes. Introduce renderer-owned
   per-chunk/per-presentation batches, shared instance data across meshes, typed model handles and
   explicit revision tracking. Rebuild on new maps, vegetation removal, relevant terrain changes,
   asset readiness and model reloads. Account for marker-model base heights changing when an asset
   becomes ready. Preserve presentation-only ownership and deterministic scatter.

2. **Cull vegetation against the camera and introduce detail levels.**
   The current pass only rejects chunks beyond 300 units horizontally from the camera. It has no
   frustum check and no vegetation LOD, and fog does not prevent GPU submission. Use conservative
   chunk bounds including scaled model height/extent, then finer checks where worthwhile. Keep
   full near trees; use simpler tree meshes or impostors at distance and remove subpixel ground
   cover through stable screen-size thresholds/fades. Preserve forest coverage and silhouettes.
   The large-map overview currently becomes cheaper partly because its camera position excludes
   much of the vegetation through this distance limit; that is not evidence of robust overview LOD.

3. **Build water-specific index buffers and skip dry chunks.**
   `drawWater` currently submits every visible terrain chunk's full-resolution indices, including
   dry layouts, and lets the fragment shader discard dry pixels. Retain triangles intersecting
   water (not only triangles whose vertices are all wet), skip entirely dry chunks, and preserve
   generated-versus-final water diagnostics. Keep fine shoreline topology; consider coarser lake
   interiors only after seam checks. Disabling the entire water pass saves roughly 0.27-0.38 ms of
   GPU elapsed on small-map play views, and 0.39-0.75 ms on large-map play views at 2554x1389.
   Sparse water will recover some of this cost, not all of it.

4. **Simplify terrain geometry without changing authoritative terrain.**
   A chunk has 64x64 quads: 8,192 triangles. The 20x20 terrain can submit up to 3,276,800 triangles
   per pass before frustum rejection, and water repeats that grid. V2 forces LOD0 to avoid cracks.
   Tighten terrain chunk bounds using actual height ranges first: current bounds use the global
   terrain height scale. Then investigate adaptive flat-interior simplification with pinned chunk,
   cliff, ramp and shoreline boundaries. Preserve positions, interpolated attributes, foundations
   and the rendered surface's agreement with simulation queries. Simply restoring the old coarse
   per-chunk LOD would risk the grey edge flashes and moving cracks already encountered.

5. **Optimize shader/state work only after the preceding measurements.**
   Terrain samples four materials twice each, even where one dominates. Foundations also compute
   the initial terrain samples before overriding them. Material-specialized paths and a cheaper
   distant detail path are candidates. Separate back-face-culling policy for closed tree meshes
   from two-sided ground-cover cards. Measure gains before accepting extra shader complexity;
   current evidence does not identify terrain shading as the dominant bottleneck.

## Acceptance for a 60 FPS claim

Use a persistent optimized-build benchmark with asynchronous GPU queries, warm-up, CPU/pass timings,
submitted instances/triangles/draw calls, and end-to-end frame times including swap. The existing
`FrameProfiler` measures CPU scopes and ends before `SDL_GL_SwapWindow`; its `render.terrain` scope
includes water. It cannot independently attribute GPU time or explain missed vsync deadlines.
The application requests swap interval 1, so missed display deadlines can magnify an observed drop.

Verify 10x10, 15x15 and 20x20 maps across dry and wet layouts, several seeds, startup and developed
economies, ground-level/play/overview cameras, movement, building placement, vegetation clearing,
save/load and model reloads. Measure at the player's normal resolution. Target p95 end-to-end
frame time at or below 16.67 ms for a stated hardware/workload envelope, and report p99 plus spikes.
Leave headroom for gameplay and UI rather than consuming the whole budget in rendering.

Compare captures and continuous camera motion after every geometry/culling change: no disappearing
forest groups, shoreline crawling, cracks, foundation drift or restored blockiness. Preserve V1,
terrain semantics, resource distribution and support rules. Cache lifetime/invalidation and
conservative culling need focused tests; numerical generation audits need rerunning only if those
authoritative paths change.


## Subsequent seeded-landform render probe (2026-10-14)

The new V2 composition and farther overview haze change the visible workload. Vegetation now renders
out to the V2 fog range instead of the former fixed 300-unit cutoff. A largest-map Release probe
at 2554 by 1389 averaged 9.34 ms render-only, with 0.265 ms vegetation CPU, 5.62 ms vegetation GPU,
11,290 submitted instances and no measured cached-instance uploads/rebuilds. This is not an isolated
comparison to the earlier map or a full-game FPS measurement. See
[the current implementation report](TERRAIN_GENERATION_V2_IMPLEMENTATION.md#render-cost-probe) for
method, caveats and verification. The destruction-aware cache and grouped GPU buffers remain in use.

## Connected cliffs and compound deposits (revision 4, 2026-10-14)

The final Release probe averaged **9.47 ms** at 2554×1389 on the largest map (Central Water, seed 7,
20 chunks), with terrain repair level 0 and 108 resource targets. It measured 100 completed frames
after 20 warm-up frames using `glFinish`, after other tests finished. Vegetation recorded no chunk
rebuilds or instance-buffer uploads in those measured frames. An earlier concurrent-test run measured
13.2 ms; it is superseded by the isolated measurement. This remains render-only, excluding simulation,
UI and swap throttling.

Compound deposits bake 22�58 pieces into joined meshes, keeping satellite count out of draw-command
count. Repeated source geometry is reduced before duplication; every baked deposit stays below
15,000 triangles. Inventory/target counts remain unchanged. No per-frame satellite-transform work
or extra vegetation-cache invalidation was introduced. The full 32-test suite and final Release
capture/profile run passed. Run the renderer fixture with `STRATEGY_RENDER_PROFILE=1` to reproduce.

## Resource sites and adapter selection (revision 5, 2026-10-21)

The latest largest-map Release run averaged **8.77 ms** on the **NVIDIA GeForce GTX 1070**, at
2554×1389 with Central Water seed 7, 20 chunks, 108 resource targets and repair level 0. It measures
100 frames after 20 warm-up frames. Site preparation averaged 0.019 ms CPU; GPU query averages were
2.35 ms terrain, 5.04 ms vegetation and 0.45 ms world. There were no site-tile uploads or vegetation
cache rebuilds/uploads in the measured frames. An earlier matching-adapter repeat measured 8.91 ms.
This remains render-only; it does not establish full-game FPS or a direct per-feature speedup against
revision 4, whose resource positions and vegetation workload differ.

An initial ~87 ms result ran on **AMD Radeon integrated graphics**. Pass queries attributed about
35 ms to terrain and 43 ms to vegetation; the static site preparation was only about 0.036 ms CPU
and performed no tile uploads. It was an adapter mismatch, not comparable evidence of a GTX 1070
regression. Always record `V2_RENDER_DEVICE` alongside `V2_RENDER_PROFILE` and `V2_RENDER_PASSES`.

Both rendering executables now export the hybrid-GPU preferences documented by
[NVIDIA](https://developer.download.nvidia.com/devzone/devcenter/gamegraphics/files/OptimusRenderingPolicies.pdf)
and [AMD](https://gpuopen.com/learn/amdpowerxpressrequesthighperformance/). The exports belong in the
executable, not a library or launcher. They selected the GTX 1070 here, but driver/user policies can
override them. The normal renderer log also identifies the selected device. No global or per-app
system settings were modified.

Site appearance uses one cached RGBA mask, shared CPU density logic for canopy clearance and local
texture updates when sources move/disappear. CPU tests compare local updates with full reconstruction;
GPU readback checks cover partial texture writes and pixel-store restoration. All 33 Debug tests,
six final affected checks, 20 Release captures and 80 moving-camera frames passed. Existing inventories
and harvest-target counts are unchanged; whole-compound depletion remains in place.

Compound deposits bake 22–58 pieces into joined meshes, keeping satellite count out of draw-command
count. Repeated source geometry is reduced before duplication; every baked deposit stays below
15,000 triangles. Inventory/target counts remain unchanged. No per-frame satellite-transform work
or extra vegetation-cache invalidation was introduced. The full 32-test suite and final Release
capture/profile run passed. Run the renderer fixture with `STRATEGY_RENDER_PROFILE=1` to reproduce.
