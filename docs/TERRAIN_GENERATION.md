# Terrain and World Generation

## Purpose

The world generator should create maps that feel discovered rather than stamped out. A seed must
produce recognizable regions, resource-rich and resource-poor areas, natural barriers, water,
buildable spaces, and decorative detail. Maps should remain fair enough for a deterministic 1v1
match without making both halves visibly mirrored.

"Minecraft-level random generation" means a layered, data-driven pipeline in which large-scale
regions influence terrain, resources, traversal, and presentation. It does not require voxel
terrain. The current heightfield and chunk renderer remain suitable if the generation inputs and
world metadata become richer.

This document defines the target design. Gameplay values must live in definitions rather than in
rendering or interface code.

## Current system

The current `Terrain` implementation:

- Generates a maximum 20x20-chunk heightfield from domain-warped regional fields, directional
  mountain chains, plains, hills, basins, rocky outcrops, and coastal shelves.
- Uses stable authored curves without per-seed normalization or authored min/max clipping. The
  normalized storage range is only a representation boundary; the generator is free to use all of
  it.
- Uses a 30-world-unit vertical scale so mountain silhouettes and valleys remain legible against
  the 480-world-unit maximum map extent.
- Samples expensive regional noise on a configurable coarse grid, then interpolates it across the
  render heightfield. This makes large-scale forms continuous and keeps loading work bounded.
- Runs explicit post-processing passes: broad plain leveling, smaller-radius hill rounding, light
  mountain erosion, and final coastal smoothing. Each pass is masked by its landform rather than
  uniformly flattening the map.
- Derives biome, traversal, buildability, water, mountain barriers, and passes from the resulting
  authoritative terrain.
- Builds navigation from slope after terrain generation.
- Supports immutable base heights plus derived construction foundations.

The current resource generator:

- Gives each resource a fixed number of mirrored cluster pairs.
- Samples cluster centers inside one fixed extent.
- Uses the same global minimum/maximum height and slope rules for all resources.
- Creates a fixed number of members with a uniformly random offset inside a square.
- Places guaranteed mirrored opening Scrap nodes near both headquarters.

The current vegetation generator is definition-backed and deterministic, but it is a separate
height filter rather than a consumer of biome and surface data. Dry, aged, and fresh short grass
are presentation-only decorations and are cleared by construction.

## Design principles

1. **Layered generation** — each stage produces reusable world data for later stages.
2. **Deterministic generation** — one seed and ruleset always produce the same authoritative map.
3. **Named random streams** — changing grass must not move resources or player starts.
4. **Data-driven distributions** — terrain types, biomes, resources, and decoration belong in JSON.
5. **Fair but not mirrored** — starts receive equivalent opportunities, not identical geometry.
6. **Gameplay/presentation separation** — traversal and resources are authoritative; grass clumps,
   pebbles, color variation, and similar detail are presentation data.
7. **Chunk locality** — generation and rebuild work should operate per chunk with deterministic
   borders and neighbor sampling.
8. **Inspectable output** — F3/debug tools should expose fields, biome IDs, traversal class, water
   depth, resource suitability, and generation decisions.

## Modular generation architecture

Terrain generation is selected through `assets/gameplay/terrain/layouts.json`. A layout is a
match-level choice and is separate from the lower-level noise generator:

- `TerrainGenerationPipeline` owns the stable stage order.
- `TerrainLayoutGenerator` applies broad topology such as natural continents, open plains,
  rolling hills, a central hill, central water, or an archipelago.
- `TerrainFieldGenerator` produces reusable regional fields and the base procedural heightfield.
- `TerrainWaterGenerator` owns the optional hydrology stage and consumes the completed layout
  instead of defining the layout itself. Its dry path initializes every water, river, wetland,
  flow, and diagnostic field to a valid empty state.
- `TerrainBiomeGenerator` classifies one independently enabled set of biome definitions.
- Resource, start, vegetation, navigation, and validation stages consume the resulting terrain
  metadata and remain outside the layout generator.

Layouts can select a procedural generator, toggle hydrology, and optionally restrict the enabled
biome set. An empty `enabledBiomes` list means every enabled biome is available. A non-empty list
must include the fallback biome and, when hydrology is active, both water biomes. Individual biome
definitions can use `"generator": "range"` or `"generator": "disabled"`; `enabled: false` remains
the global switch for a biome.

Set `"hydrologyEnabled": false` on a layout to omit oceans, rivers, lakes, and wetlands for that
generation. Direct procedural callers can pass `waterEnabled = false`; both routes use the same
`TerrainWaterGenerator::clear` behavior, so rendering, semantics, diagnostics, and navigation see
one consistent dry map rather than stale or partially initialized water data.

The top-level `layouts.json` switch `"waterGenerationEnabled": false` currently disables the water
stage for every layout while water work is paused. Per-layout `hydrologyEnabled` values are retained
for later use, but cannot override the global off switch. Dry generation also excludes the deep-
and shallow-water biome classifiers so low terrain remains ordinary land rather than invisible,
water-domain-only terrain.

Authored maps use a layout with `"source": "custom_map"` and a `map` path. The initial authored
map contract is a JSON height grid:

```json
{
  "width": 3,
  "height": 3,
  "heights": [0.2, 0.3, 0.2, 0.3, 0.6, 0.3, 0.2, 0.3, 0.2]
}
```

