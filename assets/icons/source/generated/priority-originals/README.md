# Power priority icon variants

Generated with the built-in imagegen tool using `../../action_power_priority.png` as reference.
The existing three-tier High icon is unchanged. Original transparent outputs are preserved here;
the 256px Low/Medium sources are in the parent `generated` directory and mirrored into
`assets/icons/source`. `tools/import_priority_icons.ps1` matches the original High puck width and
baseline. `tools/rebuild_icons_atlas.ps1` preserves this shared scale.

## Prompts

Low: Create the LOW power priority game HUD icon variant from this reference. Preserve exactly the
reference art style: dark industrial metal circular power puck with cyan glowing power symbol,
three-quarter isometric front view. Reference has THREE vertically stacked pucks, this LOW variant
must show exactly ONE single puck, no other tiers. No text or numbers, no border, genuinely
transparent background. Use the same puck width relative to square canvas and the same lower
baseline as reference so low, medium, high form a coherent stack-height progression. Crisp
small-size readability at 64px. Reference is style/shape guide; original high icon stays unchanged.

Medium: Create the MEDIUM power priority game HUD icon variant from this reference. Preserve
exactly the reference art style: dark industrial metal circular power pucks with cyan glowing power
symbols, three-quarter isometric front view. Reference has THREE vertically stacked pucks, this
MEDIUM variant must show exactly TWO vertically stacked pucks connected by the short central
spindle, no third tier. No text or numbers, no border, genuinely transparent background. Use the same
puck width relative to square canvas and the same lower baseline as reference so low, medium, high
form a coherent stack-height progression. Crisp small-size readability at 64px. Reference is
style/shape guide; original high icon stays unchanged.
