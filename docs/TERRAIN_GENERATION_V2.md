# Terrain Generation V2 — Playable Regions, Water, Cliffs, Peaks and Resource Clusters

Revised 2026-10-11. **The first replacement implementation is independently selectable beside V1.**
The sections below remain the target specification, not a claim that every planned capability is
finished. See [implementation and verification notes](TERRAIN_GENERATION_V2_IMPLEMENTATION.md)
for the recipe scope, checks, limitations and remaining work.

The replacement uses recipe resolution, regional reservations, immutable natural source samples,
coherent square elevations, owned edges and a shared triangle surface. Central Water activates a
local lake; the other supported recipes use the independent dry path. Archipelago is excluded from
V2 match launch until transport is supported. The retired prototype is not restored. Old prototype
V2 saves are rejected; the development save schema remains format 1.

**Water is a recipe-controlled capability in the target V2 architecture**, including oceans,
rivers, lakes and shorelines. It is not a mandatory stage or feature of every map. Oceans, river
networks and island progression are remaining capabilities, not implicit stages of this version.

## 1. Intended experience

A player should see an understandable landscape: room to establish a base, nearby opening supplies,
open ground for movement, attractive expansion sites, and a few distinctive landforms worth scouting.
Elevation should organize that landscape rather than break every field into little ledges.

Use Age of Empires as the reference for readable strategy maps: familiar layout rules with randomized
geography, coherent terrain areas, usable starts and deliberate obstacles. Add our own character
through sprawling salvage fields, oil pockets, rocky escarpments, stepped peaks and recognizable
river valleys, lakes and coastlines. Resource regions
borrow Factorio's sense of a recognizable deposit while retaining individually harvestable nodes.

Keep the square, voxel-style terrain language. Flat ground consists of connected equal-height cells;
hills rise through readable terraces and ramps. The grid is structural, but the world should not
look like repeated square platforms or a staircase covering every metre.

A good map offers choices: a safer expansion with a longer route, a more exposed resource field,
a hill overlooking an approach, and a route around a cliff. New height combat bonuses, resource
multipliers and construction costs are outside this proposal.

### Accepted seed variation and gameplay contract

The seed changes the large-scale landscape composition within the selected layout's identity:
landform arrangements, basin proportions, ridge directions, valleys, clearings and start locations.
It must not merely move small features within the same rectangular route ring or outer peak ring.
Central Water retains one principal central lake; other layouts retain their own defining features.

Starts may occur anywhere suitable within the active map, not only at corners or opposite sectors.
Respect the layout's supported separation and opening-space requirements without imposing a fixed
corner template. Fixed starting resources mean fixed per-player Scrap/Oil allocations and nearby
practical access, not fixed coordinates for starts or deposits.

Starting-resource guarantees are the fairness requirement. Do not require equal construction area,
travel times, route counts, defensibility, elevation or expansion opportunities between players.
Every start must independently pass minimum usability checks. Seeded asymmetry beyond starting
resources is intentional and can produce stronger or weaker strategic positions.

There is no universal number of approaches or prescribed corridor width. Connected-land maps must
not isolate starts or required destinations behind impassable terrain. Actual unit clearance remains
a movement-validity requirement. Removable scenery may shape routes; future removal cannot excuse
blocked essential access before that mechanic is implemented. Prefer natural bends, open movement
spaces and clearings over compulsory zigzags, excessive detours or generated roads.

Larger maps add landform groups and strategic spaces rather than uniformly enlarging hills, cliffs
and gaps. Resource repairs retain final say; prefer local repairs and record substantial departures
from the layout's visual targets. These decisions revise the target proposal, not the implemented
V2 planner or its current validation tests.

## 2. What we take from Age of Empires