Values are normalized heights and are deterministically resampled to the terrain mesh. The layout
still chooses a generator preset because its water, barrier, connectivity, and regional-field
settings are reused by later stages. This keeps custom maps compatible with the same simulation
systems while allowing future authored biome, water, start, and resource layers to be added to the
manifest without creating a second world representation.

The built-in layout definitions currently include `continental`, `open_plains`, `rolling_hills`,
`central_hill`, `central_water`, and `archipelago`. Only `continental` is active by default; match
setup can select another layout by constructing terrain with its stable `TerrainLayoutId`.

### Water diagnostics

F6 cycles a water-only diagnostic through raw generation, the smoothed render result, a smoothing
difference view, and off. Hydrology retains raw surface and coverage fields before shoreline
smoothing so the view compares real pipeline stages rather than attempting to reconstruct the
input afterward. The overlay reports both surfaces, depths, coverages, their deltas, and the local
flow vector. Animated white bands travel along the flow field; their direction and discontinuities
make incorrect reach ownership and confluence transitions visible. F4 and F6 are mutually
exclusive to keep terrain-semantic and water-stage diagnostics unambiguous.

### Current terrain work priority

Water refinement is intentionally paused. Oceans, rivers, lakes, wetlands, traversal semantics,
F6 diagnostics, and the optional `TerrainWaterGenerator` stage remain part of the supported
foundation, but visual river repair is not an active TODO. Known raw-field discontinuities at
reach ownership changes, confluences, narrow vertex-sampled channels, and depth/coverage cutoffs
are retained as future hydrology work rather than being hidden with additional shader smoothing.

Active terrain-generation work proceeds in this order:

1. Complete resource-field statistical tolerances, independent named-stream isolation tests, and
   non-circular field shapes.
2. Extend F4 with connected navigation regions, resource suitability/rejection reasons, selected
   starts, fairness travel-cost bands, retries, and per-stage generation timings.
3. Add deterministic generation reports and performance/memory budgets for 10x10, 15x15, and
   20x20 maps.
4. Formalize layout-specific and biome-specific definition blocks so future match setup can toggle
   biomes and supply overrides without hard-coded generator branches.
5. Harden custom-map layers and validation after the procedural definition contract is stable.
6. Expand multi-seed, chunk-boundary, connectivity, and statistical tests using broad quality
   bounds and a small set of documented regression seeds.

Deferred work consists of river/water visual refinement and further vegetation assets, tuning,
diagnostics, and LOD. Resume those only when explicitly reprioritized.

## Target generation pipeline

Generation is performed in the following fixed order.

### 1. Establish map domain and deterministic streams

The match seed, map dimensions, generator version, and ruleset ID form the world-generation key.
Derive stable named streams such as:

```text
terrain.continentalness
terrain.erosion
terrain.peaks
terrain.moisture
terrain.temperature
terrain.detail
hydrology.water
resources.scrap.regions
resources.scrap.clusters
resources.oil.regions
resources.uranium.regions
vegetation.grass.dry
vegetation.grass.aged
vegetation.grass.fresh
starts.candidates
```

Never consume one shared random sequence across stages. Adding a decorative variant must not alter
an authoritative resource layout.

### 2. Generate low-resolution regional fields

Generate slowly varying fields at a lower resolution than the render heightfield:

- **Continentalness:** basin, lowland, upland, or major elevated mass.
- **Erosion:** broad smooth country versus broken ridges and ravines.
- **Peaks/ridges:** local mountain potential.
- **Moisture:** dry to wet surface conditions.
- **Temperature:** reserved for visual variety and future weather/gameplay.
- **Resource potential:** one independent suitability field per resource family.

Use domain warping so region borders are irregular. Fields should be sampled from world
coordinates, not normalized map coordinates, so the same configured scale behaves consistently on
10x10, 15x15, 20x20, and future map sizes.

### 3. Classify macro regions and biomes

Biome selection should use several fields rather than height alone. Height remains the main visible
signal initially, but it is not the biome identity.

Initial biome set:

| Biome | Typical elevation | Terrain character | Traversal | Resource tendency |
|---|---:|---|---|---|
| Deep water | Below deep-water level | Submerged basin | Impassable to ground | Future offshore resources |
| Shallow water | Below shoreline | Coastal/river shallows | Impassable initially | Future water interaction |
| Shore/wetland | Very low | Flat, damp transition | Passable with possible penalty later | Oil-biased |
| Dry lowland | Low | Broad open plains | Highly passable/buildable | Scrap clusters |
| Temperate plain | Low–middle | Rolling green terrain | Highly passable/buildable | Scrap, balanced deposits |
| Upland | Middle–high | Firmer rolling terrain | Passable, less buildable | Uranium-biased |
| Rocky highland | High | Ridges and exposed rock | Mixed/limited | Uranium-rich, Scrap-poor |
| Mountain/cliff | Extreme height or slope | Sharp peaks and walls | Impassable | Rare exposed deposits |

Biome definitions need weights and ranges for continentalness, elevation, moisture, erosion, and
temperature. Boundaries should blend presentation materials, but every authoritative cell should
have one stable biome ID and traversal class.

Height-based grass variants are a first presentation rule:

- Dry short grass occupies suitable low, dry land.
- Aged short grass occupies transition and middle elevations.
- Fresh short grass occupies wetter or higher green terrain.

These clumps do not affect collision, navigation, visibility, resources, or checksums. Biome surface
coverage should also drive shader-level grass color/detail independently of spawned clump models.

### 4. Synthesize and shape the base heightfield

