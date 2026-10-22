# Sparse decorative resource fringe

Actual Release renderer captures, Central Water seed 7, 10 by 10 chunks, generation revision 7.

- `scrap.png`: small metal flakes and pipe fragments outside individual harvest targets.
- `oil.png`: sparse pipe fragments and stone chips around the cracked-ground Oil field.
- `uranium.png`: plain stone chips around the harvestable mineral exposures.

Each node has a 35% chance of requesting one or two fragments; unsuitable placements are skipped.
They use 20–35% model scale and remain deliberately secondary to the harvestable targets. They
are not world entities and add no resource inventory, collision or picking target. Construction
clears them through the existing vegetation field, and cached instances update locally on removal.
They can remain after the source is depleted. No generation or save revision changes are needed;
reloading a V2 match regenerates the decorative layer.

The renderer review completed 22 captures and 80 moving-camera frames without GL errors. The
GTX 1070 render-only probe at 2554 by 1389, 20 by 20 chunks and 1,134 resource targets averaged
8.71 ms over 100 measured frames after warm-up. Vegetation rebuilds, site uploads and node-buffer
uploads were zero during measurement. This excludes simulation, UI and swap.

Regression coverage checks sparse dry placement across all five layouts, fixed resource budgets,
seeded reconstruction after depletion, and nonsequential resource instance IDs. The latter fixes
a presentation identity mismatch that could previously remove a different resource model on depletion.

All 33 Release tests passed, including the five-layout opening and resource matrices, save/load,
V1 compatibility, vegetation clearing and renderer integration.
