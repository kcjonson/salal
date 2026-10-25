# Extracting a shared engine

Surveyed 2026-10-25, revised the same day after Kevin's decision to migrate
dual-deck-builder to C++. No code moved.

This is the engine repo. It holds a README, formatter configs, and these docs;
no code has been extracted yet.

## The name

**Salal**, *Gaultheria shallon*: the glossy evergreen shrub that carpets the
floor of Pacific Northwest coastal forests. It is the understory, the layer
everything else grows up out of, which is what this repo is to three games.

Checked against GitHub and the web for collisions in the engine, graphics and UI
space, since a name that is already a renderer is worse than a boring one.
Nothing anywhere. GitHub shows ~376 repos matching the string, all personal
projects belonging to people named Salal (it is also an Arabic and Urdu given
name), the only software among them a 0-star static site generator. So search for
it will always be noisy, and nothing in this space will ever be confused with it.

Ruled out on direct hits: **Madrona** (Stanford's GPU batch-simulation game
engine), **Hemlock** (an OpenGL 3D PBR renderer, plus a programming language and
a web framework), **Kelp** (a 645-star UI library and a 1,123-star trading bot),
**Larch** (615-star IMAP tool, an Elm-like framework, and LARC is a game engine),
**Camas** (a 67-star React authorization library), **Oxalis** (a game studio and
a Java project, with Oxylus a C++ engine next door), **Sitka** (a Microsoft font
family, which is a bad collision for an engine that renders text).

**Tatoosh**, **Elwha** and **Skagit** came back clean and are the backups.

On the word itself: salal arrives in English through Chinook Jargon, the regional
trade language, from *salla*, and the botanical epithet *shallon* is the same
borrowing. The berries were a winter staple across the coast, cooked and dried
into cakes on cedar boards or skunk cabbage leaves, then rehydrated or dipped in
seal, whale or eulachon oil. Lewis and Clark recorded the practice on the Oregon
coast in 1806.

It names a plant rather than a people or a place, and it came through trade
rather than conquest, which is why it was preferred over Sitka. "Sitka" is
*Sheetʼká*, from Tlingit *Shee Atʼiká*, "people on the outside of Shee" (Baranof
Island), and it entered common English use in 1867; the place is where the
Russian-American Company took the Kiks.ádi clan's ground by force in 1804. The
Sitka spruce's own Tlingit name is *Shéiyi*, so "Sitka spruce" was never an
Indigenous name for the tree in the first place.

## Correction to the first version of this document

The first version reported dual-deck-builder's game layer as 2,348 lines. The
real figure is **138,266 lines across 523 files**, and its `src/` total is
**210,855**, not 53,370. A line-counting command batched through `xargs` and only
the last partial subtotal was read.

The correction makes the migration **larger** than this document previously
claimed, not smaller. Worldsim's figures were measured correctly and all hold.

## The decision

**Extract a C++ engine into its own repo, consumed by all three games. Migrate
dual-deck-builder from TypeScript to C++.**

The first version of this document argued against migrating dual-deck-builder.
Kevin has decided otherwise and that settles it; the cost is recorded below so
the plan is built on the real number rather than the optimistic one.

Three C++ consumers makes extraction straightforwardly correct rather than
marginal. At two it was a judgement call about whether the ceremony paid for
itself. At three it is the obvious shape.

## Why extraction is right

You have written this engine three times.

Worldsim's UI layer is already a port: `docs/technical/ui-framework/colonysim-integration-architecture.md`
documents bringing colonysim's Layer and Shape components into worldsim in
October 2025. Dual-deck-builder then grew its own engine independently, in
another language, and arrived at the same design: `Layer` for positioning and
hierarchy, `Component` extending it for interaction, concrete shape classes
(Rectangle, Text, Circle, Triangle), batching underneath.

Same architecture, three implementations, two languages. A fourth game would make
it four.

## What exists

### Worldsim, C++20 and OpenGL

| Library | Files | Lines | Generic? |
|---|---|---|---|
| `foundation` | 45 | 6,687 | Yes |
| `renderer` | 69 | 10,010 | Yes |
| `ui` | 100 | 19,469 | Mostly, unaudited |
| `engine` | 293 | 73,881 | **Mixed.** Scenes, lifecycle, config, input and the dev server are generic; the ECS, colonists, chunks, pathfinding, tasks and crafting are not |
| `world` | 79 | 21,460 | No |
| `geometry` | 67 | 18,064 | No |
| `planet-view` | 16 | 1,942 | No |