Combine the regional fields with biome-specific terrain profiles:

```text
macro elevation
+ biome landform
+ ridge/mountain contribution
+ domain-warped medium detail
+ restrained local detail
= immutable base height
```

The current shaping pipeline is:

1. Sample low-resolution deterministic regional fields and bilinearly interpolate them across the
   render mesh.
2. Compose continental elevation, rolling hills, narrow directional mountain belts, foothills,
   basins, rocky outcrops, restrained detail, and the initial coastal shelf.
3. Level plains with a broad blur masked away from mountains and outcrops. This forms naturally
   buildable areas without stamping flat circles.
4. Round hills with a smaller kernel so their broad silhouettes and intervening valleys survive.
5. Apply light repeated erosion only to mountain/outcrop masks, removing needle peaks without
   erasing ridgelines.
6. Smooth coastal shelves last, preventing the landform passes from reintroducing stepped shores.
7. Clamp only to the normalized storage representation, then preserve the result as immutable base
   height.

Generator JSON owns the regional sampling stride, pass radii, and pass strengths. Mountain-chain
thresholds independently control how much of the broad ridge noise becomes a mountain belt; this
prevents a map-wide raised blanket while allowing substantially taller peaks.

Rules retained by the implementation:

- Do not normalize each map using its observed minimum and maximum. Use stable configured curves
  and clamp only at the end. This preserves meaningful biome and water thresholds across seeds.
- Use terrace-free continuous curves for plains and uplift curves for hills/mountains.
- Apply smoothing selectively by landform and erosion field, not uniformly to the entire map.
- Reserve contiguous buildable plateaus without forcing perfectly flat circles.
- Generate cliffs from slope/uplift masks rather than single-cell spikes.
- Sample beyond map boundaries when calculating noise and normals, preventing edge artifacts.

Construction foundations remain a derived layer over immutable generated heights. They must not
modify biome identity or regenerate resources.

### 5. Hydrology and water

Water uses a stable ocean level plus deterministic local inland water surfaces:

- Terrain below the ocean level renders with a water surface.
- A configurable coarse hydrology grid runs a deterministic priority flood. Every inland cell gets
  a downstream receiver that reaches the ocean or map boundary, including cells inside closed
  depressions.
- The drainage grid currently samples every 2 world units, independently of the denser render
  mesh. River and wetland thresholds are expressed as upstream catchment area rather than cell
  counts, so changing hydrology resolution does not change the intended drainage density.
- Drainage is accumulated from high cells toward those receivers in stable elevation/index order.
  High-accumulation cells are first assembled into an authoritative river graph rather than being
  stamped directly into terrain. The graph is split into stable connected reaches between sources,
  confluences, and outlets; short first-order stubs are pruned deterministically.
- Every active channel cell receives a Strahler stream order. Equal-order tributaries raise the
  downstream order, while a larger tributary retains its order. Width and carving strength consume
  both stream order and accumulated drainage, producing a clear tributary-to-main-river hierarchy.
- River reaches are displaced only in low-slope terrain, projected toward the local valley floor,
  smoothed with deterministic Chaikin passes, and resampled at a fixed world-space interval. Their
  stored water surfaces are constrained to descend monotonically. This yields continuous paths for
  later distance-field carving without allowing decorative meanders to cross ridges or flow uphill.
- Complete paths are rasterized into a signed river-distance field together with nearest flow
  direction, local half-width, water surface, drainage, and stream order. Overlapping tributaries
  use the minimum signed distance, so bends and confluences form a continuous union rather than a
  stack of circular or line-segment stamps.
- One terrain pass derives a consistent cross-section from that field: a gently curved channel bed,
  a smooth bank transition, and a wider low-strength floodplain flattening zone. Channel depth and
  floodplain width scale downstream with the authoritative path attributes. A final configurable
  blur and minimum-depth cutoff operate on the water-depth field—not the terrain—to remove
  saw-tooth water slivers without softening the surrounding landform.
- Confluences are detected from the authoritative upstream graph. Their channel fields widen and
  blend into the downstream reach, receive a stable downstream flow vector, and may expose a
  deterministic sediment bar on one side of the junction. River reaches that terminate in ocean
  widen progressively over a configurable estuary length instead of ending at full river width.
- Channel, bank, floodplain, wetland, sediment, confluence, and estuary masks remain distinct.
  Semantic terrain samples use these masks to blend wet-bed rock/dirt, muddy banks, greener
  floodplains, wetland soil, and sediment deposits. The masks are also exposed as terrain tags so
  vegetation and later gameplay rules can target them without inferring zones from color.
- Shallow filled depressions with sufficient drainage become small lakes. Lower-accumulation,
  low-gradient drainage areas become wetlands instead of open water.
- Terrain vertices store an explicit water kind (`none`, `ocean`, `river`, or `lake`), continuous
  water coverage, local water-surface height, and drainage metadata. Wetlands are a separate land
  tag and do not imply an open-water surface.
- The water shader reads both coverage and local surface height. It never infers water merely
  because a stored surface happens to be above a later terrain deformation. This permits elevated
  rivers and lakes without forcing their beds to sea level or producing water around foundations.
- Authoritative `waterDepth`, `waterCoverage`, `waterKind`, and `isSubmerged` values are stored per
  terrain/navigation cell. A water surface height is meaningful only where coverage is nonzero.
- Terrain supplies independent traversal costs for land, water, and air domains. Deep water denies
  land movement while allowing water and air movement.