Official AoE IV documentation describes parameterized layouts that retain an overall pattern while
varying geography, starts and resources. It distinguishes terrain scripts from object distribution
data. Adopt that separation between geography and allocation.
[Source: Introduction to Generated Maps](https://support.ageofempires.com/hc/en-us/articles/4869788243220-Introduction-to-Generated-Maps).

AoE II's Arabia changes explicitly reduced excessive elevation near town centers, adjusted nearby
forest generation and introduced varying cliff counts. Treat opening terrain and obstacle density
as deliberate controls in our recipes.
[Source: AoE II DE Update 61321, Random Maps](https://www.ageofempires.com/news/age-of-empires-ii-definitive-edition-update-61321/).

AoE II exposes a cliff exclusion for object generation through `avoid_cliff_zone`. Adopt clearance
checks for resource access, worker exits and scenery near blocked edges.
[Source: AoE II DE Update 34699, Random Map Scripting](https://www.ageofempires.com/news/aoe2de-update-34699/).

These are documented reference behaviors. The algorithms, figures and foundation rules below are
our proposed choices, not a reconstruction of either game's internals or a copy of AoE's economy.

## 3. What changes from the current prototype

The retired V2 prototype rounded each 6-unit cell independently from a natural heightfield, with 2-unit height
increments and 1.5-unit transitions. It established useful mesh/query agreement and immutable
foundations, but those construction rules alone do not produce a convincing strategy landscape.

| Current weakness | Proposed change |
|---|---|
| Frequent tiny ledges, pits and raised cells | Generate coherent terrain regions and connected elevation bands |
| Tall relief squeezed into narrow transitions | Limit ordinary relief; distinguish broad ramps from deliberate cliffs |
| Starts discovered through extensive late searches | Reserve opening regions, then validate actual early building layouts |
| Hills, cliffs and peaks emerge from the same rounding rule | Give each feature a separate mask, shape and gameplay role |
| Small fields with few resource nodes | Larger visual regions and more small piles; harvest-node and inventory tuning deferred |

Retain the mode toggle and general rendering safeguards. The replacement implementation must
provide deterministic allocation, strict foundation policy and exact triangle queries. Replace the assumption that independent quantization is sufficient world
design. The natural pass may now use layout-aware relief and protected opening regions; its previous
“unchanged first pass” requirement is superseded. Terrain still cannot change during building placement.

## 4. Two terrain layers coordinated by a generation plan

Starts, routes, resources, hydrology and decoration are pipeline stages. There are still two terrain
layers; water has its own surface and occupancy data above the final bed. It is not a third terrain
elevation pass or a global blue plane over everything below an altitude threshold.

### Resolve the generation recipe first

The setup interface exposes layouts only for terrain selection. Biomes, water policies and feature
profiles are internal settings authored into each layout, not separate player-facing selectors or
water toggles. Developers can reuse these profiles to create new layouts later. Resolve the selected
layout's internal recipe before reserving regions or sampling height fields.

- **Layout recipe:** controls strategic topology, player sectors, connected-land/island policy,
  opening/expansion space, feature distributions and permitted water types.
- **Biome generation profile:** primarily supplies aesthetic identity: climate, vegetation grouping,
  material palettes, shoreline character and resource-site appearance. It can constrain relief and
  feature ranges within the layout's strategic limits, and explicitly exclude resource types from
  particular regions. It does not independently override layout topology or resource budgets.
- **Local biome classification:** labels the resulting regions for materials and gameplay. This
  later classification must not be confused with the earlier biome profile that shaped the world.

Resolve rules in this order: required resource inventories and playable access; layout topology;
compatible user settings such as supported map size and player count; biome preferences; cosmetic
variation. A hard regional resource exclusion is an eligibility rule, not a preference that repair
may ignore. Layout authors must provide sufficient eligible sites to satisfy all higher-level
guarantees. Check compatibility and fallback capacity before making a layout/settings combination
available. An incompatible authored recipe is a definition error, not an invalid seed.

A layout forbidding water cannot acquire rivers because a biome prefers them. A layout requiring
an ocean cannot use an internal profile that forbids it. An alternative must be an explicitly
authored layout variant with its own terrain plan; never silently substitute it or retain an
ocean-shaped landscape and merely hide its water.

Resolve numeric constraints by intersecting allowed ranges rather than applying last-writer
overrides. Constraint resolution is separate from calculations: abundance multipliers calculate
additional field counts after compatibility is resolved. Layout ranges define the ordinary
terrain-generation envelope; internal biome and feature
ranges can narrow it. For example, layout hill relief of 1–3 increments and biome relief of 2–4
resolve to 2–3. An empty intersection is an authoring incompatibility, not permission to clamp one
profile into an unrelated value. Choose a fixed value or seeded sample inside the resolved range
using the parameter's declared selection rule. Resource repair may override these ordinary terrain
shape bounds as described in section 8, with diagnostics; range intersection still validates the
initial authored recipe. Document units, bounds and selection rule per
parameter. Inactive features do not contribute constraints to the dry terrain path.

Use an explicit water policy: **forbidden**, **optional** or **required**, with allowed feature types
and recipe-specific coverage/count ranges. Optional recipes can choose zero water. Make that seeded
choice before terrain generation, then use the dry path if all water features are inactive.
Dry/wet variants are authored within layouts or exposed as distinctly named layouts where useful.
Optional water is a seeded internal recipe choice; there is no separate player water toggle.

These are proposed definition fields, not claims about existing JSON support. Keep the resolved
recipe inspectable and include its authoritative settings in generation identity. Record each
resolved parameter's range, selected value, selection rule and originating layout/profile/settings
constraints, together with active feature types, resource exclusions and definition revisions.

### Layer A — regional landscape and strategic layout

Select a layout recipe and resolve resource allocations first. Jointly plan candidate opening
envelopes, sufficient eligible map-field capacity, natural landforms and their required connectivity.
Choose start locations from suitable terrain across the map; player sectors are optional layout
tools, not compulsory corner placements. Additional resource destinations are seeded, not paired
player expansion reservations. Reserve their placement capacity without predetermining symmetrical
field locations. Add coastline, catchment, basin and crossing reservations only for active water.
Use irregular regions rather than identical circular clearings or mirrored bases.

Generate a low-frequency natural landscape within that plan: broad lowlands, connected hills,
valleys and ridge directions. Noise supplies irregular boundaries and local character. Feature masks
constrain relief and preserve usable ground around openings. When water is active, plan drainage
on this natural field: ocean level where permitted, river reaches, lake basins, outlets and floodplain
masks. River routes follow valleys; shoreline reservations cannot consume essential opening space.
Dry recipes generate their ordinary plains, hills and valleys without hydrology-driven shaping.

The source field remains the contour guide. Planning controls where relief develops; it does not
replace regions with rectangular shelves. Retain source heights and masks for diagnostics and biome
preferences.

### Seeded landforms and terrain-aware connections

Resolve stable named streams for layout composition, landforms, opening candidates and active water.
Within layout-authored ranges, vary feature count, centre, direction, length, width, height, branching
and curvature. Compose broad lowlands, hill groups, rocky shoulders and valleys before adding small
boundary noise. A global rotation of an otherwise identical landscape is not sufficient variation.
Keep cosmetic scatter independent of authoritative feature placement and resource budgets.

Use reusable feature masks with local orientation, anisotropy and bounded bending rather than a
fixed-angle mountain field plus equal-radius peaks in an outer ring. Preserve broad open ground;
not every layout needs long ridges. Jointly evaluate feature placement and connected lowlands so
route repair does not repeatedly erase the intended landscape. Find valid passages around features
and through natural saddles, then locally ease terrain only where required access fails.

Treat routes as connections through usable regions, not a painted or flattened network of path
centre-lines. Do not force the current four-sided route ring or a minimum number of alternative
approaches. Curves follow terrain opportunities; straight travel through open ground is also valid.
Any route smoothing must remain inside its traversable region and preserve corner and unit-clearance
checks. Reject zigzag repairs and unnecessarily long mandatory detours as quality problems, without
introducing an unapproved universal distance cap.

For Central Water, vary lake proportions, bay placement, shoreline irregularity and optional small
islands while keeping one principal central basin and broad surrounding land. Resolve feasible water
coverage together with opening and connectivity needs instead of sampling a range whose every value
clamps to the same basin radius. Dry layouts omit all such water-driven decisions. Scale larger maps
by adding feature groups, valleys and open spaces; use plausible per-feature sizes rather than
multiplying every feature dimension by map extent.

### Layer B — coherent square elevations and explicit transitions

The current visual prototype uses a 1.5-unit cell side and 0.5-unit height increment.
The six-unit/two-unit comparison proved too coarse beside buildings and shorelines. Preserve
broad source relief while articulating it with small steps; cell size must not set lake-bank
width, forest radius or overall landmark height. These remain independently authored world-space values.

Use **region-guided quantization**: preserve the natural field's broad contours, then resolve
neighboring cells together rather than treating independent rounding as the final result. This is
the proposed algorithm, not implemented behavior.

1. **Sample the source.** For each cell, record area-weighted mean height, sampled minimum/maximum,
   and slope direction from Layer A. Keep these source samples immutable throughout resolution.
   Use a stable sampling pattern so comparisons across seeds and revisions are meaningful.
2. **Assign terrain roles.** Use the regional plan to mark opening areas, ordinary ground, hills,
   peaks/outcrops and planned cliffs. Retain ridge/valley direction, route corridors and active
   hydrology masks. These roles guide adjustments without defining rectangular platforms.
3. **Initialize elevations.** Round each mean height to the elevation increment using an explicit
   deterministic tie rule. This is the starting state, not the final world.
4. **Resolve neighboring heights.** Use bounded deterministic optimization over candidate cell
   elevations. Minimize source-height deviation, accidental isolated spikes/pits, alternating
   high/low patterns and unnecessary boundaries in compatible ordinary terrain. Also penalize
   changes that erase source ridges/valleys or obstruct planned routes. Share connected flat patches
   where the source permits them; preserve successive elevation bands on hills.
5. **Apply feature-specific rules.** Ordinary ground normally permits at most one increment between
   cardinal neighbors. Hill regions permit successive rising bands; planned cliff boundaries permit
   larger jumps. Peak/outcrop masks permit deliberate compact raised formations. A feature mask
   does not exempt every cell inside it from artifact cleanup. At mask boundaries, resolve both
   sides jointly rather than processing regions as independent height islands.
6. **Choose transitions.** Select broad ramp corridors from the route plan and source slope direction
   after resolving elevations. Reserve their actual horizontal run and adjust adjacent cells as
   needed. A one-step difference does not automatically make the whole boundary walkable: remaining
   ledges use blocked-cliff classification and appropriate geometry. Check ramp width, junctions
   and traffic clearance using the shared authoritative surface.
7. **Validate and repair.** Check opening support, actual building footprints, connected usable
   ground and the layout's declared routes. If the ordinary solution cannot provide these, revise
   the affected plan/region rather than smoothing the entire map. Required-resource repair later
   retains final authority over terrain constraints.

For ordinary cells, candidate heights are quantized values within one elevation increment of the
immutable source mean. Measure the limit against the original samples, never the previous sweep's
height; successive updates must not accumulate drift. Feature-specific bounds and planned repairs
may allow larger changes. The ordinary limit is not a reason to invalidate a seed: resource repair
and constructive fallback may exceed it, with overridden constraints recorded.

Define scoring weights, small-feature thresholds and sweep limits in generation definitions.
Use a stable cell visitation order, accept only score-improving changes, and break equal candidate
scores by lower source deviation, then an explicit stable elevation ordering. Stop when no change
improves the score or the sweep budget is exhausted. This heuristic need not find a global optimum;
geometric/playability validation and repair remain mandatory. Do not let worker scheduling change
the accepted heights.

**Safeguard against giant platforms:** there is no reward proportional to flat-region area and no
fixed 2-by-2 or 48-unit pooling. Cleanup targets small accidental features, while source deviation
and ridge/valley preservation oppose flattening naturally rising terrain. Large natural plains
remain valid. Inspect region silhouettes and retained contours, not just the number of height edges.

On identical seeds, compare raw rounded cells with the resolved surface. Record source-height error,
isolated-feature counts, flat-region shapes, ramp corridors, opening footprint capacity and route
connectivity. Review top-down and ordinary strategy views for coherent terraces without ledge
carpets or repeated macro shelves; fewer isolated cells alone is not sufficient evidence.

Classify final dry edges as **flat**, **walkable ramp** or **blocked cliff**. When water is active,
preserve channel, shoreline, basin and crossing masks while resolving cell elevations; general isolated-cell cleanup
must not fill a narrow river or remove a ford. Carve/resolve channel beds and banks during this layer,
then recompute and validate water surfaces against the bed. Terrain remains provisional until
required-resource repair and all generation checks are complete.

Store terrain edges and wet/dry movement transitions as authoritative data consumed by navigation
and mesh construction. A geometrically gentle bank is not automatically a walkable crossing.

```text
seed + selected layout (internal biome/features) + players + supported settings
  -> resolve compatible recipe and active water feature set
  -> reserve openings, expansions and recipe-specific routes
  -> Layer A: biome/layout-shaped natural relief
       wet branch: add active coasts, catchments, basins and crossings
       dry branch: no hydrology reservations or water-driven shaping
  -> Layer B: coherent elevations + terrain edge transitions
       wet branch: resolve beds/banks and finalize water fields
       dry branch: initialize empty water fields without changing terrain
  -> verify provisional hydrology, building space and recipe connectivity
  -> place the allocated required resource fields with bounded ordinary attempts
  -> if needed: repair terrain/access, rebuild affected data and retry placement
  -> if still needed: construct the recipe's guaranteed fallback terrain and field sites
  -> place collidable scenery and validate required access/declared clearing
  -> verify field allocation, openings, expansions and whole-map connectivity
  -> freeze authoritative terrain/water/resources, then add visual decoration and render
```

## 5. Landform vocabulary

| Feature | Visual character | Gameplay role | Placement policy |
|---|---|---|---|
| Plain/lowland | Connected level areas with irregular outlines | Bases, factories, movement and fighting | Dominant around openings and main routes |
| Hill | Coherent elevation bands and broad ramps | Route choices and varied expansion terrain | Multiple approachable sides, outside opening cores |
| Cliff/ridge | Connected rocky spine, shoulders and deliberate escarpments | Landmark, barrier and flank | May be substantial; preserve required access around ends or through saddles/passes |
| Peak/outcrop | Compact stepped rocky mass with shoulders | Landmark and local obstruction | Sparse; clear of starts and essential approaches |
| Valley/pass | Connected lower ground between landforms | Alternative route or expansion access | Real unit and traffic clearance |
| Resource region | Irregular clearing or sheltered pocket | Recognizable economic destination | Budget, approach space and access determine eligibility |
| River/lake/coast | Connected water body with readable banks and shore | Route structure, crossings and landscape identity | Planned with openings, dry expansions and movement domains |

Measure terrain targets on usable dry land, and water coverage separately. A water-heavy recipe
must still supply sufficient absolute dry building area per player; good percentages on a tiny
island are insufficient. Scale landmarks by dry area rather than filling small islands with peaks.

For an initial open-land recipe, experiment with 65–80% of usable land being level or gently
traversable, and at least 60% eligible for representative ordinary building footprints before objects
are placed. These are our tuning targets, not AoE measurements. A point-buildability mask does not
prove a footprint fits. Report overlapping terrain categories separately.

Initially test hills rising 1–3 increments above nearby ground and peaks rising 4–8 increments.
Measure relative relief, not absolute altitude. Start with one principal rocky landmark and a few
secondary outcrops on the smallest open-land map; scale counts by usable area and recipe identity.
Do not use maximum 60-unit relief everywhere simply because the renderer supports it.

Large connected plains are desirable. The rejected platform behavior was repeated rectangular
shelving, not the existence of useful flat ground.

## 6. Ramps, cliffs and junctions

### Explicit transition patches

Construct transitions as explicit patches between flat cell interiors. Resolve transition data
before triangulation: adjoining elevations, flat/ramp/blocked-cliff class, horizontal run, traffic
width, landing extent and movement status. Mesh generation consumes this data; it does not infer
passability from the finished triangle slope alone. A selected transition may reshape several
cells, so its occupied region must be reserved rather than overlapped with another patch.

Walkable ramps must look walkable at normal zoom. For a 2-unit rise, compare a 6–12-unit horizontal
run with the existing 1.5-unit strip. Selected ramps may occupy multiple transition cells; those
cells need not retain flat building cores. The surrounding landscape remains stepped. Do not apply
long transitions everywhere as a smooth-heightfield fallback.

Cliffs retain steep faces with narrow bevels at their lips and feet. Bevels soften silhouettes without
granting traversal. Connected rock shapes distinguish cliffs from dirt/grass ramps. Multi-level rises
use connected bands or explicit cliffs rather than masquerading as ordinary ramps.

Initially use steep cliff faces with a finite horizontal run, avoiding vertical/overhanging terrain
that would make the height query ambiguous. Cliff steepness and bevel widths are authored geometry
parameters; movement remains blocked even where a lip or foot is locally gentle.

### Dedicated corner and landing junctions

At each meeting of transition boundaries, construct one junction patch constrained by all adjoining
heights, edge classes and approach directions. Reserve enough area for its landing and unit clearance.
Do not average corner heights blindly or independently finish each adjoining edge.

| Junction | Required treatment |
|---|---|
| Flat + ramp | Connect the ramp to its flat landing with continuous geometry and sufficient entry/exit space |
| Ramp + ramp | Provide a continuous bend or shared landing where slope and turning clearance permit |
| Ramp + cliff | Preserve the barrier and supply a safe landing or redirect the ramp; never smooth a diagonal crossing into the cliff |
| Cliff + cliff | Join steep faces and bevels without erasing the barrier or leaving a gap |
| Conflicting transitions | Expand the junction region or revise the local transition plan, then revalidate affected routes |

A failed junction cannot be solved by squeezing a ramp into an undersized corner or silently
removing a planned barrier. If a required route needs a different pass, revise the route/feature plan
explicitly. Multi-cell ramps and landings must not overlap incompatible terrain or active water
without the corresponding shoreline/crossing rules.

### Boundary ownership and authoritative geometry

Give shared boundaries canonical ownership and identical positions/subdivisions across neighboring
patches and chunk boundaries. Junctions own their interiors; edge patches terminate at the agreed
junction boundary. Cover the terrain exactly once without holes, overlapping faces or T-junctions.
Rendering may duplicate vertices for hard normals or material seams, but their positions must match.

Rendering, grounding, picking and building support queries use the same final triangles, including
landings, bevels and junctions. Navigation additionally checks declared barriers, slope, continuous
unit clearance, ramp entry/exit and every crossed edge. Visually touching surfaces do not establish
a traversable connection, and corner contact alone cannot grant a diagonal shortcut.

### Verification and implementation order

Start with flat boundaries, straight ramps and simple cliff faces, then add the junction combinations
above. Test rotated arrangements, ramps ending beside cliffs, opposing ramps, multi-level boundaries,
narrow landings, chunk seams and map-edge closures. Check diagonal movement attempts and building
footprints across junctions against the authoritative surface. Inspect pan/orbit/zoom clips for cracks,
sky leaks, grey flashes and shading seams. Replan failed geometry and repeat the affected checks.

### Connectivity without prescribed route counts

Connected-land layouts require ground access between starts and the destinations required by their
resource/progression contract. There is no default of two approaches, no fixed number of generated
paths and no universal strategic corridor width. Local impassable cliffs, water and peaks are valid
provided they do not sever required connectivity. Broad fields can provide access without any
identifiable corridor at all; do not carve lanes merely to make them countable.

Validate clearance using the actual relevant unit footprints and movement domains, including ramp
entry, exit, turns and every crossed edge. A visually open gap narrower than the unit is not a valid
connection. Record bottlenecks and travel costs for quality review rather than enforcing equal
approach widths or counts between players. Island layouts validate local access and separately
require supported transport for their declared inter-island progression. Aircraft cannot excuse
stranded ground workers.

Scenery clearing can open additional connections when its removal mechanics are supported and
reachable. Distinguish immediate access, access requiring implemented clearing and access requiring
future capabilities. Do not count the latter toward essential access in the current playable recipe.

## 7. Openings, buildings and foundations

Reserve a usable opening envelope during generation, then choose the exact command hub location
inside it after provisional terrain and water classification. Starts are not restricted to corners.
Prefer naturally level dry ground; starting on cliff foundations is not the default repair for an
unsuitable map. Enforce the same minimum usability contract independently for each player, without
requiring equal-sized envelopes or equally defensible positions.

Validate this representative early build plan jointly, using current building definitions:

| Building | Current footprint used by the proposed opening check |
|---|---|
| Command hub | 20 by 20 units; half-extents 10 by 10 |
| Alloy processor | 10 by 8 units |
| Fuel processor | 10 by 8 units |
| Charging pad | Circle, diameter 7 units |
| Basic generator | Circle, diameter 9 units |
| Storage silo | Circle, diameter 8 units |
| Drone factory | 14 by 11 units |

These are test placements, not automatically spawned buildings or a mandated player build order.
They must coexist without overlap, retain usable worker exits, spawn clearance and reachable
interaction space, and permit a feasible power arrangement using actual device definitions.
Starting resources must leave usable construction and gathering/delivery space. Keep room for
further development without guaranteeing matching expansion opportunities between players.

Begin tuning with an irregular opening neighbourhood about 55-65 units across. This is a proposed
visual/search scale, not a fixed minimum diameter, mandatory radius or a claim about current JSON.
Acceptance comes from joint legal placements and access checks, not the envelope's bounding box.
Several connected buildable patches, gentle slopes and small outcrops may compose the opening;
do not flatten the whole neighbourhood into one large circular or rectangular platform. Refine the
scale through opening playtests. All dimensions above remain definition-backed when buildings change.

### Definition-backed placement types

Building placement remains independent of cell centers. Each building definition declares a
placement type; tile eligibility and footprint support are separate checks, and both must pass.

| Placement type | Eligible terrain |
|---|---|
| Land | Dry and partially wet tiles; any positive-area footprint overlap with a fully wet tile rejects |
| Water | Fully wet tiles only; any positive-area overlap with dry or partially wet tiles rejects |
| Shore | An explicitly authored mixed land/water contract for that building; no implicit exception to Land or Water rules |

Classify tile wetness from authoritative coverage, not material appearance or altitude. A boundary
touch without positive-area overlap does not count as occupying the adjacent tile. Use consistent
geometric tolerances so classification and footprint clipping agree. Water and Shore structures are
proposed definition capabilities; restoring water alone does not implement those building types.

### Actual footprint support and combined overhang

Clip the actual rotated or circular footprint against final terrain triangles and water coverage.
Measure area in the horizontal footprint plane, not sloping surface area or a count of cells.
A partially wet tile contributes its actual dry flat supported area; it is not counted wholly as
supported or unsupported.

For Land buildings, let A be total footprint area and S the area of the connected flat dry support
region at the placement plane. Require S > 2A/3. The remainder U = A - S must be strictly less than
A/3. Water within partially wet tiles, lower ground, ramps and cliff transitions all contribute to
this combined remainder. Count overlapping reasons once. Separate support islands cannot be added
together to satisfy the connected-support requirement. Traversable ramps and water are not flat
support. Exactly one third unsupported rejects; numeric tolerances must not turn that boundary into
an accepted placement. Fully wet tile overlap still rejects regardless of the area ratio.

### Placement plane and foundations

Choose a horizontal plane that does not intersect terrain anywhere beneath the footprint. The floor
must also clear the highest overlapping water surface by a definition-backed clearance. The dry
support region must reach this plane: raising an ordinary Land building entirely onto stilts cannot
bypass the support rule and would require a separate placement contract.

Foundations extend to existing ground or submerged bed. Each building declares its maximum supported
drop, measured from the placement plane to the ground/bed at its required support locations,
including submerged portions. Reject when required foundation depth exceeds that limit, even if
unsupported footprint area is small. Foundation layout must provide support for accepted overhang;
it cannot simply omit deep portions to evade the depth check.

Construction, demolition, upgrades and loading do not alter elevations, transition geometry,
river beds or water surfaces. Foundations do not flatten terrain or displace water.

Water buildings do not use the Land two-thirds rule. Definitions specify minimum water depth and
whether the building floats or uses bed foundations. Bed-supported Water buildings also declare a
maximum foundation depth; floating structures have their own draft/clearance requirements. Shore
definitions specify their dry/wet requirements, support rule and foundation limits explicitly.

### Reachable perimeter interaction

Buildings have no designated entrances: units can interact from any side. Require at least one
sufficiently wide perimeter segment with space for the relevant unit to stand, interact and leave.
Validate actual unit clearance, interaction reach and a connection to the intended movement domain;
a single reachable point is insufficient. Width and clearance derive from the relevant unit and
interaction definition. Water buildings validate access for their intended interacting units.

Perform the final access and required-route checks with building and foundation collision geometry
included. Foundations must not obstruct the accepted interaction segment or required traffic.
An overhanging floor does not create a navigable bridge unless an explicit building behavior supports
it. Report tile eligibility, support ratio, foundation depth and interaction access as distinct
placement failure reasons.

Validate the minimum early building plan independently at every start while varying outlines,
scenery and approach directions. Record extra capacity without requiring equal capacity across
players. Generation-time shaping is allowed; construction-time terraforming is not.

## 8. Resource distribution and cluster flair

Use the current resource definitions as a baseline in
[Budgeted resource distribution](RESOURCE_DISTRIBUTION_2026-10-06.md) and
`assets/gameplay/resource_fields.json`, with the revised field-count policy below superseding the
current total-field formula. Final non-starting inventories, harvest-node composition and local
concentration tuning are deferred to a separate resource review. Matching old map totals is not
an acceptance constraint for this terrain proposal:

- Exactly **600 Scrap and 400 Oil per player** for opening fields under the current contract.
  Preserve stockpile and scale-setting semantics separately; abundance must not inflate reserved nodes.
- Current non-starting inventories are 1,000 Scrap, 600 Oil and 200 Uranium per field. These are
  baseline figures, not final V2 economy targets. Abundance controls additional field counts;
  future resource definitions will specify their inventories and harvest nodes.
- Current radius-18 local limits are 2,000 Scrap, 1,200 Oil and 400 Uranium, with a 10-unit
  field-footprint gap. Revisit these with node composition and larger cluster geometry.
  Authoritative collision and gathering clearance still require validation.

- Nearby starting access measured by real gatherer travel cost, without a mandatory maximum
  Euclidean radius. Guarantee intended-player practical access; do not require matched travel
  times or paired access ratios. Assess flying gatherer delivery and ground-worker movement separately.

### Fixed openings and abundance-controlled map fields

Allocate resources in two independent parts:

1. **Fixed starting fields:** reserve each player's opening Scrap/Oil fields and their fixed
   inventories first. Abundance never changes these field counts, capacities or access guarantees.
2. **Remaining map fields:** resolve additional field counts per eligible resource family from
   supported map area, the layout's authored density and the selected abundance setting. These
   fields provide expansions and contested resources. Increasing abundance increases their count
   monotonically, subject to discrete rounding; it does not multiply starting supplies.

Calculate the additional count directly. Do not calculate a total count and subtract starting
fields, as the current implementation does. Starting fields count in final inventory accounting
and local concentration checks, but do not consume the abundance-controlled map-field allocation.
Once field inventories are defined, the final budget per family is fixed opening inventory plus
additional field count times its configured field inventory. Record these two contributions separately.
Fixed allocations do not fix field coordinates; seeded placement chooses legal sites.

Define the density basis, abundance multipliers and deterministic rounding per layout/resource
family. Resolve counts before shaping terrain; candidate availability must not determine supply.
Additional fields are seeded across eligible map regions, not allocated as equal or paired player
expansion batches. Account for starting fields separately from the remaining map allocation.
Explicit resource exclusions determine eligible families and sites. Additional counts may be zero when the declared settings resolve that way; there is no
implicit minimum of two economic expansions per player. Validate any progression-critical supply
in the authored layout contract rather than adding fields during repair.

Reserve enough eligible placement capacity for the allocated fields, then choose additional field
locations through seeded terrain-aware placement. Their travel distance, exposure and ownership
opportunities may differ between players; expansion placement is random rather than matched.
Validate field access and usable infrastructure space without treating any shared field as a second
starting guarantee. Extra buildable destinations may exist without resources. Every resolved field must be placed, including at low abundance; repair may reshape
terrain or move fields but cannot change their resolved counts or inventories.

This changes the current field-count semantics and may increase non-starting supply with unchanged
density values. Recalibrate density and abundance mappings explicitly against current map budgets
and playtests before adopting them; do not silently claim economy equivalence.

Starting quantities alone do not make an inaccessible opening usable. Rank candidates by practical
starting-resource travel and route simplicity, report long-route outliers and repair unusable
opening access after bounded placement attempts. Do not enforce cross-player travel equality.
Treat new quality scores as preferences until supported by playtest timing; do not silently introduce
a fixed distance limit.
Use the movement domains and costs of the actual relevant units. Check ground-worker access
independently of flying gatherers.

Biome exclusions determine eligible resource sites. For example, an ocean region may forbid
Uranium completely; repair must place its allocated field on eligible land, never relax that ban
or delete the allocation. A layout may exclude a resource from the whole map only if its explicit
economy/progression contract does not require it. Otherwise the authored recipe is incompatible
and must be corrected before exposure in setup. Optional resources may be absent where excluded.

### Required resources: attempts, terrain repair and guaranteed fallback

**Every seed must produce a valid map for every supported recipe/settings combination.** A resource
placement failure triggers generation repair; it cannot invalidate the seed, silently switch to V1,
drop a field, inflate supply or start an unlimited search. This is the new target contract, not
implemented behavior.

Required resources mean every field in the resolved allocation: fixed player openings, configured
expansion fields and required contested inventories. Optional visual debris is not part of this
guarantee. Resolve the allocation and reserve sufficient placement capacity during regional planning,
before terrain detail is generated; exact node positions still come later.

Use a bounded, definition-backed escalation sequence. An initial tuning proposal is **three ordinary
placement rounds, then at most two local repair batches**, followed by the constructive fallback.
These are proposed generation controls, not existing JSON values.

1. **Ordinary placement.** Try deterministic candidate sites on existing eligible terrain. Check
   the field's configured inventory and placement rules, intended-player access and real gathering/
   delivery approaches. Record the constraints preventing placement. Keep successful fields stable
   where practical; failed trials cannot leave partial nodes in the world.
2. **Local terrain repair.** After the ordinary attempt budget is exhausted, reshape terrain near
   the deficient opening or field region: widen a clearing, form an irregular shelf, ease a ramp,
   create a dry approach, or adjust a bank/channel. Resource repairs have final say over terrain
   generation constraints, including source deviation, relief ranges, feature masks and water
   coverage targets where necessary for legal placement and access. Prefer
   the smallest change that resolves the failed constraints. Do not add resources or use a repair
   to invent additional fields. Inventory and concentration tuning is a separate resource task.
3. **Rebuild and retry.** Apply each repair transactionally to provisional generation data. Recompute
   affected terrain triangles, water coverage/depth/flow, shore tags, support masks, navigation and
   access costs. Hydrology changes may propagate through an entire catchment/body; rebuild that
   dependency region rather than assuming a fixed local radius is sufficient. Recheck existing
   starts and fields as well as the newly placed inventory. Roll back unsuccessful repairs.
4. **Constructive fallback.** If the local repair budget is exhausted, use a deterministic,
   recipe-specific construction that provides sufficient dry sites, required routes and separation
   for the complete allocation. It may reshape larger regions or move provisional starts/fields.
   Preserve the original seed, resource eligibility exclusions, supported movement topology and
   validated building opportunities. Keep biome and water-feature identity where compatible;
   their shape and coverage cannot veto required resource repair. An island recipe can enlarge/reform islands;
   it cannot silently turn into connected plains. Revalidate the whole map after construction.
5. **Finalization.** Commit entities and freeze terrain, hydrology and resource placement only after
   the complete map passes its mandatory checks. Include collidable scenery before final access
   validation; purely visual decoration can consume the frozen result afterward. Building
   placement, construction, demolition and loading cannot invoke these terrain-repair operations.

The constructive fallback must be feasible by construction, not another random retry loop. Each
supported recipe/settings range needs a capacity envelope and fallback arrangement covering player
count, building clearance, all allocated fields, configured placement limits, land/water connectivity and
water occupancy and supported traversal. Resource repairs may override ordinary water coverage
targets. Validate this feasibility when defining supported setups. Incompatible
settings are configuration errors before generation, not bad seeds discovered after allocation.

Required inventories, legal placement and playable access take priority over exact source contours,
decorative landforms, terrain shape limits and water coverage targets. Resource repairs have final
say over those terrain constraints; they cannot override resource eligibility or create unsupported
movement. Record overridden constraints and revalidate all affected systems. Reserve enough freedom in each recipe to
repair those softer properties while retaining its defining topology. If a supported setup's fallback
cannot satisfy its hard constraints, that is a generator defect; do not present it as an invalid seed
or silently relax the resource guarantees.

Derive candidates and repairs from stable stage/field identifiers under the original seed. Record
repair reasons, modified regions and fallback use for diagnostics. Saves and replay checksums describe
the final repaired terrain and water. Include fallback frequency, repair area and generation cost in
the audit so the guarantee does not conceal a poor ordinary generator.

### Large clusters composed of small nodes

Separate three scales: a **region** of economic interest, a **field** with fixed inventory, and
individual **harvest nodes**. A large salvage region may contain several separated fields and open
lanes. Regional grouping does not create extra allocated fields. Final inventory and concentration
rules will be resolved alongside harvest-node composition.

A scrap field should have an irregular core, subsidiary piles and scattered edge nodes. Use elongated
or multi-lobed shapes, uneven spacing and gaps instead of uniform circles or tile carpets. Compare
larger visual envelopes with the current 5–8-unit field radii, keeping openings easy to approach.

Harvest-node counts, capacities and inventory composition are deferred to the resource review.
Visual cluster design does not decide a new harvest-node recipe. More simultaneous gathering
positions can strengthen an economy, so review throughput when addressing node composition.

Provide several small-pile silhouettes, medium piles and large wreck forms, with seeded rotation
and size variation. A giant heap may comprise several individually selectable small nodes around
a shared visual mass. Resource-bearing silhouettes must correspond to harvestable inventory and
visibly deplete. Inert debris must look clearly non-harvestable and remain unselectable.

Oil uses coherent pockets, extraction nodes and recognizable stained ground or piping. Uranium
remains distinct and contested where required. Peaks do not automatically grant bonus supply.
Biomes influence placement and appearance, never remove guaranteed resource families.

Place nodes against provisional terrain and hydrology, on accessible dry plains, clearings or shelves.
Finalize these surfaces only after the required-resource placement and repair procedure succeeds.
Keep opening supplies in their intended player's reachable land component. Place random map
fields where the recipe's available movement/transport can reach them; do not strand required
fields on unoccupied islands or turn this access rule into paired expansion guarantees.
Flying-gatherer access alone cannot validate a
ground economy or military route. Offshore resources, fishing and new water economies are separate
features and do not replace guaranteed Scrap/Oil. Validate gathering,
landing where relevant, and delivery approaches rather than node-center slope alone. Cluster scenery
must preserve node visibility, field separation, essential lanes and building space.

## 9. Recipe-controlled water generation

### Dry generation is a complete terrain path

A forbidden-water recipe, disabled compatible variant, or optional recipe selecting no water skips
hydrology planning and shaping entirely. Do not generate a wet map and drain it afterward.

Skip sea-level coast shaping, ocean/island masks, channel carving, hydrological basin filling,
bank/floodplain flattening, wetland shaping, crossing reservations and water-specific opening
setbacks. Do not reserve dry building space around nonexistent shores, shift starts to avoid
hypothetical flooding, or lower ordinary terrain toward an unused sea level. Naturally low areas
and valleys remain legitimate dry land.

Initialize canonical empty water data: zero coverage/depth/flow, no body/reach identity, no submerged
or shoreline tags, and no water navigation cells. Water surface values cannot turn low land into
wet terrain when occupancy is absent. Select ordinary dry biome classifications and skip water
meshes and wet-only decoration. Climate moisture may still influence dry vegetation and materials;
it does not implicitly spawn water or water-specific terrain shaping.

For the same seed and resolved dry recipe, changing inactive sea levels, river widths, lake settings
or hydrology quality must leave terrain heights, edges, starts, resource positions/inventories and
movement unchanged. Skip water random streams without perturbing unrelated streams. Compare this
with a direct dry-generation baseline, not with a wet map whose water renderer was turned off.
Inactive hydrology parameters should not invalidate an otherwise identical dry map fingerprint.

### Hydrology and the stepped bed — active water features only

Reuse the architectural separation described in [Terrain and World Generation](TERRAIN_GENERATION.md):
a drainage graph, connected reaches, explicit local surfaces and coverage, and separate wetland
masks. Its older smooth-bed implementation is a reference, not a ready-made V2 solution.

On a wet recipe, Layer A proposes drainage from the natural contours. Layer B resolves the stepped bed while retaining
those channels and basins. Final hydrology then verifies connectivity, depths and outlets against
the actual square-cell geometry. If quantization dams a river, dries its junction or floods an opening,
repair the candidate bed within channel masks or replan before terrain acceptance. Do not route on
one surface while rendering and navigating another.

- Oceans use one stable level and connected ocean occupancy. Inland low ground does not become
  ocean merely because its altitude is low; distinguish enclosed basins from sea-connected cells.
- Lakes have a coherent local level and defined basin/spill relationships. Resolve closed basins
  consistently with the recipe's drainage policy; do not assign arbitrary levels per cell.
- River reaches have continuous coverage and non-increasing downstream surface levels. Tributaries,
  confluences and estuaries share a consistent junction solution, without cracks or uphill joins.
- Grid-shaped beds may lie below a continuous water surface. Water heights need not be quantized
  to the land's authored elevation increments. Banks retain the stepped land character.
- Significant drops need an explicit cascade/waterfall transition or a replanned channel. Falling
  water is not a sloping navigable lake, and decorative waterfalls cannot conceal disconnected
  authoritative occupancy. Waterfall art may follow after valid channel transitions exist.
- Preserve valid narrow streams using channel masks and adequate water-mesh sampling. If a channel
  is too narrow for authoritative movement clearance, widen it or classify it as non-navigable;
  a visible ribbon of water does not imply a vessel corridor.

Store water-body/reach identity, kind, surface height, coverage, depth, flow direction and shore/bank
masks on the final surface. Flow animation, foam, reflections and waves consume this data and do not
change passability. Start with static water occupancy: tides, seasonal flooding, fluid simulation and
player-built dams are outside this revision. A wetland tag alone does not imply open water.

### Crossings and movement domains

Shallow-looking water is blocked for ordinary ground units unless an explicit crossing rule permits
it. Reserve river crossings with the route graph: test bank ramps, channel bed, configured wading
depth, continuous unit clearance and approach space. Until wading is implemented, provide actual
dry routes around water, or repair/reconstruct connected-land candidates that depend on unsupported
fords. Unsupported recipe/settings combinations are excluded before seed generation.

A narrow land strip must be genuinely dry and wide enough for the intended traffic. Never create an
invisible crossing in navigation or add a bridge prop without a supported bridge system. Bridges,
naval units and transports are not assumed to exist simply because water generation returns.

Validate land, water, amphibious and air graphs against their actual movement profiles. Water-only
units, when available, need minimum depth, channel width, valid junctions and usable shore access.
Cliff bevels cannot connect water bodies at different levels into a fake navigable passage.

Connected-land recipes preserve planned ground routes around lakes/coasts or through implemented
crossings. Central Water preserves connected surrounding land and must not strand a start behind
its basin; there is no paired-route or equal-width requirement. Archipelago deliberately has separate
land components: every starting island needs its
complete opening economy and viable local infrastructure. Allocate accessible early expansion
fields from the abundance-controlled map allocation according to the layout's declared progression
contract; do not impose an additional universal expansion quota. Cross-island progression requires supported movement/transport and a
verified build path. Until that exists, keep archipelago out of normal competitive generation or
label it as a water-layout preview; do not treat aircraft alone as complete island gameplay.

### Shores, resources and visual integrity

Reserve shore setback and dry expansion room using actual building and node footprints. Scrap and
Oil cannot be stranded across an unusable channel or placed partly under water to meet a quota.
Use the fixed-opening plus additional-field allocation in section 8; final concentration tuning is deferred. Repair insufficient dry placement space
through the procedure in section 8, rather than squeezing fields into overpowered shore clusters,
increasing supply or invalidating a seed.

Generate distinct beaches, muddy banks, rock shore cliffs, floodplains and wetland vegetation from
water/terrain masks. Avoid uniform square moats around starts. Peaks may form headlands and islands
where the recipe permits them, without consuming required opening land.

Water uses a dedicated surface mesh or a demonstrated equivalent that shares coverage boundaries
with the bed. Do not blindly reuse V2 terrain triangles as a water surface: corners, cliffs and
changing reach heights need verified ownership. Match water and terrain culling, fog and active-map
bounds. Test shoreline z-fighting, sky leaks under banks, dry holes at confluences, stretched foam,
moving-camera grey flashes and near/far mesh changes.

## 10. Layout and biome identity

### Shared capabilities versus layout choices

The concept images illustrate individual recipes, not one mandatory landscape that every layout
must reproduce. Keep three responsibilities distinct: shared generator capabilities and validation,
authored per-layout settings, and example tuning for a particular layout. Players select layouts;
internal profiles and reusable features remain authoring tools.

| Area | Shared capability or invariant | Authored per-layout choice |
|---|---|---|
| Usable terrain | Validate actual openings, allocated resource access and supported movement | Plains/hills/cliff proportions, opening envelopes and strategic terrain pattern |
| Routes | Validate required connectivity and actual unit clearance; no default approach count or prescribed corridor width; no roads | Natural passage arrangement, bottlenecks and supported land/island topology |
| Water composition | Resolve active water against the final bed; dry recipes skip hydrology | Forbidden/optional/required water, feature types, count, shape, coverage and connectivity |
| Shorelines | Reusable shallow-bank, beach, rocky-bank and cliff-shore profiles with consistent wet geometry | Profile mixture, location, transition width and depth character |
| Terrain appearance | Cells determine geometry; equal-height surfaces do not require visible grid lines | Materials, climate, vegetation and feature appearance through internal profiles |
| Scenery | Seeded grouping, collision-aware access checks and declared clearing support | Density patterns, grouping scales, preferred regions and local clearings |
| Resource clusters | Reusable region/field/visual composition and legal placement checks | Eligible sites, regional distribution and additional field counts driven by abundance |
| Shore construction | Land/Water/Shore placement and foundation validation from section 7 | Whether the layout's gameplay requires suitable shoreline construction opportunities |

Route corridors are open traversable terrain, not a generated road network. They constrain relief
and obstacles without painting continuous path strips. Natural soil patches and bare ramp materials
may occur, but must not become automatic roads connecting bases and deposits. A future road system
would be a separate feature.

Use stepped geometry and material transitions to communicate elevation; do not bake a grid overlay
into ordinary terrain textures. Provide debug grid visualization separately. Shoreline profiles and
scenery patterns are reusable options, not mandatory stages on dry maps or universal scenery rules.

### Example layout directions

Example recipe directions below are proposed, not changes to current layout definitions:

See the [layout concept gallery](pictures/terrain-v2-layouts/README.md) for an example image of
each combination below. These generated illustrations communicate visual direction and topology;
they are not engine captures or verification of cell geometry, resource counts or placement rules.

| Layout/profile combination | Terrain character | Water policy example |
|---|---|---|
| Open plains / arid | Broad dry buildable land, low hills, scattered outcrops | Forbidden; no coast, channel or basin shaping |
| Open plains / temperate | Open fields with grouped vegetation and gentler hills | Optional lakes/rivers, including a zero-water outcome |
| Rolling hills / upland | Coherent rising bands, valleys and rocky shoulders | Dry or optional valley rivers, as explicitly configured |
| Continental / coastal | Large land regions, headlands and substantial coasts | Ocean required for this variant; inland features separately controlled |
| Central hill / dry | Accessible central elevation with surrounding expansions | Forbidden unless an explicit wet variant is selected |
| Central water / lake region | Principal central basin with connected surrounding land | Central lake required; no automatic ocean |
| Archipelago / island profile | Several viable islands and constrained rocky landmarks | Ocean required; island progression rules apply |

These combinations describe internal authoring choices; players select the resulting layout.
Layouts own differences in strategic terrain and topology. Biome profiles mainly distinguish
materials, vegetation and resource-region appearance, with permitted relief and feature variation
constrained by the layout. Developers can reuse profiles without exposing a separate biome selector.

### Ridge and landform references

Use the [Central Water implementation concept](pictures/terrain-v2-layouts/central-water-implementation-no-roads.png)
for stepped grass shoulders, exposed cliff sections, compact peaks and broad clearings. Use the
[main-menu terrain reference](../assets/textures/ui/terrain_generation_concept.png) for branching
mountain spines, uneven peaks, rocky foothills, saddles and expansive valleys. These images guide
feature structure and visual composition, not literal heights, cell sizes or resource placement.

Ridges may be substantial and connected where the layout permits them; do not impose a universal
short-escarpment limit or isolated-peak ring. Combine rocky spines with lower shoulders and valley
space. Vary ridge height, length, branching and location by seed while preserving required movement
around their ends or through valid saddles/passes. Retain fine coherent cells and readable ramps;
large-scale feature variation must not restore oversized block platforms or erase ordinary lowlands.
Keep each layout's own balance of open land, hills and rocky masses. The natural palette direction
can evolve without copying the illustrated grid lines or turning every height change into dark rock.

### Central Water — example recipe direction

Use the [revised implementation concept without roads](pictures/terrain-v2-layouts/central-water-implementation-no-roads.png)
as visual guidance for this layout only. It is generated illustration, not exact cell geometry,
proof of placement legality or an authoritative resource distribution.

| Parameter | Central Water example |
|---|---|
| Strategic topology | One connected land component around a principal central lake; no required water crossing |
| Active water | Required enclosed lake; no automatic ocean or river stage |
| Lake coverage | Begin near one fifth of total map area as a soft visual target; usable openings, allocated resources and access govern the final result |
| Lake shape | Irregular connected outline with bays; optional small islands, without required fields stranded on inaccessible islands |
| Terrain character | Broad connected lowlands around the lake; coherent outer hills, limited cliff sections and sparse compact peaks |
| Opening access | Connected surrounding land with valid ground access; no fixed number of approaches or matched route widths |
| Shore mixture | Predominantly low stepped banks and partially wet cells, with selected rocky sections; neither a uniform cliff ring nor an identical beach width everywhere |
| Scenery pattern | Grouped outer vegetation, hill-shoulder groups and selected shore pockets; clearer openings, resource approaches and movement corridors |
| Resource appearance | Irregular salvage clearings with large wreck silhouettes and smaller surrounding piles; oil pockets on eligible accessible ground |
| Shore building opportunities | Supported where eligible geometry occurs; do not automatically spawn the illustrated shore building or require such sites unless declared by the layout contract |

Validate every start's usable construction envelope and fixed nearby resources independently.
Starts may occupy suitable positions beyond the illustrated corners. Do not require comparable
opening area, approaches, elevation or expansion opportunities, literal symmetry or a circular ring.
Terrain/resource repair can change shape and coverage
under section 8's priority rules; record deviations from the example targets. Resource inventories,
harvest-node composition and final concentration tuning remain deferred.

Other recipes may intentionally depart from this image: dry plains have no shore processing,
uplands may contain more relief and restricted passes, coastal recipes use sea-connected water, and
archipelago recipes retain separate islands. Do not impose Central Water's lake coverage, low-bank
mixture, outer vegetation pattern or connected-land route graph on those layouts.

For example, arid profiles can favor open ground and rocky outcrops; temperate profiles can favor
gentler connected hills and vegetation clearings; rocky upland profiles can favor ridges and cliffs
while preserving opening space. These are authored profile choices, not universal rules that
deserts forbid water or temperate terrain always contains rivers.

Wet recipes declare feature-specific water ranges and minimum absolute dry opening/expansion area.
Dry recipes have no water-related terrain constraints. Disabling a required central lake or ocean
does not automatically preserve that layout's identity; offer a named dry variant with its own
terrain plan where useful.

Biome profiles shape terrain generation as described above; local biome labels then control region
palettes, surface materials, vegetation groups, resource preferences and explicit site exclusions.
Texture blending and sparse decoration can soften material boundaries without changing geometry.
Avoid dark rock on every small height change: plains, hills, cliffs and peaks must remain distinct.

Group trees, shrubs, rocks and debris with clearings. Lower decorative density at ramp entrances,
resource approaches and key sightlines. Ground props on the final surface; decoration must not hide
gaps or introduce untracked path blockers. Scenery is intended to become destroyable. Until a
supported clearing mechanic exists, destruction cannot be assumed to make required access usable.
When implemented, layouts may declare clearable obstacles and verify the units/tools and initial
access needed to remove them. Distinguish immediately usable routes from routes requiring clearing.

## 11. Determinism, rendering and repair

Use stable named streams for layout, features, hydrology, starts, resource families and decoration. Changing
a scrap mesh variant must not move starts or alter inventories. Cache region and transition data.

Retain existing rendering safeguards: actual frustum culling, inactive-chunk exclusion, closed active
map skirts, outward winding, finite normals, cliff texture projection and mesh/query agreement.
Mesh simplification must preserve transition silhouettes and cannot reuse V1 indices on V2 buffers.

Expose region IDs, source/final heights, feature masks, edge classes, flat support, unit-clearance
corridors, catchments, reaches, water depth/coverage, crossings, test building layouts, inventories
and travel costs in debug views. Record accepted seeds,
generation revision, candidate rejection reasons and terrain repair reasons. Include the resolved
recipe and parameter provenance described in section 4 so diagnostics explain which authored rule
constrained each result.

Bound ordinary attempts and local repairs, then use the guaranteed constructive fallback in section 8.
Terrain remains provisional through required-resource placement, including any water and navigation
rebuilds. Only the validated complete map becomes immutable. Keep the original seed throughout;
never discard it because ordinary placement failed. Do not add inventory, return partial worlds or
silently switch to V1. Cancellation may stop generation cleanly; it is not a seed-validity failure.
Measure repair/fallback frequency and loading cost instead of increasing searches indefinitely.

Keep the V1/V2 setup toggle and mode restoration. V1 remains the default for old profiles. The new
recipe needs its own generation revision and topology fingerprint, including transitions and
authoritative definitions, water-body levels, occupancy and movement classifications. Existing V2 saves must retain their generator or reject before world
replacement. Never regenerate old buildings onto different terrain.

## 12. Implementation sequence

1. **Baseline and diagnostics:** record current maps, support masks, starts, access costs, inventories,
   loading times and moving-camera clips.
2. **Recipe resolution and regional planning:** implement compatibility and water policy, resolve
   resource budgets, and validate constructive fallback capacity for supported setups. Reserve
   candidate openings anywhere suitable, map-field capacity and required connectivity. Jointly
   compose seeded ridges, valleys, lowlands and passages; add water reservations only when active.
   Shape Layer A through the resolved biome profile and layout constraints. Keep shared capabilities
   separate from layout tuning as specified in section 10; route reservations must not generate roads.
3. **Coherent cells:** implement connected elevation bands and isolated-cell suppression; verify
   retained contours and absence of repeated macro shelves.
4. **Conditional hydrology and transitions:** validate the independent dry path first. For wet recipes,
   resolve permitted beds/banks and water surfaces against Layer B. Implement broad ramps, cliffs,
   wet/dry edges and supported crossings. Verify applicable depth, flow and per-domain clearance.
5. **Openings:** validate the proposed joint opening build plan at each start, usable exits, island self-sufficiency
   and strict immutable foundations. Validate placement types, clipped support area, foundation
   depth and perimeter access; exclude layout/settings combinations requiring unavailable transport before generation.
6. **Required-resource repair and clusters:** implement bounded placement attempts, transactional
   terrain/water/access repairs and recipe-specific constructive fallbacks. Force ordinary placement
   failures in tests and verify complete inventories for the original seed. Then enlarge cluster
   composition and add small-pile art; review gathering concurrency before changing node recipes.
7. **Biomes and landmarks:** tune peak/cliff density, grouped scenery, material exposure and readability.
8. **Global acceptance:** run the matrix below, repair failures and repeat affected checks before
   replacing the current V2 recipe.

Each stage records what is missing, what is improvable, what changed and how it was verified.
A failed stage returns to implementation. Visual approval cannot excuse unusable buildings or supplies.

## 13. Acceptance criteria

| Area | Required evidence |
|---|---|
| Appearance | Coherent openings and hills; ridge/valley structure informed by both references; no ledge carpets, forced zigzag lanes or repeated square macro shelves |
| Movement | Exact mesh/query agreement, no corner shortcuts, real clearance, valid wet/dry transitions and recipe-specific land/water routes |
| Recipe identity | Setup exposes layouts only; internal profiles obey layout constraints; numeric ranges intersect deterministically; incompatible definitions/settings are excluded before generation; zero-water outcomes use the dry path |
| Layout variation | Multiple seeds produce distinct large-scale landform arrangements within each layout identity; starts are not restricted to corners; larger maps add spaces/features rather than uniformly enlarging them; Central Water patterns are not imposed globally |
| Dry independence | No water-driven shaping or reservations; empty wet fields; changing inactive hydrology settings leaves authoritative dry generation unchanged |
| Hydrology | When active: connected coverage, non-increasing river levels, consistent confluences/outlets, coherent lake/ocean levels, no flooded openings |
| Water layouts | Adequate dry opening space at each start, valid required crossings, accessible allocated fields and supported cross-island progression; no cross-player route equality |
| Buildings | Joint early building layouts; Land/Water/Shore eligibility; actual clipped connected support; combined overhang thresholds; depth limits; reachable perimeter with foundation collision |
| Resources | Fixed opening allocations, abundance-controlled additional field counts, site eligibility, gathering/delivery access and intended-player opening access; totals/node tuning deferred |
| Economy | More small nodes introduce no unreviewed opening throughput advantage; compare actual gathering and delivery |
| Starting guarantee | Fixed per-player opening Scrap/Oil inventories and nearby practical availability; independently usable starts; expansions and other strategic advantages need not be equal |
| Rendering | Reviewed pan/orbit/zoom clips at shores, confluences, estuaries, ramps, cliffs, peaks, foundations and every map edge; no gaps, grey flashes, water seams or disappearing chunks |
| Reliability | Every seed succeeds for supported setups; deterministic repairs/fallback, clean cancellation, no partial world, immutable post-generation terrain, saves and V1/V2 restoration |
| Cost | Compare generation time, bounded attempts, repair area, fallback frequency, triangle/node counts and frame times with the baseline |

Begin with the six layouts and 10/15/20 sizes, two and four players where supported, low/normal/high
abundance and several recorded seeds. Exercise both water-enabled and dry variants where each recipe
supports them, plus developer-authored layouts using different internal biome profiles. Test empty
range intersections and incompatible water policies as definition/setup errors. Test dry layouts
with river-preferring profiles, required-water layouts, and steep profiles requiring resource repair.
Assert that ocean Uranium exclusions remain enforced while required inventories relocate to eligible
land; changing cosmetic variation must not alter budgets or strategic topology. Authoritative landform
seed variation may change geography and start/expansion locations within the layout contract.
Verify parameter provenance and repeatable resolved recipes for every case.
Compare forbidden-water, optional zero-water and direct dry-generation cases; perturb inactive
hydrology settings and assert identical authoritative dry results. Include ocean coasts, inland
lakes, rivers, confluences, estuaries, cliffs at shores,
quantization-induced dams, valid/invalid crossings, narrow channels and island openings. Test that
water visuals and navigation agree, disabling water clears all wet fields, and saves restore the
same water geometry and classifications. Include rotated footprints and resources beside banks. Verify combined cliff/water overhang below,
exactly at and above one third; validate interaction access from any usable building side. Test
Land rejection on fully wet tiles even below the overhang allowance, Water rejection on dry/partly
wet tiles, boundary-only contact, disconnected support patches, water clearance, and foundation
depth just below/at/above its configured maximum. Include narrow perimeter access, valid access
from different sides, floating versus bed-supported water structures where implemented, and
foundations obstructing previously usable interaction space or routes. Test
openings in varied non-corner locations, open fields, single passages and multiple passages without
prescribed counts. Reject blocked required connectivity, including undersized gaps, and verify
scenery clearing when implemented. Future removal must not mask current essential-access failures.
Force ordinary placement exhaustion, failed local repairs and full constructive fallback on both
dry and wet recipes. Assert fixed opening allocations (not coordinates), resolved field counts,
the original seed, valid existing fields/openings, deterministic final checksums and bounded execution. Test capacity-envelope limits
and reject unsupported configurations before generating terrain. Finite seed sweeps supplement,
but cannot replace, a feasible constructive fallback for the every-seed guarantee.

Inspect whole maps and inhabited openings at ordinary strategy zoom, top-down and close views,
then move through them in the renderer. Exercise harvesting, delivery, representative construction
and access to the expansions actually allocated by the layout and abundance setting, using movement
capabilities available in the tested recipe. Verify fixed opening inventories across abundance
settings, monotonic additional field counts with documented rounding, and
successful placement when additional counts are zero. Historical prototype checks do not validate
this proposal.

Review a recorded multi-seed gallery per layout at matching scales. Compare ridge arrangements,
lake proportions/bays, lowland shapes, clearings and start locations, not merely different terrain
fingerprints or rotated copies. Record building capacity, ground and drone travel, bottlenecks,
resource exposure and useful elevation as quality diagnostics; only starting-resource guarantees
and independent usability are enforced, not equal strategic positions. Include actual movement,
construction, gathering/delivery and opening playtests alongside visual pan/orbit review. Record
what is missing, improvable and changed; failed mandatory checks return to repair and affected
verification. Run the complete layout/size/settings matrix and repeat global review after failures
are corrected. Numerical connectivity and attractive screenshots alone are insufficient.

The first priority is **usable connected land with memorable features**. Increase terrain
density and resource spectacle only while these checks keep passing.
