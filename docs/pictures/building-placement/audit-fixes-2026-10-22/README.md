# Building-placement audit follow-up

Engine captures from the placement regressions and the saved Continental setup. See
[the audit follow-up](../../../BUILDING_PLACEMENT_AUDIT.md#implementation-follow-up-2026-10-22)
for measured scope, limits and verification.

- [Temporary excavation preview](leveling-preview.png): the GPU presents the proposed cut while
  authoritative and persistent renderer terrain remain unchanged. A depth-buffer regression checks
  that its geometry matches the accepted cut.
- [Accepted excavation](leveling-complete.png): the same plane after commit.
- [Placement controls](placement-controls.png): right-click rotation, right-drag camera, Escape cancel.
- [Rejected placement](placement-rejected.png): explicit authoritative collision feedback.
- [Moving generator preview](basic_generator_cut_moving.png): ground-adjustment status and support.
- [Moving hub preview](command_hub_cut_moving.png): the same large-building performance scene.
