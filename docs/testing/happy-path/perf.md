# Perf scenes and runner

The sandbox launched with `--scene=<name> --http-port <port> --no-vsync`; a capture is
`/api/control?action=capture&op=start`, two seconds, then `op=stop`. The runner steps use the
Release perf build, `build-perf/apps/sandbox/Release/sandbox.exe` (`<exe>`), with no other build running.

1. `--scene=perf_quads`: thousands of overlapping coloured rects, many rounded, some bordered, filling a 1600 by 900 area from the top-left; `/api/ui/lint` count 0.
2. Two screenshots a second apart differ: the field scrolls a few pixels.
3. A capture: `capture.frames` above 0, `drawCalls.p50` 1, `uploadBytes.p50` above 3,000,000, `vsync` false, `framebuffer` the window's size.
4. `--scene=perf_text`: six columns of list rows headed "size 11 px" to "size 24 px", each row an icon (twelve slightly different ring-and-diamond icons, cycling), a label, a green value and a unit, none overlapping the next.
5. A capture: `drawCalls.p50` 671.
6. `--scene=perf_world`: a field of stars, cut off straight 100 px inside the window's edge on any side it reaches, brown roads running past it, coloured shapes and "site" labels over it, and a panel in each corner holding dots and "panel 0" to "panel 3".
7. A capture: `instances.p50` 3000, `drawCalls.p50` 3.
8. `op=stop` with no capture running: 409.
9. `node scripts/perf.mjs compare --threshold abc`: "isn't a number", exit code 2.
10. `node scripts/perf.mjs run --exe <exe> --scenes perf_quads --runs 1 --window 2`: one run, its gated figures printed, a JSON file under `perf-results/local/`, exit code 0.
11. `node scripts/perf.mjs compare --exe <exe> --base-exe <exe> --scenes perf_quads --runs 3`: each gated line shows both sides' spread, the counts match, it ends "perf: pass", exit code 0.
12. The same `run` while something else listens on 127.0.0.1:8094: "port 8094 is in use", exit code 2.
