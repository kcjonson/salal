# ui drag

`--scene=ui_drag`; `/api/ui/tree`'s `drag` and each node's `state` read after any step.

1. A help line, a road of four slots (Any card, Red only, Refuses, and a wide Verge with a Vehicle in it), a hand of three cards (Blue 1, Red 2, Blue 3 (reticle)) and "Nothing yet"; lint count 0, `drag` null.
2. Press Blue 1 and release without moving: nothing happens, "Nothing yet".
3. `ev=down,<Blue 1>&ev=move,<Any card>`: Blue 1 follows the pointer over the road, out of the hand's box; Any card and the Verge are outlined green, Refuses, Red only and the Vehicle aren't; Any card fills darker green. `drag.target.id` is `any`, `accepted` true, Blue 1's `slot` 2047.
4. Move over Refuses: it's edged red and the cursor is the no-entry sign. Release there: the card is back in the hand, the status reads "Blue 1 back in the hand, refused by refuses".
5. `ev=drag,<Red 2>,<Red only>`: "Dropped Red 2 on Red only"; the card is back in the hand (the scene doesn't move cards).
6. Drag Blue 1 onto the Vehicle: refused, even though the Verge round it takes cards. Drag it onto the Verge beside the Vehicle: "Dropped Blue 1 on Verge".
7. Drag Blue 3: the card stays in the hand, dimmed, and a ringed reticle follows the pointer instead; drop it on Any card: "Dropped Blue 3 on Any card", the reticle gone.
8. Drag Blue 1 over Any card, press Escape: back in the hand, `drag` null; release: nothing more. Same again with a right click instead of Escape.
9. Tab until Blue 1 shows its ring, press Enter: it lifts onto Any card, the nearest slot that takes it. Right: it skips Red only and Refuses, which don't take it, to the Verge. Tab: back to Any card. Enter: "Dropped Blue 1 on Any card", the ring still on Blue 1.
10. Tab to Red 2, Enter: it lifts onto Red only, the nearest that takes it. Left: Any card. Escape: back in the hand, the ring still on Red 2.
11. Lint count 0 again.