`ui` breaks down as `components` 12,765, `layout` 1,712, `focus` 1,422, `font`
1,151, `debug` 592, `theme` 547, `component` 496, `shapes` 483, `layer` 176,
`input` 86. `renderer` as `vector` 3,930, `primitives` 2,784, `resources` 1,320,
`CoordinateSystem` 564, `shader` 533, `gl` 468, `metrics` 411.

### Dual-deck-builder, TypeScript and WebGL

| Area | Files | Lines |
|---|---|---|
| `src/renderer/engine` | 238 | 68,185 |
| `src/renderer/game` | 523 | 138,266 |
| `src/` total | 790 | 210,855 |

Engine: `ui` 16,573, `components` 10,442, `rendering` 8,893, `draw` 7,686,
`debug` 6,841, `input` 6,381, `text` 3,655, `services` 2,756, `style` 1,249,
`theme` 950, `animation` 926, `gpu` 854, `errors` 644, `coords` 335.

Game: `screens` 35,075, `map` 25,416, `mechanics` 22,179, `ui` 21,933,
`campaign` 21,734, `ai` 5,661, `core` 3,740, `sim` 859, `art` 364, `data` 343.

**Its engine is already isolated.** One import from game code across 238 files.

**44% of its game layer is tests**: 204 files, 60,522 lines. That is the most
valuable asset in this whole plan, for reasons under the migration section.

### The asymmetry

Dual-deck-builder's engine is 68,185 lines against worldsim's 36,166 for
foundation, renderer and ui together. It is the larger implementation and in
places the better one.

Stated carefully, since the comparison is not apples to apples: worldsim's input
and debug tooling are not all in `libs/ui` (86 and 592 lines there). Its
InputManager and HTTP dev server live in `libs/engine`.

But dual-deck-builder has `input` (6,381), `debug` (6,841) and `animation` (926)
as **first-class engine subsystems, peers of rendering and UI**, where worldsim
has them split across a library boundary with 73,000 lines of colonist logic.
That is not a maturity gap, it is a difference in where each project drew the
line, and the cleaner line is dual-deck-builder's.

## The extraction problem

Not "is `libs/ui` clean enough to move." It is that **worldsim's generic code
does not all live in `libs/`.**

Scene management, lifecycle, config loading, input, and the HTTP dev server sit
inside `libs/engine` mixed with the game. Extract foundation, renderer and ui and
you get a drawing layer with no way to open a window, run a loop, switch scenes,
or read a gamepad.

So: **dual-deck-builder is the reference for where the boundary goes, worldsim is
the source of the code.** That framing survives the decision change and is the
most useful line in this document.

## Is this an engine, or a drawing library?

An engine, but a small and specific one. The test is not what is inside it, it is
**who owns the frame**. A drawing library is something your code calls. An engine
is something that calls your code, and worldsim's `AppLauncher` already does
that: it creates the window, sets up GL, runs the loop, and calls into whatever
scene is current.

The useful translation, since most of the thinking here comes from web work:

| Engine layer | Closest web equivalent |
|---|---|
| `foundation` | lodash, a math library, `console` |
| `renderer` | the Canvas/WebGL context, plus a text shaper |
| `ui` | React, plus the CSS layout engine, plus a component library |
| `app` | **the browser itself** |

That last row is the whole point. On the web you never write a main loop: you
register handlers and the browser calls them, and `requestAnimationFrame` is the
browser lending you one frame at a time. Natively nobody lends you anything. You
create the window, make the GL context, write `while (running)`, poll the OS for
input, and decide when to swap buffers.

So a web developer building a native game is mostly rebuilding the browser.
`libs/app` is that, written once instead of three times.

### What it deliberately is not

Nothing here is Unity or Unreal, and the gap is worth being explicit about. No
scene editor, which is what most people actually picture when they hear
"engine". No physics. No audio, literally none today. No asset importers, no
skeletal animation, no scripting runtime, no networking, no console abstraction,
no material graph.

What it is: **a 2D presentation engine.** It owns the frame, the window, input,
time, a retained scene graph, layout, text, batching, theming and focus. That is
a real engine by the ownership test and a modest one by any other.

Physics stays per-game on purpose. In this game the physics *is* the game and
lives in `sim`; dual-deck-builder has none. If a third game ever wants the same
rigid-body model, that is the moment to promote it, not before.

