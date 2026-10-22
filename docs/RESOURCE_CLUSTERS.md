# Resource clusters

Status: generation revision 7 implements many individual harvest nodes per field. Field-level
composition replaces compound mini-fields; fixed supplies are preserved but harvest-target counts
change. The earlier appearance prototype is recorded below as historical context.

## 1. Direction and scope

A resource field should read as a place in the landscape: a scrapyard, a seep basin or a mineral
outcrop. Its outline, large features and scattered smaller piles should remain recognizable at the
normal RTS camera distance. Expanding toward it should feel like claiming a resource region.

The intended combination is the broad, coherent patch appearance associated with Factorio and
the readable individual gathering targets we want from the Age of Empires reference. These are
design references, not a requirement to copy either game's generation or harvesting rules.

The strongest art reference is the [main-menu terrain concept](../assets/textures/ui/terrain_generation_concept.png):
large salvage concentrations, occasional major wrecks and smaller piles scattered around them.
The [Central Water concept](pictures/terrain-v2-layouts/central-water-implementation-no-roads.png)
provides a second reference for fitting resources into clearings, banks and rocky terrain.
Its lake and illustrated base positions are specific to that layout, not universal cluster rules.

### Reading the background reference

The following observations come from the menu image. Resource identities without an unmistakable
visual cue remain interpretations; the image cannot establish inventories, ownership or harvest rules.

| Region in the image | Visible composition | Design implication |
|---|---|---|
| Broad debris region on the lower-left shore | A long, irregular expanse of exposed earth; several dense debris pockets; collapsed frames and larger wreck sections; many small fragments with substantial gaps | Build a salvage site with a shared ground treatment and multiple concentrations, rather than enlarging one mound or repeating separate round heaps |
| Dark industrial site above it on the left | Low blackened ground, machinery resembling pump jacks, scattered equipment and nearby water channels | Give Oil a broad, mostly low silhouette, a dark surface signature and occasional vertical machinery; do not infer that every unclaimed source is purely natural or that every machine is a player building |
| Yellow outcrops on the upper-right rock bench | Several separated bright exposures distributed over a larger rocky patch, with darker host rock between them | Compose mineral fields as exposures within a connected geological region; the image does not prove this mineral is Uranium or dictate Uranium's colour |
| Transition from resource regions to ordinary terrain | Uneven margins, exposed soil, rubble, rock and vegetation gaps; no uniform circular boundary | The surrounding surface and gradual fringe make the field coherent before its individual objects are inspected |

The hierarchy is **site → concentrations → individual pieces**. Most of each site is low to the
ground; a few tall structures establish identity. Empty ground inside the outline is intentional.
The Scrap region reads as an accumulation spread over a place, rather than several identical cones.
These cues should survive changes in layout, biome, shoreline presence and seed.

