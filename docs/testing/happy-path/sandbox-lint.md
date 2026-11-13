# Sandbox lint

Each scene started with `--scene=<name>`, `/api/ui/lint` read once it has drawn, and after any step below.

1. `sdf_minimal`, `polygon`, `arc`, `polyline`, `blend`, `input_test`, `fill_hook`, `border` and `mask`: lint count 0, with no ui to check.
2. `fill_hook`'s screenshot: amber and teal stripes in every rect but "no hook", cut by each rect's rounded corners and inside its border, cropped by the clip, turned with the 15-degree rect, the bar showing through the translucent one, and the last rect's stripes half a stripe over from the one before it.
3. `ui_order`, `ui_text`, `ui_layout`, `ui_grid`, `ui_clip`, `ui_scroll`, `ui_motion` and `ui_icons`: lint count 0.
4. `ui_theme`: count 0; press T for light: still 0.
5. `ui_input`: count 0; drag the knob to the end of its track: still 0.
6. `ui_focus`: count 0; open the dialog, then Escape: still 0, the status reads "Dialog cancelled".
7. `ui_display` and `ui_controls`: count 0. In ui_controls' tree, Sound and Music carry `value` true or false and `state.selected` to match, View its segment's index, Volume its number.
8. In ui_controls, check Sound off, then drag Volume to each end: still 0, Sound's `value` false, Volume's 0 and then 1. Hover Lift: its tree `transform.translate` is [0, -2], and `transform` null once the pointer leaves. Tab to Lift and press Enter: it dips and washes pressed for a moment, and the status reads "pressed lift".
9. `ui_overlays`: count 0. `ui_overlay_components`: count 0, and still 0 with the difficulty menu open, the "Abandon run?" dialog open, a critical toast up and Save's tooltip showing.
10. In ui_text's tree, `weight_400`'s `text.fontFamily` is 0 and `weight_600`'s and `weight_700`'s are 1, the body role's bold face; the three look alike, being one atlas.
11. `sdf_minimal`'s screenshot: under the alphabet, "Halo over stripes" in dark ink with a cream halo about 3 px wide that keeps it readable over the green stripes and the dark blue between them, its outer edge soft and not cut off square at any glyph's box; "Drop shadow" in cream with a soft dark copy down and right of it.
12. `ui_text`'s screenshot: bottom right, over striped green art, "Halo label" in dark ink inside a cream halo, and "Drop shadow" in cream over a dark copy 2 px down and right.
13. `polygon`'s screenshot: bottom right, on a pale plate, the green rock and the red disk each over a soft brown shadow down and right of it, both fading to nothing with no hard edge; the rock's shadow follows its notch without a dark sliver.
14. `border`'s screenshot: a tab on a panel, its top corners round, its sides meeting the panel's top rule square, and the panel's rule not showing across the tab's opening; a sheet ruled down its left side only, all four corners square; one ruled top and bottom only; an underline; three sheets open at the bottom, inside, centred and outside, each side rule running straight to the bottom edge of the red guide box and stopping square there; and along the bottom, a dashed border with a dash centred on its top-left corner, round dots round a rounded box, round-capped dashes, and square-capped dashes on three sides, each loop meeting itself with no short dash or long gap.
15. `ui_display`'s screenshot: under the key caps, a "Tab" box with no bottom rule and square bottom corners, and an "Underline" box ruled only along its bottom, a "Dashed" box and a "Dotted" one; lint count still 0.
16. `polyline`'s screenshot: along the bottom, yellow round-capped dashes bending round the zigzag's joints over its red guide, round green dots spaced evenly round the closed hexagon, a dashed blue drawLine with square ends, and a star whose orange border is dashed all the way round.
17. `polygon`'s screenshot: right of the five rocks, a blue shield shaded light to dark, ringed in cream, with a cream star on it, labelled "embedded SVG".
18. `ui_drag`: count 0, and still 0 after a drag dropped on a target and one cancelled.
19. `mask`'s screenshot: flat slate everywhere outside a lumpy blob; inside the blob the tiles, bars and rings show dimmed, and inside two overlapping circles moving within it they show at full colour, with hard edges where the circles meet the dim and the blob meets the slate; the two labels and the picker tab draw over it all, undimmed.