### Why extracting one is not the usual mistake

Building a general-purpose engine before you have a game is how hobby projects
die. This is not that. Three games exist, they demonstrably share the same four
layers, and the architecture has already been implemented three times by hand.
Factoring out what three codebases share is a different activity from
speculating about what a game might need.

## What the repo looks like

```
salal/
  CMakeLists.txt
  CMakePresets.json          from worldsim, unchanged
  vcpkg.json                 opengl glfw3 glew glm nlohmann-json
                             stb nanosvg msdfgen cpp-httplib gtest benchmark
  libs/
    foundation/              math, platform, logging, arenas, resource handles,
                             seeded PRNG with forking
    renderer/                gl, shader, primitives, batch, vector, text,
                             coords, metrics
    ui/                      component, components, layout, focus, theme,
                             shapes, font, animation, inspector
    app/                     window, fixed-step loop, scenes, config,
                             input (keyboard, mouse, gamepad), dev server
  apps/
    sandbox/                 the ui-sandbox pattern, belongs with the engine
  tests/
  docs/
    architecture.md          the four-layer stack, normative
    coding-standards.md      moved from worldsim
```

`libs/app/` is new and is the hard part: the generic slice carved out of
worldsim's `libs/engine`, plus the fixed-step loop, gamepad input and seeded
PRNG that [asteroids-roguelike/docs/tech-stack.md](https://github.com/kcjonson/asteroids-roguelike/blob/main/docs/tech-stack.md) found missing. Those belong to the
engine, because all three games want them.

`animation` moves into `ui` from dual-deck-builder's design; worldsim has no
equivalent.

The dependency rule carries over, with one addition: a consuming game's
simulation layer may not depend on `renderer` or `ui`.

```
game
 |
app
 |
ui
 |
renderer, foundation
```

### How games consume it

**CMake FetchContent, pinned to a tag per game.** No submodule dance, nothing to
initialise on clone, CMake 3.25 already the floor, and an engine change cannot
break a game that has not opted in.

Worldsim already runs vcpkg with overlay ports
(`./cmake/vcpkg-overlay-ports`), so a private vcpkg registry is the idiomatic
upgrade once three consumers becomes more, or once anyone else is working on it.

Rejected: a monorepo holding all three games. It solves versioning by deleting it
and couples CI and release cycles for projects with nothing to say to each other.

## Migrating dual-deck-builder

**This is two jobs, and conflating them is how it goes wrong.**

### Job A: its engine's better subsystems, into the shared C++ engine

Input, debug tooling and animation are where the TypeScript implementation came
out ahead. Those designs get reimplemented in C++ inside `libs/app` and
`libs/ui`, and all three games get them.

This is engine work with payoff beyond dual-deck-builder, it happens during the
`app` phase, and it is the part of the migration that is unambiguously worth
doing on its own merits.

### Job B: its game layer, ~138,266 lines, into C++

The large and risky one. The port order matters more than anything else here, and
there is a very good first target.

**Start with the simulation core.** `src/renderer/game/sim/` is 859 lines and its
spec (`docs/specs/Balance Simulation.md`) says it is deliberately free of DOM,
`fetch`, localStorage and dev-tools hooks so the same code runs in Node and in a
Web Worker. It takes plain data in and emits plain JSON out, it is seeded with
forking (`fork('run', n)`, `fork('fight', f)`), and **the same config and seed
give the same JSON in Node and in the browser.**

So a C++ third implementation can be held to exactly that bar. Port it, run both
on identical configs and seeds, diff the JSON. That is differential testing for
free, and it means the riskiest question in the whole migration (does the game
logic port faithfully?) gets answered mechanically rather than by playtesting.

The sim core plays through the game's real code rather than a copy of it, which
is what makes this work: verifying the sim verifies `mechanics` (22,179),
`campaign` (21,734) and the encounter builder underneath it.

**Then the logic layers**, in dependency order: `core` (3,740), `mechanics`,
`campaign`, `map` (25,416, area-map generation, pure enough to verify the same
way), `ai` (5,661). Each one is verifiable against the TS implementation on
identical seeds before anything is drawn.

**Presentation last.** `screens` (35,075) and game `ui` (21,933), 57,008 lines
between them, are the only part that needs the shared engine to exist. They come
after Job A and after the logic layers are known good.

**The 60,522 lines of tests are the spec.** They port alongside their subjects and
they are what makes a cross-language rewrite of this size tractable at all. A
port with no oracle is a rewrite; a port with 204 test files and a seeded
differential harness is a translation.

### What the migration costs

The web playtest deploy. Dual-deck-builder currently pushes to
`bearcavinteractive.com/playtest/dual-deck-builder/` on every merge to main, and
that is its primary playtest channel with Electron builds secondary. You cannot
send someone a link to a C++ binary.

**WebAssembly puts it back**, which is the single best argument for the section
below and changes this from an accepted regression to a temporary one. The
engine is a clean Emscripten target and both new games are good fits.

Until the web build exists, the gap is partly covered by worldsim's HTTP dev
server: screenshot capture, element-tree inspection, layout linting and
synthetic input injection. That serves agent-driven verification well and human
playtesting not at all. For humans it is a distributed build instead of a URL.

### One rule that does not apply here

Worldsim's CLAUDE.md says to delete old code aggressively and never leave legacy
fallbacks. That is right within a language and wrong across this migration: the
TypeScript implementation is the oracle for the differential tests and has to
stay until parity. Deleting it early throws away the only thing that proves the
port correct.

## WebAssembly

**Worth doing, and it recovers the thing the migration costs.** A browser build
for playtesting puts the playtest URL back, for both new games.

This codebase is an unusually clean Emscripten target, and that is a measured
claim rather than an optimistic one:

- **Ten shaders, all `#version 330 core`.** The port is a version header and
  precision qualifiers, in ten files.
- **No geometry or compute shaders anywhere.** WebGL2 has no geometry stage, and
  that is the usual blocker. It is absent.
- **The GL 3.3 core context is created in exactly one place**,
  `libs/engine/application/AppLauncher.cpp:128`, which is a file headed for
  `libs/app`. So the platform branch lands in one file in the engine.
- **GLM, nanosvg, stb and nlohmann-json** are header-only or pure C/C++.

### Toolchain

Emscripten with `--use-port=contrib.glfw3` (its own GLFW3 implementation,
available since Emscripten 3.1.55), linking with `-sUSE_WEBGL2=1 -sFULL_ES3=1
-sALLOW_MEMORY_GROWTH=1`.

### Porting items

Each is small and each lives in one place, which is the point.

- **GLEW compiles out.** WebGL needs no extension loader.
- **Shaders become `#version 300 es`** with explicit precision qualifiers.
- **The main loop inverts.** `emscripten_set_main_loop` drives the frame instead
  of a `while` loop owning the program.
- **The dev server compiles out.** cpp-httplib cannot listen in a browser.
- **Threads.** Worldsim uses them in `Application.cpp` and `AssetRegistry.cpp`.
  On the web they need SharedArrayBuffer plus COOP/COEP headers; neither new game
  needs them, so the simplest answer is a single-threaded web path.
- **File I/O** goes through preloading or MEMFS rather than the filesystem.
- `FULL_ES3` emulation costs performance wherever client-side arrays or
  `glMapBuffer` are used. Use VBOs and the flag stops mattering.

### Per game

- **asteroids-roguelike:** ideal. 2D, single screen, a few hundred flat shapes,
  no worldgen, no large assets.
- **dual-deck-builder:** good. UI-heavy and simulation-light, and a web build
  restores exactly the playtest channel the C++ migration takes away.
- **worldsim:** no, and it does not need one. A 1.3 GiB planet file and threaded
  worldgen are not browser work.

### Design for it now, not later

This is the actionable part, and it is a constraint on the extraction rather
than a separate project:

- Keep the renderer inside the GL ES 3.0 subset. No geometry shaders, no compute,
  no `glMapBuffer`.
- Author shaders so a version header swap is the entire change.
- Make the fixed-step loop a tick function the platform calls, not a loop that
  owns `main()`. This matters for its own sake and it is what
  `emscripten_set_main_loop` needs.
- Put the dev server behind a compile flag from the first commit.
- Load assets through an indirection that can be MEMFS.

All cheap upfront. All expensive to retrofit once three games depend on the API.

### Not verified

- Whether msdfgen runs at asset-build time or at runtime. At runtime it has to
  compile to WASM, which is probably fine and is unchecked.
- Download size. A WASM binary plus SDF atlases plus SVG assets is likely a few
  megabytes, acceptable for playtesting and unmeasured.
- Audio, below.

### Audio is net-new

Worldsim has **no audio library at all**: nothing in `vcpkg.json`, nothing in
`libs/`. So the mix-priority system, stem layering and procedural SFX in
[asteroids-roguelike/docs/art-audio.md](https://github.com/kcjonson/asteroids-roguelike/blob/main/docs/art-audio.md) are engine work nobody has started, and they are a
fourth gap alongside the fixed-step loop, gamepad input and determinism.

The WebAssembly target constrains the choice. miniaudio and OpenAL-soft both have
Emscripten backends over Web Audio; picking one of those keeps the web build
possible, and picking something platform-native closes it off.

## Sequencing

Designed so nothing blocks on anything it does not have to.

0. **Audit.** Does `ui` respect worldsim's documented layering, or reach into
   `engine`? Worldsim's own CLAUDE.md warns the code may violate its standards,
   so this is verified, not assumed. Read-only, and it gates the phases below.
1. **Extract `foundation` and `renderer`.** The generic ones. Worldsim converts
   to consuming them and must still build and run identically.
2. **Carve out `app`.** The generic slice of `libs/engine`, plus the fixed-step
   loop, gamepad input and seeded PRNG. Job A of the migration lands here.
   `asteroids-roguelike` starts here.
3. **Extract `ui`.** Largest and least certain, and nothing upstream waits on it.
4. **Dual-deck-builder Job B**, sim core first with differential verification,
   presentation last.

In parallel and blocked by none of it: the feel prototype for this game, which
needs `foundation`, enough of `renderer` to draw flat shapes, and the loop. No UI
layer at all.

The **WebAssembly target is not a phase**, it is a set of constraints observed
from phase 1 onward (see "Design for it now"). The first actual web build is
worth attempting as soon as the prototype draws something, because a browser
build that works on a white triangle and a dozen circles proves the toolchain
before there is anything complicated to debug.

Each extraction phase is its own branch and PR in worldsim. Not one giant one:
worldsim is active, and a long-lived extraction branch against an active repo is
the standard way this fails.

## Risks

**Dual-deck-builder's playtest freeze is now the dominant risk.** It is actively
playtested on every merge, and Job B is ~138,000 lines. A port that long either
freezes playtesting or runs as a parallel implementation for a long time. The
sim-core-first order exists so that the frozen window covers only presentation,
with the logic verified before it starts.

**Extracting too early bakes in worldsim's assumptions.** An API shaped by one
2D-tiles-plus-3D-worldgen game may not fit an arcade shooter or a deckbuilder,
and you find out afterwards.

The mitigation is also the opportunity: **this game is the genericity test.** No
tiles, no chunks, no 3D, no pathfinding. If the engine serves it without
contortion the engine is generic; if it fights, that is the API talking while
there is still only one real consumer. Dual-deck-builder is the second test and a
harder one, since it is UI-heavy where this game is simulation-heavy.

**Extraction stalls every game and ships nothing.** The sequencing exists for
this. Phase 1 is small, phase 3 is optional for a long time, phase 4 is its own
project, and the prototype runs throughout.

**Versioning.** Three consumers, one developer, pinned tags. Manageable now,
becomes real with a second developer.

**The web build rots if it is not in CI.** A third build configuration that
nobody compiles breaks silently and then costs a day to revive. If the
WebAssembly target is worth having, it belongs in the engine repo's CI from the
first commit that supports it, even when all it does is link.

## Open

- The audit in phase 0. Everything is downstream of it.
- Whether `libs/app` is one library or splits into `app` and `input`.
- Whether this document moves into the salal repo, and when.
- Whether worldsim's vector/SVG asset conventions come along with `renderer` or
  get parameterised out. Dual-deck-builder has its own `draw` and `rendering`
  layers that may disagree.
- Whether dual-deck-builder keeps a browser build of its sim core for the Balance
  Lab after the game itself is native. The spec says that core runs in a Web
  Worker today, and keeping that would preserve the differential harness
  permanently rather than only during the port.
- Which audio library, and whether audio lives in the engine or per game. The
  engine, probably, since all three will want a mix.
- Whether msdfgen is a build-time or runtime dependency, which decides whether it
  has to compile to WASM.
- WASM download size, unmeasured.
- Whether the web build gets gamepad support. The browser Gamepad API exists and
  Emscripten's GLFW port maps to it, but couch co-op in a browser tab is an odd
  proposition and may not be worth the verification effort.