- Building definitions state whether shallow/deep water placement is permitted; initially all
  existing buildings require dry land.
- Drones may fly over water because their movement profile includes the air domain. Ignoring entity
  obstacles is a separate property and does not grant access through terrain barriers.
- Fog and visibility continue across water unless a future rule changes them.

River/lake/wetland classifications and local water depth are authoritative. Reflections, foam,
waves, and decorative reeds remain presentation-only consumers of those results.

Construction foundations modify only the derived land height layer. Their influence fades to zero
at occupied water vertices; they neither move water surfaces nor rewrite ocean, river, or lake
identity. Removing a foundation therefore restores base terrain without requiring hydrology to be
regenerated.

### 5a. Coast formation

Coasts are shaped before drainage and classified after local water occupancy is known:

- A masked final smoothing pass creates shallow coastal shelves.
- Low, gently sloped coast cells classify as beaches and use the beach surface blend.
- Peak/rock overlap creates occasional rocky coasts with difficult traversal.
- Broad basin masks deepen selected continental edges into bays.
- Sparse peak/outcrop overlap lifts parts of the coastal shelf into small offshore islands.
- Shoreline tags are derived around ocean, river, and lake boundaries. Wetlands also carry the
  shoreline tag, allowing the existing reed vegetation rule to populate low-gradient wet areas.

### 6. Derive terrain semantics and navigation

For every authoritative terrain cell, derive and cache:

- Base height
- Gradient and slope angle
- Biome ID
- Water depth
- Legacy traversal class: open, difficult, or impassable (used for broad terrain semantics)
- Per-domain movement costs for land, water, and air; a missing/zero cost denies that domain
- Buildability class
- Material/surface tags
- Movement profile on each unit archetype, including one or more allowed domains and whether it
  ignores entity obstacles

Initial land-impassable terrain consists of water and slopes above the configured ground-unit limit.
Water-only units use the water domain, amphibious units carry both land and water domains and use
the cheapest valid cost per cell, and aircraft use air. Explicit universal barriers omit all three
domain costs, so even aircraft and drones cannot cross them.
Mountain chains should form useful strategic barriers while leaving deterministic passes. Validate
that each player can reach required opening objectives and that the traversable land graph is not
accidentally split unless the selected map rules explicitly allow islands.

Navigation should consume this semantic grid rather than resampling raw terrain repeatedly. Future
movement profiles can interpret it differently: infantry, tracked vehicles, wheeled vehicles,
amphibious units, aircraft, and drones.

### 7. Select fair starting regions

The fixed opposite-edge formula has been replaced by deterministic candidate-region selection.
Every candidate is centered in an interior chunk; the outermost chunk ring is never eligible.
Opponent separation scales with playable map size: starts must be separated by at least the
configured fraction of the map's side length (currently one half). For two players, the generator
maximizes separation among valid pairs after applying that minimum, with terrain quality breaking
distance ties. The seed resolves otherwise equivalent choices and which player receives which side,
so player one is not tied to a fixed corner. For larger future matches, each
additional player maximizes its distance to its nearest already-selected opponent; team-aware
minimum separation rules will be added when matches support more than two players.

A valid starting region requires:

- Sufficient connected buildable area for the command hub and early buildings.
- A minimum amount of traversable land around the base.
- An accessible opening Scrap field within the target travel-time band.
- Comparable expansion opportunities and access to common resources.
- No direct line through impassable terrain that traps the player.
- A minimum distance from water, cliffs, and map boundaries unless the map preset permits them.

Generate many candidates, score them deterministically, then select a pair with strong separation
and a bounded fairness difference. Do not mirror terrain or resources. After selection, run an
opening-playability validator and retry using the next deterministic candidate pair if required.

### 8. Generate resource regions and fields

Resources should be driven by biome suitability, independent potential fields, and local resource-
field rules. The result should contain rich areas, sparse areas, and occasional absence where
permitted.

The authoritative generation hierarchy is:

```text
resource-family potential
  -> generated resource-field position and footprint
    -> deterministic node count
      -> individually placed resource nodes
```

A resource field is generation metadata, not a harvestable entity. It owns a center, radius/shape,
resource family, deterministic stream identity, and desired node count. Every node is a normal
resource entity with its own presentation variant and remaining capacity.

Field footprints are deliberately allowed to cross biome and surface boundaries. Fields may also
overlap other fields, including fields belonging to another resource family. A field center may be
chosen from the resource's regional potential and broad placement rules, but the field footprint is
not clipped at a biome boundary. Every node candidate is validated independently against the node
variant's terrain requirements.

Nodes may never overlap another authoritative resource node, regardless of which fields or resource
families produced them. Ordinary entity collision, map boundaries, terrain suitability, and access
validation still apply. Field overlap therefore creates natural mixed-resource areas without
allowing intersecting harvestable geometry.

Resource placement now has a typed terrain-tag boundary. Existing Scrap, Oil, and Uranium nodes
explicitly require `land`, so ordinary resources cannot be generated under water. Definitions may
require or forbid any combination of `land`, `water`, `shallow-water`, `deep-water`, `shoreline`,
`submerged`, `dry`, `vegetated`, `rocky`, `buildable`, and `no-build`. Shoreline is derived on both
sides of a land/water boundary rather than tied to one biome. This prepares later coastal and
water-specific resources without special-casing their IDs in world generation. Per-resource
height and slope limits may override the global fallback rules.

