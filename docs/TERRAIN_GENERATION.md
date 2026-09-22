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

## Current system and limitations

The current `Terrain` implementation:

- Generates one fixed maximum 20x20-chunk heightfield from domain-warped fractal, billow, and ridge
  noise.
- Globally normalizes every seed to the full 0–1 height range. This makes relative terrain readable
  but forces every map to contain similar extremes and prevents stable water/biome thresholds.
- Uses height primarily as terrain color and as a shared filter for all resource types.
- Smooths the complete heightfield four times, reducing small artifacts but also erasing regional
  character.
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

### 4. Synthesize the base heightfield

Combine the regional fields with biome-specific terrain profiles:

```text
macro elevation
+ biome landform
+ ridge/mountain contribution
+ domain-warped medium detail
+ restrained local detail
= immutable base height
```

Important changes from the current generator:

- Do not normalize each map using its observed minimum and maximum. Use stable configured curves
  and clamp only at the end. This preserves meaningful biome and water thresholds across seeds.
- Use terrace-free continuous curves for plains and uplift curves for hills/mountains.
- Apply smoothing selectively by biome and erosion field, not uniformly to the entire map.
- Reserve contiguous buildable plateaus without forcing perfectly flat circles.
- Generate cliffs from slope/uplift masks rather than single-cell spikes.
- Sample beyond map boundaries when calculating noise and normals, preventing edge artifacts.

Construction foundations remain a derived layer over immutable generated heights. They must not
modify biome identity or regenerate resources.

### 5. Hydrology and water

The first water implementation can use a stable global water level:

- Terrain below the water level renders with a water surface.
- Store authoritative `waterDepth` and `isSubmerged` values per terrain/navigation cell.
- Terrain supplies independent traversal costs for land, water, and air domains. Deep water denies
  land movement while allowing water and air movement.
- Building definitions state whether shallow/deep water placement is permitted; initially all
  existing buildings require dry land.
- Drones may fly over water because their movement profile includes the air domain. Ignoring entity
  obstacles is a separate property and does not grant access through terrain barriers.
- Fog and visibility continue across water unless a future rule changes them.

Later hydrology may add lakes and rivers using basin/fill and flow fields. River generation should
be a separate deterministic stage, not an ad hoc height deformation. Water rendering, reflections,
foam, and shoreline decoration are presentation-only; water occupancy and traversal are
authoritative.

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
For two players, the globally farthest valid pair is selected. For larger future matches, each
additional player maximizes its distance to its nearest already-selected opponent. Distance is the
primary comparison; terrain and economic quality only break equal-distance choices.

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

### 8. Generate resource regions and clusters

Resources should be driven by biome suitability, independent potential fields, and local cluster
rules. The result should contain rich areas, sparse areas, and occasional absence where permitted.

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

Each resource definition should support:

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
    "clustersPerSquareChunk": [0.04, 0.11],
    "clusterRadius": [3, 10],
    "nodesPerCluster": [2, 9],
    "placementAttemptsPerCluster": 90,
    "minimumNodeSpacing": 2.5,
    "minimumClusterSpacing": 12,
    "capacityMultiplier": [0.75, 1.35],
    "startingNodesPerPlayer": 3,
    "startingMinimumDistance": 16,
    "startingMaximumDistance": 28
  }
}
```

Cluster generation should:

1. Build a resource suitability field from biome, height, slope, moisture, and the resource's named
   potential noise.
2. Identify contiguous high-suitability regions.
3. Choose a variable number of cluster centers proportional to map area and abundance settings.
4. Generate irregular cluster masks using radial falloff plus warped noise.
5. Place a variable number of nodes inside each mask using deterministic blue-noise/Poisson-like
   spacing.
6. Vary node scale, rotation, and capacity within definition bounds.
7. Reject collisions and inaccessible nodes.
8. Validate global and per-player access after placement.

Suggested initial tendencies:

- **Scrap:** common in dry lowlands and plains; broad clusters with many medium-capacity nodes.
- **Oil:** uncommon in low basins, wetlands, and selected plains; compact clusters with high local
  value.
- **Uranium:** rare in uplands and rocky highlands; small, widely separated clusters.
- **Synthetic:** remains produced/generated by its intended gameplay systems rather than becoming a
  universally scattered raw node unless the resource design document changes.

The abundance match setting should affect cluster frequency and/or total regional capacity, not
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

### Phase B: water and barriers

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

### Phase C: starts and resources

10. ~~Replace fixed start coordinates with candidate scoring and pair selection.~~ Complete.
    Headquarters require a dry, slope-valid footprint, sufficient connected local land, and viable
    nearby opening-resource sites. Guaranteed resources consume the selected anchors directly.
11. ~~Extend resource-node definitions with biome, field, variable cluster, density, and capacity
    rules.~~ Complete. Each raw resource now defines biome and terrain tags, moisture/elevation/slope
    limits, an independent named potential field, cluster-density/radius/node-count ranges, spacing,
    attempt budgets, and capacity variance.
12. ~~Replace mirrored pair generation with resource regions and irregular clusters.~~ Complete.
    Cluster counts scale with playable map area and abundance, centers require high resource-specific
    potential and deterministic spacing, and nodes use irregular radial masks with minimum spacing.
    No generated node receives an automatic mirrored counterpart.
13. Add path-cost fairness validation and deterministic compensation/retry.
14. ~~Preserve explicit opening Scrap guarantees without mirroring the rest of the map.~~ Complete.
    Opening Scrap is placed independently around each selected starting anchor using its own named
    per-player stream; regional Scrap, Oil, and Uranium remain asymmetric.

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
    normalized instanced presentations. Generic dirt patches and shoreline debris were removed;
    ground variation remains terrain-material/decal work rather than freestanding geometry.
18. ~~Ensure construction clears/hides decoration through the same footprint mask.~~ Complete.
    Accepted placement removes intersecting scatter instances through the authoritative building
    `SpatialShape`; loading regenerates scatter against the loaded authoritative world.

### Phase E: tools and hardening

19. ~~Add loading-stage progress and cancellation-safe generation jobs.~~ Complete for match
    startup. CPU terrain, navigation, starts, resources, validation, and decoration execute in a
    cancellable background job. Atomic phase/work progress drives the live loading screen. The
    accepted authoritative terrain is copied to the renderer and uploaded in bounded chunk batches
    on the OpenGL thread while asset imports/uploads continue through the resource manager.
20. Add field/biome/navigation/resource debug views and generation reports.
21. Add deterministic, connectivity, fairness, statistical, and chunk-seam tests.
22. Profile 10x10, 15x15, and 20x20 maps and establish time/memory budgets.

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
