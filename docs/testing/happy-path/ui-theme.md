# ui theme

1. `--scene=ui_theme`: a "Theme" title, five panes (pane, inset, overlay, button, tooltip), a purple pane, the type roles and the swatches; lint count 0, and `/api/state?what=scene` reads mode dark with no diagnostics.
2. Press T: everything goes light, lint still 0, the button pane's corners rounder than the others', and the state lists one diagnostic, an info that `styles.button` is redefined (borderRadius).
3. Press T again: dark, and no diagnostics.