Vegetation generation always rejects submerged samples independently of its biome and surface
allowlists. Grass and trees therefore remain land-only even if a future data edit accidentally
adds a water biome to one of those lists.

Each resource-field definition should support field distribution separately from node variants:

```json
{
  "generation": {
    "stream": "resources.scrap",
    "requiredTerrainTags": ["land"],
    "forbiddenTerrainTags": ["deep-water"],
    "allowedBiomes": ["dry_lowland", "temperate_plain"],
    "minimumHeight": 0.16,
    "maximumHeight": 0.62,
    "maximumSlopeDegrees": 12,
    "regionThreshold": 0.48,
    "regionScale": 90,
    "fieldsPerSquareChunk": [0.04, 0.11],
    "fieldRadius": [3, 10],
    "nodesPerField": [2, 9],
    "placementAttemptsPerNode": 20,
    "minimumNodeSpacing": 2.5,
    "variants": [
      { "node": "scrap_small", "weight": 5 },
      { "node": "scrap_medium", "weight": 3 },
      { "node": "scrap_large", "weight": 1 }
    ],
    "startingNodesPerPlayer": 3,
    "startingMinimumDistance": 16,
    "startingMaximumDistance": 28
  }
}
```

Resource-field generation should:

1. Build a resource suitability field from biome, height, slope, moisture, and the resource's named
   potential noise.
2. Identify contiguous high-suitability regions.
3. Choose a variable number of field centers proportional to map area and abundance settings.
   Do not reject a field because its footprint overlaps another field or crosses a biome boundary.
4. Give each field a deterministic radius/shape and node count.
5. Sample each node independently inside the field using deterministic blue-noise/Poisson-like
   spacing, validating terrain at the sampled node rather than clipping the field itself.
6. Select a weighted node archetype such as small, medium, or large. The selected archetype owns
   its model/presentation, collision shape, base capacity, and optional terrain restrictions.
7. Vary rotation and definition-backed capacity only where the selected variant permits it.
8. Reject node-to-node overlap globally, including nodes generated by other overlapping fields.
9. Reject inaccessible nodes and validate global and per-player access after placement.

Suggested initial tendencies:

- **Scrap:** common in dry lowlands and plains; broad fields with many mixed-size nodes.
- **Oil:** uncommon in low basins, wetlands, and selected plains; compact fields with high local
  value.
- **Uranium:** rare in uplands and rocky highlands; small fields with a few distinct crystal sizes.
- **Synthetic:** remains produced/generated by its intended gameplay systems rather than becoming a
  universally scattered raw node unless the resource design document changes.

The abundance match setting should affect field frequency, nodes per field, and/or total regional capacity, not
simply multiply a fixed mirrored pair count.

### 9. Validate resource fairness without visible symmetry

Opening guarantees remain explicit, but generated deposits should not be mirrored. Fairness should
be evaluated using path cost and available capacity:

- Opening Scrap capacity reachable per player
- Distance/path cost to the nearest common and rare resource
- Total resource capacity within expanding travel-time bands
- Number of viable expansion regions
- Whether water or mountains deny one player a resource category

Use bounded tolerance bands rather than exact equality. If validation fails, first add or relocate a
small compensating cluster in the deficient player's eligible region. If that cannot produce a valid
map, retry the resource-layout stream with a deterministic attempt index. Do not regenerate terrain
unless terrain itself fails starting-region validation.

Implemented: land travel-cost fields are calculated once per player and reused across every
resource category and retry. Each resource definition owns its bands, comparison threshold,
minimum reachable capacity, tolerance ratio, compensation count, and retry limit. Compensation is
independently randomized per player and attempt; fairness therefore does not imply visual symmetry.

### 10. Scatter non-gameplay surface detail

After authoritative terrain, navigation, starts, buildings, and resources are accepted, generate
presentation-only detail:

- Dry, aged, and fresh grass clumps
- Shader-level grass coverage and color variation
- Pebbles and tiny stones that are not harvestable nodes
- Multiple small flower and grass species selected by biome and surface
- Actual reed clusters restricted to shoreline terrain
- Several pebble and small-stone forms on suitable dry, upland, and rocky surfaces
- Decals for dirt, cracks, damp soil, and resource-region character
- Optional ambient particles such as dust or pollen

Do not scatter generic dirt patches or shoreline debris as mesh decorations. Ground variation
belongs in terrain materials/decals, while the shoreline silhouette should come from reeds, stones,
water, and the terrain itself. Current realistic scatter assets are normalized to metre scale and
triangle budgets by `tools/normalize_scatter_asset.py` before entering the runtime manifest.

Vegetation rules define a named smooth density field, coverage threshold, cluster frequency,
per-cluster instance and radius ranges, optional moisture/elevation limits, and a spacing group.
Placement uses deterministic cluster streams and a spatial grid. Only members of the same spacing
group exclude one another, so ground cover, flowers, reeds, weeds, and stones can overlap naturally
without returning to a global quadratic distance scan.

Decorations must:

- Use separate named streams per decoration family.
- Follow biome, moisture, height, slope, and surface tags.
- Never block navigation or building placement.
- Be cleared or hidden within building footprints and foundation falloff.
- Be generated or instanced per chunk and excluded from authoritative world-state checksums.
- Be reproducible locally from seed, generator version, and rules.

Larger decorative objects that affect collision or sight are no longer presentation-only and must
become authoritative entities with explicit definitions.

## Data model

