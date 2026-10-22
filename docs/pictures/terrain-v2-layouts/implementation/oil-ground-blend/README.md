# Oil ground blend

Actual Release engine captures, Central Water seed 7, 10 by 10 chunks, generation revision 7.

- `expansion.png`: visible cracked earth, feathered into the local ground beneath the Oil field.
- `starting.png`: the same material on starting Oil supplies.
- `gameplay-zoom.png`: the expansion field from the wider gameplay camera.

The old near-black soil replacement is removed. The shader combines the underlying terrain and
local soil with a photographed cracked-mud texture, transferring crack contrast into the local
palette. Sparse wet areas darken slightly and retain restrained highlights. Coverage uses the
existing irregular cached field mask, including its exploration, slope and water restrictions.

The source is Poly Haven's CC0 Mud Cracked Dry 03, original 1K diffuse JPEG; see
`assets/textures/terrain/SOURCES.md` for provenance and verified source checksum. The texture uses
unit 5, separate from foundation, exploration and site-mask bindings. It is preloaded with terrain
assets and does not displace terrain or change generation, resources, collision or saves.

The Release renderer review completed 22 captures and 80 moving-camera frames without GL errors.
The GTX 1070 render-only probe at 2554 by 1389, 20 by 20 chunks and 1,134 resource targets averaged
8.31 ms over 100 measured frames after warm-up. Site and node-buffer uploads remained zero.
This excludes simulation, UI and swap and is not a full-game FPS claim.

The first iteration made the cracks too subtle; the final blend preserves more photographed
contrast in the interior while retaining the soft transition into the surrounding ground.
