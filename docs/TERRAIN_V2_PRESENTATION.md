# V2 natural-material presentation pass

Implemented 2026-10-11. This is a presentation pass over the existing V2 layout and
terrain; it does not regenerate elevations, change resource distribution or alter
building support, navigation, exploration or save data. V1 remains selectable.

## Ground and water

V2 previously replaced material RGB with a bright procedural meadow palette,
retaining only a narrow range of texture luminance. It now uses the existing grass,
soil, rock and dry-ground textures directly, with restrained desaturation, broad
colour variation and scattered soil exposure. Tile rotation and two-scale blending
remain in use to reduce visible repetition. No additional texture samples or assets
were introduced.

Steep exposed faces favour stone, with vertical projection to avoid stretched cliff
textures. Gentle terraces retain their grass instead of becoming bright grey stripes.
Directional lighting has less ambient fill, and V2 material lighting is encoded for
the display framebuffer after sampling sRGB textures. This is local to V2 ground;
it is not a global colour-management change to models, foundations or UI.

V2 water uses a less saturated blue-green palette to fit the ground. Existing depth,
shoreline, flow, foam and exploration behaviour is retained.

## Map boundaries

`TerrainBackdrop` is a renderer-owned, cached mesh extending 96 units beyond the
active square. Its inner ring matches the terrain raster positions and normals.
Outer rings smooth the boundary relief into low hills and fade to the background
colour between 8 and 72 units outside the playable square. The former blue void is
replaced by matching atmospheric grey in V2.

The backdrop samples only the active boundary, never inactive terrain from a previous
map. It receives the bordering exploration texel, so unexplored land remains hidden.
The ordinary terrain shader still clips inactive terrain. Debug views omit the
backdrop to show the actual map extent. Switching to V1 disables its geometry.

This is visual context, not an extension of the playable map. The active square and
all placement/movement bounds remain authoritative. The current supported V2 layouts
have dry outer boundaries; future layouts with water reaching the edge will need a
matching water backdrop rather than this dry continuation.

Boundary samples and GPU buffers rebuild only when terrain is staged or regenerated.
There is one additional draw per normal terrain pass, no new vegetation, and no
per-frame mesh generation. On the largest map it contains 46,080 vertices and 81,920
triangles, compared with up to 3,276,800 triangles in the terrain pass before culling.

## Verification and cost

- Renderer integration and render architecture passed (2/2, 45.01 seconds).
- New framebuffer checks verify land continuity beyond all four edges, hidden
  exploration there, and removal of stale V2 backdrop geometry after switching to V1.
- Five layout overviews, six inhabited Central Water views and 80 camera-motion frames
  completed without OpenGL errors. Centre, four edges and overview captures were inspected.
- Debug and Release game builds passed. Authoritative generation files are unchanged;
  the expensive resource/navigation matrices were not repeated for this presentation pass.
- The same largest-map Release render probe used for the vegetation audit measured
  8.52 ms per frame versus 8.34 ms before this pass (about 0.18 ms extra). Vegetation
  CPU time remained 0.24 ms, with 9,987 submitted instances and zero cached-instance
  uploads or chunk rebuilds during the measured frames. This probe excludes gameplay,
  UI and swap, and is not evidence of full-game 60 FPS.

The timing probe used the prior temporary profiling fixture, which exited 1 because
its assertion checks did not all pass (including its cache fixture and Debug-only
shader reload expectations). Its measured render loop completed without GL errors.
The current Debug integration fixture, including the cache checks and new boundary
checks, passed. Timing results are measurements, not a Release integration-test pass.

Final logs and temporary profiling sources are under ignored
`build/debug/audits/natural-terrain/`. Current engine captures are in
[the natural-material gallery](pictures/terrain-v2-layouts/implementation/natural-materials/README.md).

## Remaining art limitations

This improves ground and map-edge presentation; it is not photorealism. The existing
low-poly trees, building placeholders, regular terrace silhouettes and lack of cast
terrain/vegetation shadows remain visible. Higher-quality vegetation and measured
shadow rendering would be separate follow-ups, with explicit performance budgets.