Introduce a terrain-generation definition set, for example:

```text
assets/gameplay/terrain/
  generators.json
  biomes.json
  surfaces.json
  water.json
  decorations.json
```

Core runtime types:

```cpp
struct TerrainSample {
    float baseHeight;
    float slopeDegrees;
    BiomeId biome;
    SurfaceId surface;
    float waterDepth;
    TraversalClass traversal;
    BuildabilityClass buildability;
};

struct WorldGenerationResult {
    Terrain terrain;
    TerrainSemanticGrid semantics;
    StartingRegions starts;
    ResourceLayout resources;
    GenerationDiagnostics diagnostics;
};
```

Use typed IDs for biome, surface, generator preset, and traversal profile. Resource generation stays
part of authoritative match creation. Decorative scatter should be a render/environment resource,
not thousands of authoritative `Entity` records.

## Determinism, persistence, and multiplayer

- All authoritative stages use integer iteration order and named deterministic streams.
- Avoid `unordered_*` iteration when results affect generated output.
- Quantize field thresholds and candidate scores where floating-point ties could change ordering.
- Give the generator a version ID. A save stores the seed, version, preset/ruleset identity, map
  dimensions, and authoritative changes after generation.
- During development, saves may store generated authoritative terrain/resource data directly if
  reproducing old generator versions is not desired.
- Extend the world checksum with generator identity, authoritative terrain semantics, resource
  layout, water occupancy, and post-generation terrain changes.
- Presentation-only decoration is regenerated locally and excluded from network commands and world
  checksums.

## Chunking and performance

- Generate low-resolution regional fields once, then materialize height and semantics per chunk.
- Every chunk samples a border large enough for normals, slopes, biome blending, and decoration
  spacing; this prevents seams.
- Cache immutable base height and semantic data separately from construction-foundation deltas.
- Build navigation and render meshes from the same accepted terrain result.
- Upload terrain chunks incrementally during loading and report stage/chunk progress.
- Use GPU instancing for dense grass and minor decorations.
- Resource clustering operates on region grids before creating world entities, avoiding repeated
  whole-world collision scans.
- Generation must have deterministic work budgets and useful diagnostics for failed placement
  attempts.

## Loading-screen progress

Map loading should expose these stages:

1. Terrain fields and base height
2. Water, shoreline, biome, and terrain semantics
3. Navigation
4. Starting-region selection
5. Resource regions and clusters
6. Fairness/playability validation
7. Decorative scatter
8. Incremental terrain mesh upload
9. Required asset upload
10. Match ready

Progress should be based on completed deterministic work units, such as generated chunks and tested
candidates, rather than simulated timers.

## Debugging and visualization

Add F3/debug overlay modes for:

- Biome colors and biome IDs
- Continentalness, erosion, moisture, peaks, and each resource-potential field
- Slope, water depth, traversal class, and buildability
- Connected navigable regions and mountain passes
- Resource suitability, cluster masks, rejected nodes, and capacity totals
- Starting candidates, scores, chosen regions, and fairness metrics
- Chunk borders and generation/upload status

Allow exporting a compact map-generation report for a seed. It should include field ranges, biome
coverage, water coverage, traversable/buildable percentages, resource counts/capacities, fairness
scores, rejection reasons, generation timings, and checksum.

## Validation and tests

### Determinism

- Same seed, dimensions, generator version, and rules produce identical terrain semantics,
  resources, starts, and authoritative checksum.
- Changing a decoration rule does not change terrain, starts, or resources.
- Changing one resource definition does not move other resource types.

### Terrain quality

- No NaN/infinite heights or normals.
- Adjacent chunks agree at borders and produce continuous normals.
- Water threshold and biome ranges remain stable across seeds.
- Impassable hills form contiguous shapes rather than isolated single-cell blockers.
- Every standard map has minimum buildable and traversable coverage.

### Gameplay validity

- Headquarters and opening buildings fit entirely on valid terrain.
- Both players have reachable guaranteed Scrap within configured travel cost.
- Required resource categories exist and are reachable under the selected preset.
- Ground connectivity meets preset requirements; drones remain able to cross water and cliffs.
- Resource nodes never overlap starts, buildings, water (unless allowed), or each other.

### Statistical tests

Run batches of fixed seeds for every supported map size and report:

- Biome and water coverage distributions
- Buildable/traversable area
- Resource cluster counts, capacities, and spatial variance
- Starting-region fairness difference
- Generation time and memory

Use broad quality bounds, not snapshots of exact aesthetics. Retain a small set of golden seeds for
visual regression screenshots and known edge cases.

## Implementation sequence

### Implementation status

Completed in the first terrain-rewrite slice:

- Terrain generator, biome, and surface definition files with validated loading.
- Six deterministic landform masks—plains, hills, mountain belts, rocky outcrops, coastal shelves,
  and basins—now participate directly in height synthesis. Their named streams and definition-backed
  strengths spatially separate broad playable ground, rolling relief, uplift, local rock, shelves,
  and depressions without changing authoritative terrain ownership. F4 exposes all six masks.
- Mountain belts use two configurable directional, anisotropic, domain-warped ridge families. A
  broad foothill envelope bridges plains into the sharper mountain curve, while a high-ridge cliff
  curve adds silhouette without applying equivalent roughness to the whole map.
- Elevation shaping is landform-specific: plains compress continental relief, hills use a signed
  continuous power curve, mountains and cliffs use sharper uplift curves, basins depress land, and
  coastal shelves pull only a narrow band toward water level.
