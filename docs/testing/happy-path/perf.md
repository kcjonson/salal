# Perf scenes and runner

The sandbox launched with `--scene=<name> --http-port <port> --no-vsync`; a capture is
`/api/control?action=capture&op=start`, two seconds, then `op=stop`. The runner steps use the
Release perf build, `build-perf/apps/sandbox/Release/sandbox.exe` (`<exe>`), with no other build running.

1. `--scene=perf_quads`: thousands of overlapping coloured rects, many rounded, some bordered, filling a 1600 by 900 area from the top-left; `/api/ui/lint` count 0.
2. Two screenshots a second apart differ: the field scrolls a few pixels.
3. A capture: `capture.frames` above 0, `drawCalls.p50` 1, `instances.p50` 10,527, `uploadBytes.p50` 104 bytes an instance (1,094,808), `vsync` false, `framebuffer` the window's size.
4. `--scene=perf_text`: six columns of list rows headed "size 11 px" to "size 24 px", each row an icon (twelve slightly different ring-and-diamond icons, cycling), a label, a green value and a unit, none overlapping the next.
5. A capture: `drawCalls.p50` 38, a draw call each time a ninth texture is needed, `instances.p50` 5,003.
6. `--scene=perf_world`: a field of stars, cut off straight 100 px inside the window's edge on any side it reaches, brown roads running past it, coloured shapes and "site" labels over it, and a panel in each corner holding dots and "panel 0" to "panel 3".
7. A capture: `drawCalls.p50` 3, `instances.p50` about 97,000 (3,000 of them trees).
8. `--scene=perf_ui`: overlapping dark cards, each with a coloured border, header, dot, "#n" title, a number and a meter, over a 1600 by 900 area, dimmed by a scrim over the whole window; a menu of eight rows below one card, dimmed with them; a dialog of twelve rows centred on the window, two toasts at the top right and a yellow tooltip, "a tooltip, above everything", above a card, none of them dimmed. `/api/state?what=scene` has 1250 cards, 10049 nodes and 5 overlays; every `/api/ui/lint` violation is a `sibling-overlap` of cards.
9. Two screenshots a second apart: the cards, the menu and the tooltip have moved together; the dialog and the toasts haven't.
10. A capture: `drawCalls.p50` 1, `instances.p50` 16,504.
11. `op=stop` with no capture running: 409.
12. `node scripts/perf.mjs compare --threshold abc`: "isn't a number", exit code 2.
13. `node scripts/perf.mjs run --exe <exe> --scenes perf_quads --runs 1 --window 2`: one run, its gated figures printed, a JSON file under `perf-results/local/`, exit code 0.
14. `node scripts/perf.mjs compare --exe <exe> --base-exe <exe> --scenes perf_quads --runs 3`: each gated line shows both sides' spread, the counts match, it ends "perf: pass", exit code 0.
15. The same `run` while something else listens on 127.0.0.1:8094: "port 8094 is in use", exit code 2.
