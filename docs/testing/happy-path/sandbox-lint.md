# Sandbox lint

Each scene started with `--scene=<name>`, `/api/ui/lint` read once it has drawn, and after any step below.

1. `sdf_minimal`, `polygon`, `arc`, `polyline`, `blend`, `input_test` and `fill_hook`: lint count 0, with no ui to check.
2. `fill_hook`'s screenshot: amber and teal stripes in every rect but "no hook", cut by each rect's rounded corners and inside its border, cropped by the clip, turned with the 15-degree rect, the bar showing through the translucent one, and the last rect's stripes half a stripe over from the one before it.
3. `ui_order`, `ui_text`, `ui_layout`, `ui_clip`, `ui_scroll`, `ui_motion` and `ui_icons`: lint count 0.
4. `ui_theme`: count 0; press T for light: still 0.
5. `ui_input`: count 0; drag the knob to the end of its track: still 0.
6. `ui_focus`: count 0; open the dialog, then Escape: still 0, the status reads "Dialog cancelled".
7. `ui_display` and `ui_controls`: count 0. In ui_controls' tree, Sound and Music carry `value` true or false and `state.selected` to match, View its segment's index, Volume its number.
8. In ui_controls, check Sound off, then drag Volume to each end: still 0, Sound's `value` false, Volume's 0 and then 1.
9. `ui_overlays`: count 0. `ui_overlay_components`: count 0, and still 0 with the difficulty menu open, the "Abandon run?" dialog open, a critical toast up and Save's tooltip showing.