- Smoothing is now selective. Each generated vertex keeps a deterministic smoothing influence:
  plains and shelves receive strong smoothing, hills moderate smoothing, and mountain belts and
  rocky outcrops preserve their detail. The configured pass count remains a global work budget,
  not a global blur strength.
- Named deterministic continentalness, erosion, peaks, moisture, temperature, detail, and domain
  warp fields sampled in world coordinates.
- Definition-backed height synthesis with stable configured bounds and no per-seed min/max
  normalization.
- An immutable 2-world-unit semantic terrain grid containing base height, slope, regional fields,
  typed biome/surface IDs, traversal class, and buildability class.
- Deterministic biome resolution using explicit priority, suitability scores, and stable ID
  tie-breaking. Deep- and shallow-water definitions are now active against the configured stable
  shoreline.
- Terrain queries for complete samples, biome identity, traversal class, and buildability.

Terrain rendering, water rendering, navigation, and decorative grass now consume the semantic
biome/surface layer. More advanced hydrology and the remaining world-generation stages remain in
the following phases.
Water presentation uses semantic depth for a shallow-to-deep color gradient, a narrow animated
shore tint and broken foam band, procedural wave normals with view-dependent highlights, and a
matching depth tint on the underwater terrain bed. The fine terrain topology remains fixed for
water at every camera distance, so the enhanced surface does not reintroduce crawling coastlines.
An F4 terrain-debug view renders semantic colors and cell boundaries without fog and reports the
biome, surface, traversal/buildability, slope, and regional fields beneath the cursor.

### Phase A: generation foundation

1. Add generator version/preset definitions and named field parameters.
2. Introduce world-coordinate regional fields and remove per-map min/max normalization.
3. Add authoritative biome/surface/traversal semantic grids.
4. ~~Make renderer colors/material blending consume biome and surface definitions.~~ Complete.
5. ~~Make navigation consume traversal semantics.~~ Complete. Navigation and its flow-field caches
   use typed land/water/air movement profiles. Open and difficult terrain have data-defined costs,
   amphibious profiles can select either land or water per cell, entity-obstacle bypass is separate,
   and explicit universal barriers block every domain.

### Phase B: water and barriers — foundation complete, refinement paused

6. ~~Add stable water levels, water occupancy, and a basic water render pass.~~ Complete. The
   generator owns a normalized water level, semantic cells expose deterministic depth/submersion,
   and a dedicated fog-aware translucent render pass draws the matching coastline.
7. ~~Define water occupancy and building interaction with water.~~ Complete. Buildings resolve a
   definition-backed land/shallow/deep placement profile across their entire footprint, with an
   optional shoreline requirement. Live preview, authoritative construction, and the map editor
   share the same evaluator and expose localized rejection reasons. Land, water, amphibious, and
   air navigation profiles have focused shoreline-crossing tests.
8. ~~Generate mountain masks, impassable slopes, and deliberate passes.~~ Complete. Generator
   definitions now provide height, peak, cliff-slope, erosion-saddle, pass-slope, and pass-cost
   thresholds. Mountain and cliff cells block land and water movement while retaining air
   movement. High-erosion saddles through mountain signals become deterministic difficult-terrain
   passes. Only the explicit `universal-barrier` terrain tag can block every movement domain.
9. ~~Add connectivity/buildability validation.~~ Complete. Start selection labels deterministic
   connected land components on the semantic grid, rejects components below a definition-backed
   map-area fraction, requires every selected opponent to share a reachable land component, and
   retains the local footprint, expansion-space, resource-site, edge, and separation checks. If a
   requested seed cannot produce a playable layout, match creation tries a bounded sequence of
   deterministically derived seeds and stores the successful resolved seed.

Further river continuity and water presentation work is deferred. When resumed, address the raw
signed water field and reach/confluence ownership before shader polish: continuous sub-vertex
rasterization, one coherent junction surface/flow solution, and tests that reject dry holes inside
connected channels. Do not treat extra blur as the durable fix.

### Phase C: starts and resources

10. ~~Replace fixed start coordinates with candidate scoring and pair selection.~~ Complete.
    Headquarters require a dry, slope-valid footprint, sufficient connected local land, and viable
    nearby opening-resource sites. Guaranteed resources consume the selected anchors directly.
11. ~~Extend the original resource-node definitions with biome, regional-potential, variable
    cluster, density, and capacity rules.~~ Complete for the current single-archetype generator.
    This is the legacy foundation for the planned explicit resource-field/node-variant split.
12. ~~Replace mirrored pair generation with resource regions and irregular clusters.~~ Complete for
    single-archetype nodes. The next revision renames clusters to explicit fields, permits field
    overlap and biome crossing, and selects weighted small/medium/large node archetypes per field.
    Cluster counts scale with playable map area and abundance, centers require high resource-specific
    potential and deterministic spacing, and nodes use irregular radial masks with minimum spacing.
    No generated node receives an automatic mirrored counterpart.
13. **Complete:** path-cost fairness validation and deterministic compensation/retry.
    - Measure path cost and reachable capacity from every starting anchor to each generated
      resource category, including Scrap, Oil, and Uranium.
    - Compare capacity inside expanding definition-backed travel-cost bands rather than using
      straight-line distance or exact mirrored equality.
    - Detect denial caused by water, mountains, disconnected land regions, or insufficient valid
      harvesting edges.
    - First add or relocate a small compensating cluster in the deficient player's eligible region.
    - If compensation cannot satisfy the bounded tolerance, retry only resource layout with a
      deterministic attempt index. Do not regenerate accepted terrain for a resource-only failure.
