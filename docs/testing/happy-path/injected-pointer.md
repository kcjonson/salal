# Injected pointer

`--scene=input_test`, the real cursor kept off the window; `/api/state?what=scene`'s `pointer` and `pointerInWindow` read after any step.

1. Before any input: "Pointer In Window: NO", `pointerInWindow` false.
2. `ev=move,400,300`: "Pointer: (400, 300)", "Pointer In Window: YES" in green, "pointer enter" in the recent events.
3. Take a screenshot, then read the state again: still (400, 300) and YES, frames later.
