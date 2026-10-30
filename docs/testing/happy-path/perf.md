# Perf scenes

The sandbox launched with `--scene=<name> --http-port <port> --no-vsync`; a capture is
`/api/control?action=capture&op=start`, two seconds, then `op=stop`.

1. `--scene=perf_quads`: thousands of overlapping coloured rects, many rounded, some bordered, filling a 1600 by 900 area from the top-left; `/api/ui/lint` count 0.
2. Two screenshots a second apart differ: the field scrolls a few pixels.
3. A capture: `capture.frames` above 0, `drawCalls.p50` 1, `uploadBytes.p50` above 3,000,000, `gpuFrames` above 0.
4. `--scene=perf_text`: six columns of list rows headed "size 11 px" to "size 24 px", each row an icon, a label, a green value and a unit, none overlapping the next.
5. A capture: `drawCalls.p50` 671.
6. `--scene=perf_world`: a field of stars, cut off straight 100 px inside the window's edge on any side it reaches, brown roads running past it, coloured shapes and "site" labels over it, and a panel in each corner holding dots and "panel 0" to "panel 3".
7. A capture: `instances.p50` 3000, `drawCalls.p50` 3.
8. `op=stop` with no capture running: 409.
