# ui layout

The demos added since the scene came over from worldsim; `/api/ui/tree` gives each `bounds` in its parent's box.

1. `--scene=ui_layout`: lint count 0.
2. "inset by fraction": a plate with a blue box top left, a red one top right and a green band low in the middle, none touching the others. In the tree, `fraction_left` at (20, 15) 40 by 40, `fraction_right` at (130, 15) 50 by 25, and `fraction_band` at (80, 70) 40 by 20: shares of the 200 by 100 plate, its 10 px padding ignored.
3. "wrapping strip": seven chips in three lines, the first line as tall as its tallest chip, lines 6 px apart. In the tree, the strip is 240 by 94, `chip_3` starts the second line at (6, 42) and `chip_6` the third at (6, 68).
