# ui overlay components

1. `--scene=ui_overlay_components`: a title, "Nothing yet", two selects, an Actions dropdown, three rows of buttons and a "Right-click here" area; lint count 0.
2. Click the difficulty select: a menu below it as wide as it, Normal washed and focused.
3. Press Down, then Enter: the menu closes, the select reads Hard, the status reads "difficulty hard".
4. Click Actions, then click empty space: the menu closes and nothing else happens.
5. Click Actions, then Scrap: nothing; Scrap is dim. Click Inspect: the status reads "inspect".
6. Click Build: its menu opens above it.
7. Click "Abandon run...": a scrim over everything, a panel titled "Abandon run?" under a "CONFIRM" kicker, Tab cycling inside it only.
8. Open the dialog's select: its menu lands above the dialog.
9. Press Escape twice: the menu closes, then the dialog, the status reads "kept playing", focus back on the button.
10. Click "Crew panel": a panel with no scrim; the buttons under it still click.
11. Click each toast button: toasts stack at the top right, newest at the top; Info and Warning go after five seconds.
12. Open the dialog, then press "Critical toast": the toast sits above the scrim and a press on it reads "pressed breach".
13. Rest the pointer on Save: after half a second a tooltip below the pointer with the title, the line and a Ctrl+S key cap. Move off: it goes.
14. Right-click the area near its bottom-right corner: a context menu at the press, flipped and kept on screen; right-click elsewhere: it moves there.
