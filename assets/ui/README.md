# UI structure

Keep each screen's RML document and layout stylesheet separate. Shared styles
are linked before the screen stylesheet:

- `common_base.rcss`: document defaults, decorative panel images and dividers.
- `common_buttons.rcss`: art-button layout and interaction states.
- `common_forms.rcss`: selectors, form columns and footer actions.
- `common_menus.rcss`: vertical main/pause menu layout.
- `common_slots.rcss`: build/HUD action-slot artwork and hover tint.

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

The renderer integration test checks template expansion, selector focus and
interaction tints. Set `STRATEGY_FORM_SCREENSHOTS` to an output directory to
capture the settings and match setup fixtures as BMP files.
