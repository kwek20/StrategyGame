# UI structure

Keep each screen's RML document and layout stylesheet separate. Shared styles
are linked before the screen stylesheet:

- `common_base.rcss`: document defaults, decorative panel images and dividers.
- `common_buttons.rcss`: art-button layout and interaction states.
- `common_forms.rcss`: selectors, form columns and footer actions.
- `common_menus.rcss`: vertical main/pause menu layout.
- `common_slots.rcss`: build/HUD action-slot artwork and hover tint.
- `common_progress.rcss`: one tintable fill texture, fixed tracks and labels for
  loading, health, energy and construction. Use a `progress-track` wrapper and
  a native `progress` element with `progress-fill`; add `health`, `power` or
  `construction` to the progress element to choose its tint. Supply a clamped
  normalized value and `max="1"`. Keep numbers in a sibling `progress-label`.
  Never resize the track or use a framed image as the fill.

Settings and match setup use `form_panel.rml`, a native RmlUi template. It owns
the `shell`, panel artwork and `content` insertion point, and imports form styles.
Keep screen dimensions, content and entrance animations in the screen files.

Use `art-button` with a label `span` and optional `button-icon`. Variants should
use single-class selectors for their default tint so shared hover, focus,
active and disabled states can override it. Avoid ID-based button skin rules.
Action slots have their own skin and intentionally distinct selection states.

Selector rows carry the base control ID (for example `settings.resolution`)
and accept keyboard focus. Their children retain `.left`, `.value` and `.right`
IDs. C++ depends on these IDs for navigation, updates and action dispatch.
Do not rename them during visual refactors. HUD/build content is also generated
by `PlayState` and `BuildState`; check those producers before changing classes.

The entity HUD uses two equally sized panels with independent inset content
areas. Keep stat values non-shrinking; labels can truncate. Action slots use a
four-column grid with separate command and construction sections. Long lists
scroll within the panel. The first twelve actions use Alt+1 through Alt+9,
Alt+0, Alt+- and Alt+=; unavailable slots retain their position but cannot run.
Unavailable actions use a visual class rather than native disabling so their
explanation tooltips remain hoverable. Tooltips opt out of ancestor clipping.

The renderer integration test checks template expansion, selector focus and
interaction tints. Set `STRATEGY_FORM_SCREENSHOTS` to an output directory to
capture the settings and match setup fixtures as BMP files.

## Progress texture

`assets/textures/ui/progress_fill.png` was generated with the built-in image
generation tool. All progress variants use this same neutral texture; the native
progress element clips it as the value changes, keeping texture details stable.
Superseded artwork remains on disk but is no longer preloaded.

Generation prompt:

> Create a game UI fill texture only. Opaque rectangular grayscale texture filling the ENTIRE canvas edge to edge, wide landscape 3:1. Neutral grayscale silver-white fine electrical energy filaments flowing horizontally with subtle medium-light gray cloudy detail, luminous thin white horizontal core. Low contrast enough to read at 18 pixels tall. No frame, no border, no end caps, no margins, no text, no symbols, no colored pixels. This is a raw tintable texture to be mapped onto narrow progress bars, not a picture of a bar. Save image as project asset.
