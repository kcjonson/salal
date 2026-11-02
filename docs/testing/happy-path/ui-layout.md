# ui layout

The demos added since the scene came over from worldsim; `/api/ui/tree` gives each `bounds` in its parent's box.

1. `--scene=ui_layout`: lint count 0.
2. "inset by fraction": a plate with a blue box top left, a red one top right and a green band low in the middle, none touching the others. In the tree, `fraction_left` at (20, 15) 40 by 40, `fraction_right` at (130, 15) 50 by 25, and `fraction_band` at (80, 70) 40 by 20: shares of the 200 by 100 plate, its 10 px padding ignored.