14. ~~Preserve explicit opening Scrap guarantees without mirroring the rest of the map.~~ Complete.
    Opening Scrap is placed independently around each selected starting anchor using its own named
    per-player stream; regional Scrap, Oil, and Uranium remain asymmetric.

#### Planned resource-field realignment

The existing cluster generator is the migration source, not the final field implementation:

1. ~~Introduce typed `ResourceFieldDefinition` and weighted `ResourceNodeVariant` references.~~
   Complete.
2. ~~Keep harvestable node archetypes in `resource_nodes.json`; move distribution, potential,
   fairness, opening guarantees, and field-shape rules to `resource_fields.json`.~~ Complete.
3. ~~Replace `generatedResourceNodes` in match rules with `generatedResourceFields`.~~ Complete.
4. ~~Generate a plain `ResourceLayout` containing deterministic field descriptors and node spawn
   records before mutating `World`. Fields are not entities; node spawn records become entities.~~
   Complete, including opening and compensation layouts.
5. ~~Remove minimum field-to-field spacing. Validate only the field center's broad regional rules,
   then validate every node against its selected variant and terrain sample.~~ Complete. Field
   footprints can cross biome boundaries and overlap; every resulting node is sampled separately.
6. ~~Use one global spatial index for generated resource nodes so variants and resource families
   cannot overlap even where their fields do.~~ Complete. The grid is rebuilt deterministically on
   a layout retry and includes previously accepted resource families.
7. ~~Make opening guarantees and fairness compensation create small fields rather than isolated
   hard-coded node archetypes. Continue evaluating actual resulting capacity by travel-cost band.~~
   Complete. Opening fields use provisional placement and commit only when their complete node set
   fits; compensation records one small field at each accepted correction site.
8. Add deterministic tests for variable node counts, weighted variants, overlapping/cross-biome
   fields, global node non-overlap, per-node capacity, retry isolation, and equal-seed layouts.

### Phase D: environmental detail

15. ~~Move dense non-gameplay vegetation out of authoritative `World` entities into chunk scatter
    resources.~~ Complete. `VegetationField` is deterministic presentation state owned by the
    session, grouped and culled per terrain chunk, excluded from saves/checksums/collision, and
    submitted to the renderer in model-instanced batches. Harvestable resource trees remain
    authoritative entities.
16. ~~Drive dry, aged, and fresh grass from biome/moisture/surface rules.~~ Complete.
17. ~~Add instanced pebbles, weeds, shoreline detail, and surface decals.~~ Complete at the
    scatter-system level. Multiple pebble, short-grass, weed, wildflower, and shore-reed variants
    have independent named streams, biome/surface/tag filters, scale and slope ranges, and
    normalized instanced presentations. Smooth family-specific density fields produce deterministic
    clusters, while spacing-group spatial grids enforce local separation across chunk boundaries.
    Generic dirt patches and shoreline debris were removed; ground variation remains
    terrain-material/decal work rather than freestanding geometry.
18. ~~Ensure construction clears/hides decoration through the same footprint mask.~~ Complete.
    Accepted placement removes intersecting scatter instances through the authoritative building
    `SpatialShape`; loading regenerates scatter against the loaded authoritative world.

### Phase E: tools and hardening

19. ~~Add loading-stage progress and cancellation-safe generation jobs.~~ Complete for match
    startup. CPU terrain, navigation, starts, resources, validation, and decoration execute in a
    cancellable background job. Atomic phase/work progress drives the live loading screen. The
    accepted authoritative terrain is copied to the renderer and uploaded in bounded chunk batches
    on the OpenGL thread while asset imports/uploads continue through the resource manager.
20. **Active:** add field/biome/navigation/resource debug views and generation reports. F4 resource-field
    bounds, centers, generated-node markers, cursor membership, and nearest-node distance are
    complete. Next prioritize connected navigation regions, resource suitability/rejection reasons,
    chosen starts, fairness bands, and generation timings. Vegetation-specific diagnostics are
    deferred.
21. **Active:** add deterministic, connectivity, fairness, statistical, and chunk-seam tests. Exact multi-seed
    resource-layout determinism, field membership, variant validity, and global node non-overlap are
    covered. Next add statistical distribution tolerances and confirmation that resource
    compensation changes neither accepted terrain nor unrelated named random streams.
22. **Next:** profile 10x10, 15x15, and 20x20 maps and establish generation-time, simulation-time, memory,
    terrain-upload, and rendering budgets. Record repeatable test seeds and machine configuration.

## First playable target

The first overhaul should stop after a coherent vertical slice rather than attempting every future
biome feature at once:

- Stable non-normalized height generation
- Dry lowland, temperate plain, upland, rocky highland, mountain, shore, and water classifications
- Water and steep hills block ground movement
- Drones cross both freely
- Fair opposite starting regions with reachable Scrap
- Non-mirrored, biome-aware Scrap/Oil/Uranium clusters
- Dry/aged/fresh decorative grass driven by terrain semantics
- Debug biome, traversal, and resource-suitability views
- Deterministic and multi-seed fairness tests

This slice provides the architectural boundary needed for later rivers, bridges, roads, additional
biomes, amphibious units, weather, and country-specific terrain interaction without rebuilding the
generator again.
