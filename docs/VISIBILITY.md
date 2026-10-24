# World visibility

`Player::visibility` is the authoritative fog state. `VisibilityGrid` stores sparse,
ordered 16-by-16-cell chunks in world coordinates. Missing chunks are hidden; queries
do not allocate them. Negative coordinates use floor division, including at chunk corners.

The playable rectangle does not clip sight, exploration, entity visibility or targeting.
It continues to govern movement, placement and generation. Each operational friendly
entity reveals its normal circle, with the existing elevation and power modifiers.
Current visibility is cleared and rebuilt each simulation tick; exploration remains.

Cell size remains match extent divided by 128, preserving the previous fog resolution.
Chunks are allocated only as cells are revealed. Satellite research reveals the map and
its surrounding scenery through the same grid: low resolution explores it, while high
resolution makes it live. The scenery extent is shared with `TerrainBackdrop`.

Terrain, water, models, selection, picking, effects, intelligence and combat use this
same state. The renderer uploads a dense window covering the rendered landscape,
without owning exploration history. Hidden texture borders prevent extrapolation of
the last visible pixel. Geometry extent and fog-texture extent are separate uniforms.
There is no border-specific fog texture or border-only source list.

Save format 1 now requires visibility cell size and explored chunks for each player.
Live visibility is reconstructed from entities and research on load. Explored cells,
including those outside the playable rectangle, participate in authoritative checksums.
As with other development schema changes, previous save layouts are rejected; there
is no compatibility reader or format-number change.
