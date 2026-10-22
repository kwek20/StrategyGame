# Field-level resource composition — revision 7

Actual Release renderer captures, Central Water seed 7, 10 by 10 chunks. These are engine output.

- `starting-scrap.png`: many small individually harvestable piles, fixed 600 total supply.
- `expansion-scrap.png`: broader composition with pockets, larger frames and scattered small piles.
- `starting-oil.png`: small well/seep/equipment targets sharing a fixed 400 supply.
- `expansion-oil.png` and `oil-gameplay-zoom.png`: individual equipment and seep targets at two zooms.
- `uranium.png`: ten small host-rock exposures with 200 total supply, viewed from the lake side.
- `overview.png`: fields in the surrounding terrain and vegetation.

Each field chooses node variants and positions from deterministic streams. Its total supply is
unchanged; targets deplete independently. Hovering a node outlines currently visible live field
members. Field membership and remaining amounts survive save/load. Ground treatment cannot join
unrelated explicit fields, and exhausted nodes leave scars while their resource entities remain.

The full 33-test Release suite passed, including five-layout generation matrices, forced fallback,
exact opening/expansion budgets, field membership restoration, final ground routes, legacy V1
behaviour, asset imports and renderer integration. Follow-up delivery/renderer checks also passed,
including collecting across a partly depleted small pile into the next target without losing cargo.

The GTX 1070 render-only probe at 2554 by 1389, 20 by 20 chunks, seed 7 and 1,134 resource targets
averaged 8.52 ms over 100 measured frames after 20 warm-up frames. Node-buffer uploads, site-tile
uploads and vegetation rebuilds were zero during measurement. World GPU time averaged 0.215 ms;
site preparation CPU time averaged 0.108 ms. This excludes simulation, UI and swap, and does not
establish full-game FPS or unchanged collection rates. The gallery run also exercised 80 moving
camera frames without GL errors. Hovering every field member's outline was not part of the probe.

Remaining limits: fixed small model variants, circular node collision, and whole-node disappearance
rather than continuous erosion of each fragment. New/resurrected/moved sources rebuild the resource
instance allocation; ordinary depletion/removal updates only affected field ranges. Effective
income timing needs gameplay balance testing because field traversal and depletion frequency change.
