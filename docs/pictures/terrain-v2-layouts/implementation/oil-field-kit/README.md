# Oil field kit — engine review, 2026-10-21

Actual Release renderer captures: Central Water, seed 7, 10 by 10 chunks, generation revision 6.
These are engine output, not generated concept images.

- `starting-field.png`: fixed starting supply, low wellheads and seep surfaces.
- `expansion-close.png`: idle pump landmarks, horizontal tanks and stained ground.
- `gameplay-zoom.png`: expansion field at a wider camera distance.

The first review showed excessive grey sheen. The final material reduces the highlight strength
and keeps the crust warm and dark. Large deposits use collapsed pump frames; the medium deposits
in this expansion capture use idle pumps. All three GLBs pass the asset importer.

Release renderer integration passed with 22 terrain/inhabited captures and 80 moving-camera frames.
The opt-in GTX 1070 render-only probe at 2554 by 1389, 20 by 20 chunks and 108 resource targets
averaged 8.55 ms over 100 measured frames after warm-up. No measured site uploads or vegetation
rebuilds occurred. This excludes simulation, UI and swap; it is not a full-game FPS measurement.

The site cache, render architecture and asset import checks also passed. No resource budgets,
harvest target counts, collision radii, generation streams or V1 presentations changed in this pass.
