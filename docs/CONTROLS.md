# Controls and key bindings

Updated 2026-10-24. Implemented controls, not a proposed shortcut list.

## Settings

Settings has separate Video, Audio and Keybinds tabs. Keybinds supports search, category filtering,
primary/secondary bindings, keyboard chords, bare held modifiers, mouse buttons and wheel actions.
Continuous actions cannot use wheel events. Escape cancels capture; Clear Selected removes a slot.

Binding conflicts are checked in overlapping input contexts. Replace Conflicts clears only the
conflicting slots. Different contexts can reuse inputs. Right-drag camera movement deliberately
shares a button with a short right-click contextual order or building rotation. Shift can accelerate
the strategy camera and extend a selection, but repeat placement and range locking are independent.

Apply saves without closing Settings. It updates audio/display and notifies covered gameplay through
`StateContext::settingsRevision`. Play reloads its cached configuration once on return/update and
clears held input. Cancel/Back discards unapplied changes, including temporary interface scaling.
Bindings are stored as two explicit device/code/modifier slots per named action. Existing integer
settings bindings are read to preserve user preferences; new writes use the structured form.

## Default controls

| Action | Default |
| --- | --- |
| Strategy camera / direct-control movement | WASD, independently configurable |
| Fast strategy camera / run in direct control | Hold Shift |
| Pan / orbit camera | Right-drag / middle-drag |
| Zoom | Mouse wheel |
| Select / add to selection | Left-click / hold Shift |
| Select and center command hub | H |
| Cycle idle construction drones / select all idle drones | . / Shift+. |
| Recall / assign control groups 1-9,0 | Number / Ctrl+number |
| Contextual unit order | Right-click |
| Stop selected units | X |
| Command panel slots 1-12 | Alt+1 through Alt+0, Alt+-, Alt+= |
| Copy hovered owned building, including rotation | B |
| Place / repeat and queue construction | Left-click / hold Shift while placing |
| Maintain power connection range | Hold Ctrl during placement |
| Rotate building | Right-click or R |
| Cancel placement | Escape |
| Cancel selected unfinished construction | Delete |
| Enter/leave direct control | P; Escape also leaves direct control |
| Quick save / quick load | F5 / F9 |
| Detailed / terrain / water / power diagnostics | F3 / F4 / F6 / F7 |

Ctrl+Shift can combine range locking and repeated placement. HUD action labels and placement hints
are derived from bindings. The most recent rotation is remembered per construction recipe during
the current session. Copying chooses the nearest owned construction drone and an owned building's
recipe/orientation; it never copies the building or bypasses payment/placement checks.

The optional Arrows + Grid preset moves strategy/editor camera movement to arrow keys and maps
command slots to QWER / ASDF / ZXCV. Rotation's keyboard alternative becomes T and Stop becomes
Space to avoid grid conflicts. Direct-control WASD remains unchanged. Presets replace bindings only;
Apply is still required. Fixed menu navigation (Tab/arrows/Enter and Escape) remains available.

Control groups and remembered rotations are transient presentation state. They do not control the
simulation directly. Group recall drops references to missing/enemy entities.

## Repeated construction

Every accepted placement creates and pays for a normal blueprint immediately. A Shift placement
retains the chosen recipe. Builders already constructing (including construction suspended for
charging) append the new building to their FIFO construction queues. The first queued placement
starts immediately for idle builders. Gathering is deliberately interrupted by the first build order.

A final click without Shift exits placement but appends to the same active placement batch; it does
not replace earlier work. Escape exits placement without cancelling blueprints or paid queued work.
Explicit replacement construction, Move, Gather, Deliver, Attack, Repair and Stop clear queued work
when those commands are accepted. Stop also clears suspended work. Cancelled blueprints are removed
from all queues; completed or missing targets are skipped. Recharging preserves active and queued
construction. Multiple selected builders each retain their own queue.

Power devices connect to an eligible operational endpoint at placement. If still unconnected when
construction finishes, they retry then, allowing sequentially built poles to extend a network.
A visible connection is not a promise of adequate grid power. Range assistance still obeys ordinary
terrain, exploration, collision and cost validation.

Queued building IDs live in `UnitComponent::constructionQueue`, participate in authoritative
checksums, and persist in format-1 saves. The field is required under the existing pre-release
in-place schema policy: older development saves without it are rejected. No save migration or
format-version bump was added. Place/Construct commands serialize queue intent; wire codec version
is 10, with existing command variant indices preserved.

## Code and focused verification

- `src/input/KeyBindings.*`: action catalogue, contexts, binding matching/labels, held state,
  conflict detection and presets.
- `src/game/SettingsState.*`, `assets/ui/settings.*`: tabbed settings and binding capture.
- `src/game/PlayState.*`, `src/game/BuildState.*`: gameplay/editor input consumers.
- `src/simulation/GameSession.*`: authoritative construction queue execution.
- `key_bindings`: context/chord/hold/conflict/preset/config behavior.
- `construction_queue`: deterministic FIFO, charging, cancellation, checksums, save/load and codec.
- `persistence_round_trip`: updated configuration and save round trips.
- `keybind_settings_controls`: actual search/capture/replacement/filter/reset/preset UI and scaling.
- `repeat_placement_controls`: three real placements retain work and close on the final plain click.
- `power_placement_controls`: constrained clicks and a real Settings change applied to active Play.
- `navigation_keybind_controls`: hub/idle/group shortcuts and direct-control camera/movement/release.