This draft covers field composition, resource readability, placement, interaction and depletion.
It does not redesign processors, delivery commitments, cargo, power or resource conversion.
The broader contracts remain [the resource system](Resource%20system.md) and
[terrain generation V2, section 8](TERRAIN_GENERATION_V2.md#8-resource-distribution-and-cluster-flair).

## 2. Established constraints

- Every player receives the fixed starting allocation: currently **600 Scrap and 400 Oil**.
  These are provisional balance values backed by definitions. They do not scale with abundance.
- Starting resources must be practically nearby and available to their intended player. There is
  no newly imposed fixed maximum distance. Measure actual gathering and delivery access.
- Abundance controls additional field counts, separately from fixed starting fields. A larger
  visual patch must not silently add fields, resource capacity or replacement supplies.
- Expansion positions vary by seed. Do not introduce equal expansion travel costs, mirrored sites,
  equal defensibility or guaranteed expansion quotas as hidden cluster requirements.
- Layouts own distribution constraints. Biomes primarily affect appearance and preferences, but
  may explicitly prohibit a resource at a site, such as Uranium in the ocean.
- Required resource placement has final say during generation: bounded attempts and terrain/access
  repair must lead to a supported result without rejecting the original seed or dropping inventory.
  Terrain is immutable once the match begins.
- Vegetation is intended to become removable. Required resources must already be reachable with
  the mechanics that exist; future clearing cannot justify inaccessible starting supplies.
- More harvest targets, altered throughput and new source capacities require a separate balance
  decision. The earlier deferral of node/source design has not implicitly approved those changes.

## 3. Current field and node implementation

The field owns seeded orientation, bent concentration pockets, sparse links and fringe placement.
Each node is a small selectable resource-bearing pile, exposure, seep or piece of equipment, grounded
individually against the terrain. It has its own remaining supply and depletes independently.

V2 now adds a sparse unharvestable fringe: each node has a 35% chance of requesting one or two tiny
metal flakes, pipe fragments or stone chips. Placement can skip unsuitable candidates. The fragments
sit 0.4–1.2 units beyond the node collider, at 20–35% model scale; they avoid other source/structure
footprints, water, steep edges, opening construction space and reserved routes. Each family's
`decorativeFringe` settings live beside its node recipe. Rubble is zero-collision, unselectable,
clearable-on-build vegetation presentation, with no resource inventory. Its dedicated random stream
cannot change resource generation. Existing source footprints are retained for fringe reconstruction
after depletion, so harvesting does not move or regrow it. The existing vegetation cache handles
local removal. Resource presentation also preserves explicit instance IDs, ensuring depletion removes
the correct target when world entity IDs differ from presentation order.

Nodes within the same field and resource family also receive sparse connecting rubble. A seeded,
stable minimum spanning tree chooses short links instead of connecting every pair. Tiny fragments
follow gently bent, irregular trails, using the same placement exclusions as the local fringe.
Current `decorativeFringe` recipes use `connectionSpacing: 0.65`, `connectionWidth: 0.45` and
`maximumConnectionLength: 16` (world units). Invalid terrain can leave gaps; long links are omitted.
Separate fields never receive inter-field trails. Connections use their own random streams and the
existing vegetation cache, carry no supply or collision, and survive node depletion. This is a
presentation-only addition: V2 generation revision remains 7 and resource budgets do not change.

V2 oil spills are painted into the terrain, with compact wet cores in the site mask's alpha channel
and broader cracked earth in its green channel. Wetness feathers into the ground with stable
world-space variation, dark brown staining and restrained sheen. Field links do not generate wet
cores. `v2_oil_seep` uses a `terrainSurface` presentation with no model; its normal gameplay entity,
selection footprint and inventory remain intact. Oil wellhead models likewise contain no flat spill
polygons. This removes coplanar flickering and terrain intersection without lifting puddles above
the ground. Exhausted sources retain the existing ground-scar behavior. V1 assets remain unchanged.

The wet stain now uses a lighter warm-brown tint (76/67/54% of the underlying surface, blended at
up to 62%) so its centre reads as discoloured earth rather than a shadow. Single-clicking a V2
resource node selects its entire field and shows total remaining supply plus live harvest-node
count. Double-clicking the same node selects that node alone and shows its own supply. Hover still
previews the field, except while the cursor remains over an individually selected node. Field
selection follows field identity through depletion or removal of its original node, including
fields with one live node left. It clears when no live nodes remain. Shift selection retains the
existing additive entity behavior; V1 resources without field identity remain individual selections.

| Property | Scrap | Oil | Uranium |
|---|---|---|---|
| Starting supply per player | 600 | 400 | None |
| Expansion field supply | 1,000 | 600 | 200 |
| Node capacity | 25 or 50 | 25 or 50 | 20 |
| Starting target count | 12–24 | 8–16 | None |
| Expansion target count | 20–40 | 12–24 | 10 |
| Field placement radius | 9–14 | 9–14 | 7–11 |
| Individual collision radius | 0.70–1.10 | 0.85–1.65 | 0.65–1.00 |

These count ranges follow the possible capacity partitions, not equal probabilities. A dedicated
inventory stream chooses legal node variants until the exact budget is exhausted. Definition loading
requires a common capacity quantum and caps a field at 128 targets. Abundance still controls only
additional field counts; the original V1 inventories and presentations remain available.

Every generated node saves a neutral field identity alongside its remaining resource amount. This
identity is checksum-authoritative and independent of transient generation diagnostics. Gatherers
prefer another live source in the same field, then use the existing bounded nearby-source fallback.
Hovering a node also outlines currently visible live nodes in its field. Per-node selection remains
the command target. Gathering speed per worker is unchanged; effective collection timing requires
measurement because source sizes, depletion cycles and travel distances have changed.

The shared site mask links nodes only within the same field. Exhausted entities retain scars;
models disappear individually. Repeated models share persistent instance buffers with field ranges;
depletion updates only the affected range. Terrain changes invalidate intersecting cached transforms.
New/resurrected/moved sources rebuild the presentation allocation. Resource nodes remain gameplay
entities for selection, collision, saves and simulation, independent of the presentation cache.

`tools/build_resource_nodes.py` bakes 13 small node assets from the authored definitions. It does not
rewrite gameplay data. Scrap uses six original salvage silhouettes, Oil reuses the original modular
wellhead/tank/pump/seep kit, and Uranium uses the existing mineral assets with host rock. Many tiny
decorative fragments are still part of these small meshes rather than individual simulation entities.
Continuous within-node erosion and more precise noncircular collision remain future work.

### Earlier compound appearance prototype (revisions 4–6)

V2 revision 5 retains the compound deposit archetypes introduced in revision 4. Each compound has
22, 38 or 58 base placements (large Scrap also has a landmark frame) and remains one selectable,
exhaustible harvest target. Mappings
preserve the original target capacities and counts. Its circular collider and selection bounds cover
the whole compound; tree clearance keeps canopies away from the shared site surface.

Target placement now has a separate deterministic composition stream and definition-backed
directional bias. Scrap favours elongated accumulations, Oil is less directional, and Uranium
favours vein-like arrangements; ordinary eligibility, access and bounded fallback checks still apply.

`ResourceSiteMap` reconstructs irregular, overlapping ground footprints from stable resource IDs and
positions. It joins nearby concentrations of the same resource into broader sites, independent of
harvest-target count. The same mask guides tree-canopy clearance. The renderer caches its tiles in
one texture and shades soil, Oil staining and mineral host ground without altering terrain geometry.
Only affected tiles are rasterized/uploaded when sources move or disappear; unchanged frames reuse
the texture. Ground treatment is gated by current exploration visibility to avoid changing remembered
terrain based on hidden entity state. It does not introduce new saved fields or authoritative groups.

| Current property | Scrap | Oil | Uranium |
|---|---|---|---|
| Starting inventory per player | 600, two targets | 400, two targets | None |
| Additional field inventory | 1,000, three targets | 600, two targets | 200, two targets |
| V2 field placement-radius range | 8–11 | 8–11 | 6–9 |
| Small / medium / large deposit radius | 2.8 / 3.8 / 5 | 2.8 / 3.8 / 5 | 2.8 / 3.8 / 5 |

Distances are world units. A field's current placement radius bounds target centres, not the full
outer mesh envelope. Settings live in [resource fields](../assets/gameplay/resource_fields.json),
[resource nodes](../assets/gameplay/resource_nodes.json) and [presentations](../assets/entities.json).

The [current captures](pictures/terrain-v2-layouts/implementation/resource-sites/README.md) show the
implemented sites. The [revision-4 captures](pictures/terrain-v2-layouts/implementation/cliffs-and-clusters/README.md)
record the baseline before shared ground treatment and the new silhouettes. The appearance pass adds
six procedural small Scrap silhouettes, a broken-frame landmark for large Scrap, a restrained metal
palette, low Oil seep geometry with occasional pump machinery, and mineral host rock with subdued
exposures. Sources and rebuild instructions are in the compound asset folder.

Remaining limits: pieces share a baked arrangement within each archetype; targets deplete as whole
compounds, with ground scars retained while their resource entities remain. Collision still uses
the original circles. Proximity-based visual links do not create a selectable field, change gathering
or introduce resource ownership. Progressive depletion, finer picking/collision and per-piece
destruction are future source/node work. The prototype does not prove unchanged collection times.

## 4. Proposed field composition

### Shared structure

Compose the region before placing its harvest targets. Start with a seeded, irregular footprint
containing overlapping lobes and gaps; use its local density to distribute large cores, smaller
gathering piles and scattered fragments. Avoid an even circle, a regular ring or independent piles
whose only relationship is being inside the same radius.

Use three visual scales:

| Scale | Purpose | Example for Scrap |
|---|---|---|
| Landmark | Establish the field's identity | A broken industrial structure or major wreck |
| Main concentrations | Make the resource-bearing area readable | Several overlapping piles with distinct silhouettes |
| Fringe | Connect concentrations and soften the outline | Small piles, plates, beams and sparse fragments |

Add a fourth, underlying layer: the **site surface**. Patchy exposed earth, embedded rubble,
staining and sparse vegetation visually join the concentrations. This should follow the irregular
composition, not appear as a circular decal beneath each target. The ground treatment is cosmetic:
it must not silently change terrain heights, traversal, building support or resource capacity.

Not every field needs a landmark. Multiple smaller concentrations may form a strong field without
a giant central object. Use orientation, uneven density and material variation to connect them;
do not fill every gap or give every pile the same size and spacing.

Keep the harvestable region distinct from scenery. A prominent salvage pile should belong to a
resource target, rather than being a convincing but unclickable resource-shaped prop. Small inert
fragments may dress the fringe if their scale and treatment clearly separate them from gathering
targets. Decorative pieces never create inventory or block essential access.

The field's visual weight should broadly communicate its supply. Art can spread a fixed inventory
over a larger footprint, but should not make a modest opening field look like an inexhaustible
deposit. Exact visual-size bands and their relationship to remaining supply are still to be decided.

### First art-prototype specification

The following are proposed trial settings, not measurements extracted from the perspective image
and not accepted gameplay constraints. Compare them in the engine before committing dimensions.

| Property | Starting Scrap field | Expansion Scrap field |
|---|---|---|
| Overall visual length | Approximately 1–1.5 Command Hub widths: 20–30 units | Approximately 2–3 Hub widths: 40–60 units |
| Overall visual width | Roughly one half to three quarters of its length | Roughly one half to three quarters of its length |
| Main visual concentrations | 2–3, connected by smaller scatter and shared ground treatment | 3–6, with unequal density and irregular gaps |
| Large silhouette features | Optional broken frame or low wreck section | Optionally 1–2 dominant wreck sections, with smaller concentrations around them |
| Supply | Existing 600 Scrap | Existing 1,000 Scrap |
| Harvest targets | Existing two during the appearance prototype | Existing three during the appearance prototype |

Counts of visual concentrations are independent of harvest-target counts. A target may own several
visible concentrations, with picking spanning its resource-bearing pieces and clear selection feedback.
The intended visible envelope, solid obstacles and interaction region must each be explicit; do not
turn the trial 60-unit visual length into one 60-unit circular collider.

Visual length includes sparse fringe. The resource-bearing concentrations can remain compact enough
for reasonable gathering travel. Nevertheless, wider interaction regions and larger obstacles may
change collection time even with unchanged inventories and target counts; measure that change.
If a trial looks oversized for its supply or makes starting collection awkward, reduce its envelope
or adjust its composition before proposing extra supply or faster harvesting.

For Scrap, prototype at least six small-pile silhouettes, three medium concentrations and two large
wreck-section silhouettes. Prefer distinct forms—beam bundles, sheet stacks, machinery fragments,
collapsed frames and mixed rubble—over colour swaps of the same mound. Rotation, partial burial,
bounded scale variation and different combinations provide additional variety after silhouettes differ.
Most pieces should be low, with dark steel, muted rust and occasional worn paint; avoid covering the
whole field in equally tall, saturated orange piles. Assess the small variants at gameplay distance.

### Resource-specific character

Oil ground now uses lighter stained earth and photographed cracked-mud detail blended with the
underlying terrain/soil palette. The field no longer replaces that terrain with a near-black
surface. Cracks remain visible in the interior, coverage fades irregularly at the edges, and only
small wet pockets darken further. See the Oil ground blend gallery and
`assets/textures/terrain/SOURCES.md`; the field cache and generation are unchanged by this material pass.

Oil's implemented kit now uses original capped/bent wellheads, valves, horizontal tanks,
disconnected pipes and idle/collapsed pump landmarks. Small deposits stay low; medium deposits
carry an idle pump and large deposits a collapsed pump. Flat irregular seep/crust surfaces replace
the repeated angular seep geometry and upright barrels. Terrain uses the existing site mask for
brown stained margins, dark crust and localized wet sheen, without adding water or changing resource
budgets, harvest targets, collision radii or depletion. The model bake and current triangle counts
are documented in `assets/models/gen/resources/clusters/README.md`.

- **Scrap:** irregular industrial debris, large wreck sections and many smaller salvage piles.
  Vary silhouettes, exposed metal, rust, broken machinery and ground scatter. Prioritize additional
  small-pile variants so a broad field does not reveal one repeated model.
- **Oil:** an irregular dark site, with stained ground and low seep/pool details where appropriate.
  Use occasional pump machinery or abandoned equipment if that is the agreed source identity,
  following the reference's low ground mass and sparse tall silhouettes. Machinery status must be
  clear: scenery, neutral source and player-built extractor cannot be visually interchangeable.
  Do not require water channels or wet terrain on dry layouts merely because they appear beside
  this example. Natural seeps and industrial remnants may become separate composition variants.
- **Uranium:** a host-rock region with several mineral-bearing exposures and smaller fragments,
  integrated with rocky terrain. Use the right-side mineral site's distributed composition as
  inspiration; do not copy its yellow colour as evidence of Uranium identity. Material contrast and
  restrained colour accents should identify the resource without requiring every rock to glow.
  Final colour, crystal treatment and source models remain art decisions.

Field identity must survive biome palette changes. Different layouts may produce different outlines
and settings without requiring different economic rules for the same resource.

## 5. Harvest targets, collision and depletion

### Harvest-target alternatives and accepted decision

| Option | Benefit | Cost / risk |
|---|---|---|
| Keep a few compound targets per field | Preserves current target counts and limits simulation cost | Needs convincing composition and depletion within each compound |
| Several individually harvestable piles sharing a fixed field budget | More granular selection, clearing and depletion | Can change concurrency, travel, obstruction and effective income |
| Dense field of individually simulated fragments | Direct interaction with most visible pieces | Highest entity, pathing and rendering cost; greatest balance change |

**Accepted direction:** several individually harvestable nodes sharing the fixed field budget,
implemented in revision 7. Tiny fragments stay decorative. Retaining inventory alone does not
preserve effective income rate; gathering and delivery timing must still be compared.

If target counts change, explicitly decide whether rate limits belong to gatherers, targets or the
whole field. Compare collection, delivery, congestion and depletion timing before accepting a model.

### Interaction and physical footprint

Clicking a substantial resource-bearing piece should select its owning target; hovering and selection
must reveal that relationship. A gather order should work from the visible deposit and remain
predictable when the player clicks near its edge. Avoid invisible interaction points in empty space.

Derive picking, collision and interaction regions from the same composition, while allowing their
shapes to differ for their purpose. Large solid wrecks must have honest blocking geometry. Loose
fragments should not become invisible walls. A decorative fringe must not acquire a field-sized
solid collider merely because it is rendered as one mesh.

Provide usable interaction space and departure paths for supported gatherers. Test the populated
world with buildings, neighbouring deposits and foundations included. Resources beside water need
valid land access under the current source rules; future water extraction requires its own contract.

### Proposed depletion appearance

Move from whole-deposit disappearance toward a few discrete, deterministic visual stages:
large pile → reduced pile → sparse residue → exhausted ground. Keep stages tied to authoritative
remaining inventory so loading and multiplayer show equivalent states. Choose stage thresholds and
the treatment of large wreck shells during the art prototype, rather than inventing new quantities.

Remaining resource must remain selectable and reachable at every stage. Any shrinking collision
shape that opens a route or building site is a gameplay change, requiring deterministic navigation
and placement updates. A purely visual reduction must not leave a surprising invisible obstacle.
Decide explicitly whether an exhausted wreck remains blocking, becomes clearable, or disappears.

## 6. Generation, rendering and verification

Proposed order: allocate existing field budgets → select eligible regions → compose the local field
footprint → place targets and interaction space → validate access and concentration limits → perform
bounded repairs if necessary → add scenery around the accepted field.

### Extrapolated generation recipe

1. **Allocate the field and its inventory first.** Preserve the fixed starting allocation and
   abundance-controlled additional field count. Composition cannot request extra supply because
   a larger wreck or an extra attractive pocket was selected.
2. **Choose a suitable site, then shape its envelope locally.** Start from a seeded anchor and orientation;
   form a bent, elongated or branching envelope from overlapping lobes and bounded irregularity.
   Fit it to usable ground, hill feet, benches or clearings when those features exist. Keep outlines
   diverse; terrain context influences shape without forcing every field against a cliff or shore.
3. **Create a density map inside the envelope.** Place a few unequal concentration centres with
   lower-density links and intentional empty pockets between them. Clip and soften the fringe
   around incompatible terrain. A cluster mask describes local composition; it is not a new world
   elevation grid and does not require flattening the whole site.
4. **Place major pieces and harvest regions together.** Choose occasional landmark wrecks and
   the existing harvest targets before filling small detail. Keep the inventory assignment explicit
   and preserve usable approach/departure space. Substantial salvage-looking pieces must belong
   to a selectable target. If the ownership/picking model cannot support the trial envelope, revise
   the envelope or prototype that interaction first rather than spreading unclickable fake resources.
5. **Populate the density map at several scales.** Use medium piles in dense pockets, smaller piles
   around and between them, and sparse fragments at the fringe. Separate placement by piece size;
   a small fragment must not require the clearance of a major wreck. Reject intersecting major
   solids, floating objects and arrangements that seal required access. Some surface detail can
   overlap visually without becoming a collision object.
6. **Ground the pieces and surface treatment.** Sample the accepted terrain locally for each piece
   or small composed group. A giant rigid mesh must not float across slopes or bridge a cliff just
   to fill the envelope. Blend exposed soil, embedded rubble or staining through the same density
   map with local variation. Avoid roads, regular borders and independent round target decals.
7. **Validate before publishing.** Check complete inventory, local concentration limits, source
   eligibility, construction clearance and populated-world interaction/routes. Ordinary terrain
   fitting must not move a starting field into practical remoteness. If required placement still
   fails after bounded attempts, use the established resource-repair/fallback process; validate
   the resulting composition again. Never solve rejection by adding or dropping supply.
8. **Dress the surroundings and cache the result.** Keep canopies away from the resource-bearing
   region and necessary approach space; allow irregular vegetation pockets where they do not hide
   interaction or block required access. Cache render groups locally so depletion/removal updates
   only the affected portion. Retain the full footprint for picking and access diagnostics, not just
   a centre point and nominal radius.

Different resource families share the site-envelope and density machinery, but use their own
composition recipes. Scrap distributes debris around concentrations; Oil distributes stained/seep
areas and occasional equipment; Uranium follows host-rock exposures. Different layouts choose
eligible sites and allowed variants without making the same field template recur in every biome.

Use independent deterministic streams for placement, composition and asset variation. Adding a
decorative variant should not move other fields or alter supply. Persist authoritative state needed
for depletion/collision; derive replaceable visual details from stable identities and a versioned
recipe. Do not require transient generation metadata to resume gathering after loading.

Batch repeated pieces or bake suitable groups. Cache static transforms and update only affected
targets/chunks when depleted or destroyed. Keep stable piece identities if future individual removal
requires them. Do not merge an entire field into geometry that cannot update locally. Rendering,
selection, fog-of-war and remembered appearances must agree about what is currently visible.

Verify against the current implementation before accepting each prototype:

- At normal camera distance, does it read as one irregular region with multiple scales? Do close-ups
  expose excessive repetition, floating pieces, abrupt outlines or a painted circular patch?
- Do fixed starting supplies, field counts, inventories and local concentration limits remain correct?
- Can players identify and click resources, gather, depart and continue after depletion and loading?
- Do relevant land routes and opening construction sites remain usable after final collisions?
- Does depletion communicate the remaining resource and leave the intended physical footprint?
- Do different seeds, dry layouts, shore sites, map sizes and abundance settings retain variety?
- Are unchanged frames free of transform rebuilds/uploads, and are depletion updates local? Measure
  render time, simulation cost, triangles, batches and memory on the largest supported map.

The revision-4 reference probe is 9.47 ms render-only at 2554×1389 on the largest Central Water map,
with 108 targets and no measured vegetation cache rebuilds/uploads. It is a comparison workload,
not a universal frame budget or proof of full-game 60 FPS. See the
[performance audit](TERRAIN_V2_PERFORMANCE_AUDIT.md).

## 7. Decisions for the next iteration

1. **Field scale:** review the proposed 20–30-unit opening and 40–60-unit expansion Scrap envelopes
   against the 20×20 Command Hub in an actual gameplay view. Approve or adjust the trial dimensions,
   density and visual concentrations before changing placement definitions.
2. **Harvest structure:** keep compound targets initially, or prototype several small harvestable
   piles now? If the latter, define the concurrency rule before changing generation.
3. **Art identity:** agree on the proposed Scrap silhouette set, Oil seep/remnant variants and
   machinery status, and the Uranium material treatment.
4. **Depletion and clearance:** what remains after harvesting, what still blocks movement/building,
   and what can the player remove?

Then build one representative Scrap field with a fixed existing inventory, compare it in-game with
the current field, and iterate on composition and readability. Extend the approved structure to Oil
and Uranium only after their different source identities are agreed. Integrate wider generation
changes after the prototype's interaction, depletion and performance checks pass.
