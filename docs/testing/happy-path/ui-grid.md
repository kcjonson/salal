# ui grid

1. `--scene=ui_grid`: a "Grid" title over five captioned frames; lint count 0.
2. "game layout": title, stats and score down a narrow left column, the board filling the right of the top two rows, controls centred under it; stats only as tall as its label, at the top of its row.
3. In the tree, `game_board` is as tall as the first two rows and the gap between them, and `game_controls`' centre is the board's.
4. The track row: 100px, 1fr, auto, minmax and 15% left to right, the 1fr and minmax cells the same width, auto just wide enough for its label.
5. "auto flow, spans": 1 and "2, 2 wide" across the first three columns, "5 at 4/1" in the fourth; 3, 4 and 6 on the second row; "7, 2 tall" down the fourth column's last two rows; 8 and 9 on the third, the cell after 9 empty.
6. "justify / align": start in its cell's top-left corner, center in the middle, end bottom-right, stretch filling its cell, "start end" bottom-left, "end start" top-right.
7. "form, auto 1fr": "New crew member" across the top, then Name, Callsign and Home port in a column as wide as "Home port", each beside a field filling the rest.
