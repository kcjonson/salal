# Extracting a shared engine

Surveyed 2026-10-25, revised the same day after Kevin's decision to migrate
dual-deck-builder to C++, and revised again the same day after the Phase 0
surveys. No code moved.

This is the engine repo. It holds a README, formatter configs, and these docs;
no code has been extracted yet.

## Corrections in this revision (2026-10-25)

Checked by five read-only surveys against worldsim main `db335a2b`,
dual-deck-builder `afcd7b16` and asteroids-roguelike `176064c`, and fixed in
place: dual-deck-builder's engine imports zero game code (not one) and was
rebuilt to a spec abstracted from worldsim; its 740-line sim core drags a
~32,900-line import closure; worldsim's dev server lives in `foundation`, has
twelve routes and no compile flag; twelve shader files, not ten; two production
GL-context sites, not one; worldsim already has a deterministic PRNG and lacks
only the fork API; Phase 0 is done and adds an untangling step before phase 1.
Detail: [phase-0-audit.md](phase-0-audit.md),
[app-carve-out.md](app-carve-out.md),
[consumer-requirements.md](consumer-requirements.md),
[open-decisions.md](open-decisions.md).

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
claimed, not smaller. The worldsim figures in that version were also off, by
small amounts on four of seven libraries (none by more than 1,232 lines), and
are replaced below with the numbers from main.

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
(dated 2025-10-29, with its storage decision reversed in place on 2025-11-03;
see decision F) documents bringing colonysim's Layer and Shape components into
worldsim in October 2025. Dual-deck-builder's engine is the third pass, and not
an independent one: between August and October 2026 it was rebuilt to
`docs/ui-rendering-spec/`, eighteen chapters of backend-agnostic spec
abstracted from worldsim's C++ so that a second implementation could be built
to it, with a C++ column in the module map and a conformance scorecard for both
(dual-deck-builder `docs/ui-rendering-spec/01-architecture.md`,
`14-testing-and-conformance.md`). Its code has a single `Component` base class
with Rectangle, Circle, Line, Polygon, Triangle and Text as leaf subclasses and
"layer" meaning a named z-layer rather than a positioning class
(`src/renderer/engine/components/Component.ts`), but the retained tree, the
immediate-mode draw API and the batcher underneath are the same shape as
worldsim's.

Same architecture, three implementations, two languages, and now a written spec
for the UI half of it. A fourth game would make it four.

## What exists

### Worldsim, C++20 and OpenGL

Counted on main at `db335a2b`, `.h` and `.cpp` only, tests and benchmarks
included.

| Library | Files | Lines | Generic? |
|---|---|---|---|
| `foundation` | 45 | 6,739 | Yes, once the HTTP dev server leaves it (it is in `foundation/debug/` today, and foundation links OpenGL and httplib for it) |
| `renderer` | 69 | 10,021 | Yes, once `FontRenderer` moves in from `ui`; `Primitives.cpp` already includes it |
| `ui` | 100 | 19,469 | Mostly. Audited: it reaches into `engine` through two small headers and nothing else |
| `engine` | 294 | 75,113 | **Mixed.** Scenes, lifecycle and input are generic (16 files, 2,902 lines); the ECS, colonists, chunks, pathfinding, tasks and crafting are not. The figure bundles two CMake targets: `engine` alone is 236 files and 60,938 lines, and `assets` (58 files, 14,175 lines) is its own target and stays behind |
| `world` | 79 | 21,460 | No |
| `geometry` | 67 | 18,141 | No |
| `planet-view` | 16 | 1,942 | No |
| `game-systems` | 1 | 2 | A placeholder `world-sim` still links |

Tests are a large share of those numbers: 2,545 of foundation's lines, 2,422 of
renderer's, 5,731 of ui's, and 29,873 of engine's (40%).

`ui` breaks down as `components` 12,765, `layout` 1,712, `focus` 1,422, `font`
1,151, `debug` 592, `theme` 547, `component` 496, `shapes` 483, `layer` 176,
`input` 86. `renderer` as `vector` 3,941, `primitives` 2,784, `resources` 1,320,
`CoordinateSystem` 564, `shader` 533, `gl` 468, `metrics` 411.

### Dual-deck-builder, TypeScript and WebGL

Counted at `afcd7b16`, `.ts` and `.tsx`, tests included, with the non-test
figures alongside.

| Area | Files | Lines | Non-test lines |
|---|---|---|---|
| `src/renderer/engine` | 238 | 68,185 | 37,163 |
| `src/renderer/game` | 523 | 138,266 | 77,744 |
| `src/` total | 790 | 210,855 | 118,699 |

Engine: `ui` 16,573, `components` 10,442, `rendering` 8,893, `draw` 7,686,
`debug` 6,841, `input` 6,381, `text` 3,655, `services` 2,756, `style` 1,249,
`theme` 950, `animation` 926, `gpu` 854, `errors` 644, `coords` 335. Without
tests the three subsystems this document leans on are smaller than they look:
`input` 3,217, `debug` 2,896, `animation` 479.

Game: `screens` 35,075, `map` 25,416, `mechanics` 22,179, `ui` 21,933,
`campaign` 21,734, `ai` 5,661, `core` 3,740, `sim` 859, `art` 364, `data` 343.

**Its engine is already isolated.** Zero imports from game code across 238
files; the only specifiers that leave `engine/` are shader and font asset
files. The coupling runs the other way, game into engine, in 143 non-test
files, and `mechanics`, `ai` and `sim` import nothing from the engine at all.

**43.8% of its game layer is tests**: 204 files, 60,522 lines. That is the most
valuable asset in this whole plan, for reasons under the migration section.

### The asymmetry

Dual-deck-builder's engine is 68,185 lines against worldsim's 36,229 for
foundation, renderer and ui together (37,163 against 25,531 without tests on
either side). It is the larger implementation, and the first version of this
document called it the better one in places. The surveys sharpen that claim
subsystem by subsystem:

- Input. Dual-deck-builder's dispatcher, hit testing, focus manager and drag
  service are built to the spec and documented in decision records
  (`docs/AI_TECHNICAL_DECISIONS/input-dispatcher.md`, `focus-manager.md`,
  `drag-service.md`). Worldsim's is a polled singleton
  (`libs/engine/input/InputManager.h`) with keyboard routed to `ui` through a
  `FocusManager` side channel rather than through the scene. A real design
  gap, in dual-deck-builder's favour.
- Debug tooling. The same tree-snapshot schema, lint rules and injection
  grammar; the difference is transport. Worldsim serves them over HTTP from
  `libs/foundation/debug/DebugServer.cpp`, dual-deck-builder exposes them as
  browser window globals (`src/renderer/engine/debug/hooks.ts`) driven by
  Playwright, and has no screenshot capture at all. The portable part under
  both is the pure functions.
- Animation. Dual-deck-builder has a 356-line tween engine
  (`src/renderer/engine/animation/Animator.ts`), no timelines, sequences,
  springs or loops. Worldsim has nothing. Small, and net-new on the C++ side.
- Gamepad. Neither has it. Dual-deck-builder has scaffolding (a `controller`
  input mode, a `virtual` pointer type) and a deferred adapter spec; worldsim
  has zero hits for the word.
- Frame loop. Dual-deck-builder's is variable-step over
  `requestAnimationFrame` with no fixed timestep or accumulator
  (`src/renderer/engine/rendering/FrameLoop.ts`, delta clamped by
  `MAX_DELTA_SECONDS = 0.25` in `FrameTimer.ts:48`); the game is turn-based,
  so it gives no precedent for salal's frame contract.

Where worldsim keeps input and the dev server outside `libs/ui` (86 and 592
lines there; the InputManager is in `libs/engine`, the server in
`libs/foundation`), dual-deck-builder has `input`, `debug` and `animation` as
peers of rendering and UI inside one engine directory. That is the cleaner
line, and the spec the TS engine was built to is the single best input for
salal's `ui` design (decision C in [open-decisions.md](open-decisions.md)).

## The extraction problem

**Worldsim's generic code does not all live in `libs/`.** Whether `libs/ui` is
clean enough to move was the smaller question.

Scene management, lifecycle and input sit inside `libs/engine` mixed with the
game, and the HTTP dev server sits one layer lower still, in
`libs/foundation/debug/DebugServer.cpp`, which is why foundation links OpenGL
and httplib today (`libs/foundation/CMakeLists.txt:28-37`). There is no config
system to extract: `AppConfig` is a C++ struct filled with designated
initializers in each `main()`. Extract foundation, renderer and ui and you get
a drawing layer with no way to open a window, run a loop, switch scenes, or
read a gamepad.

So: **dual-deck-builder is the reference for where the boundary goes, worldsim is
the source of the code.** That framing survives the decision change and is the
most useful line in this document.

## Is this an engine, or a drawing library?

An engine, but a small and specific one. The test is **who owns the frame**,
not what is inside it. A drawing library is something your code calls. An engine
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

Physics stays per-game on purpose. In asteroids-roguelike the physics *is* the
game and lives in `sim`; dual-deck-builder has none. If a third game ever wants
the same rigid-body model, that is the moment to promote it, not before.

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
                             (worldsim's pugixml, lua and sol2 serve its
                             assets target and stay behind)
  libs/
    foundation/              math, platform, logging, arenas, resource handles,
                             seeded PRNG with forking
    renderer/                gl, shader, primitives, batch, vector, text,
                             fonts, coords, metrics
    ui/                      component, components, layout, focus, theme,
                             shapes, animation, inspector
    app/                     window, fixed-step loop, scenes, config,
                             input (keyboard, mouse, gamepad), dev server
  apps/
    sandbox/                 the ui-sandbox harness and its pure-UI scenes
  tests/
  docs/
    architecture.md          the four-layer stack, normative
    coding-standards.md      moved from worldsim
```

`libs/app/` is new and is the hard part: the generic slice carved out of
worldsim's `libs/engine` (16 files, 2,902 lines: `application/`, `scene/`,
`input/`, `clipboard/`), plus the dev server and its metrics plumbing relocated
up from `foundation` and `renderer` (another ~2,100 lines), plus the fixed-step
loop, gamepad input and PRNG fork API that [asteroids-roguelike/docs/tech-stack.md](https://github.com/kcjonson/asteroids-roguelike/blob/main/docs/tech-stack.md) found missing. Those belong to the
engine, because all three games want them. The file list with sizes is in
[app-carve-out.md](app-carve-out.md).

Determinism is a partial gap rather than an absent one. `libs/foundation/random/`
already has `Pcg32.h` and `SplitMix64.h` with `deriveSeed(seed, streamId)`,
deterministic and identical across x64, ARM and WASM by its own header comment.
What is missing is a named-stream `fork` API and the discipline: `std::mt19937`
is still used in `engine` and the apps outside worldgen. Which generator the
engine standardises on is decision A.

`animation` moves into `ui` from dual-deck-builder's design; worldsim has no
equivalent. `font` moves the other way, out of `ui` and into `renderer`:
worldsim's `renderer/primitives/Primitives.cpp:12` already includes
`ui/font/FontRenderer.h`, the one place renderer reaches up into ui, and
`FontRenderer` itself uses nothing else in ui. Moving it removes that knot and
gives the asteroids prototype's debug overlay text without the ui library.

The sandbox is the harness from worldsim's `apps/ui-sandbox` plus the scenes
that are pure UI. Roughly a third of its 27 scenes (Planet, Grass, Tree and the
Salvage scenes among them) link `world`, `planet-view` or `assets`
(`apps/ui-sandbox/CMakeLists.txt:42-51`) and stay in worldsim.

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

Input dispatch, the debug tooling's transport-free core and the tween animator
are where the TypeScript implementation came out ahead ("The asymmetry" says
by how much). Those designs get reimplemented in C++ inside `libs/app` and
`libs/ui`, and all three games get them. Gamepad support is not among them;
neither codebase has it.

This is engine work with payoff beyond dual-deck-builder, it happens during the
`app` phase, and it is the part of the migration that is unambiguously worth
doing on its own merits.

### Job B: its game layer, ~138,266 lines, into C++

The large and risky one. The port order matters more than anything else here, and
there is a very good first target.

**Start with the simulation core, knowing what that means.**
`src/renderer/game/sim/` is 740 non-test lines (859 with tests) and its spec
(`docs/specs/Balance Simulation.md`) says it is deliberately free of DOM,
`fetch`, localStorage and dev-tools hooks. It takes plain data in and emits
plain JSON out, and it is seeded with forking (`fork('run', n)`,
`fork('fight', f)`). But its static import closure is about 110 files and
32,900 lines: `map` 15,233, `mechanics` 8,097, `campaign` 5,020, `ai` 2,993,
`core` 814. Porting "the sim" is porting most of the game's logic; what it
leaves out is `screens`, game `ui`, `art` and `Game.ts`.

The spec requires that the same config and seed give the same JSON in Node and
in the browser. Today that is a requirement, not a demonstrated property: the
only runner is `node scripts/balance-sim.mjs` with modes `fights` and `gen`,
the browser Balance Lab the spec describes has not been built, no workflow
invokes the sim, and no golden JSON corpus is committed. The anchors that do
exist are pinned test outcomes (`Fights.test.ts` on seed 7, `Gen.test.ts` on
seed 1), the Rng golden vectors, and the baseline tables in the spec.

So a C++ third implementation can be held to exactly that bar, once the bar is
fixed in a file. Generate a corpus from `--json` and commit it in
dual-deck-builder (decision J), port the closure, run both on identical configs
and seeds, diff the JSON. That is differential testing for the price of one
script, and it means the riskiest question in the whole migration (does the
game logic port faithfully?) gets answered mechanically rather than by
playtesting. It also pins the engine's PRNG: bit-identical output needs
dual-deck-builder's sfc32 and MurmurHash3-based `fork(name, attempt)`
(`src/renderer/game/core/Rng.ts`), which is decision A.

The sim core plays through the game's real code rather than a copy of it, which
is what makes this work: verifying the sim verifies `mechanics`, `campaign`,
`map` and `ai` underneath it, in dependency order `core`, `mechanics`,
`campaign`, `map`, `ai`, each checkable on identical seeds before anything is
drawn.

**Presentation last.** `screens` (35,075) and game `ui` (21,933), 57,008 lines
with tests and 37,044 without, are the only part that needs the shared engine
to exist. They come after Job A and after the logic layers are known good.

**The 60,522 lines of tests are the spec.** They port alongside their subjects and
they are what makes a cross-language rewrite of this size tractable at all. A
port with no oracle is a rewrite; a port with 204 test files and a seeded
differential harness is a translation.

### What the migration costs

The web playtest deploy. Dual-deck-builder currently pushes to
`bearcavinteractive.com/playtest/dual-deckbuilder/` on every merge to main
(`.github/workflows/deploy-sftp.yml`), and that is its primary playtest channel
with Electron builds secondary. You cannot send someone a link to a C++ binary.

**WebAssembly puts it back**, which is the single best argument for the section
below and changes this from an accepted regression to a temporary one. The
engine is a clean Emscripten target and both new games are good fits.

Until the web build exists, the gap is partly covered by worldsim's HTTP dev
server: screenshot capture, element-tree inspection, layout linting and
synthetic input injection, five of its twelve routes (the rest are health,
metrics, a dev-command bag, a state handshake, two SSE streams and an index
page; architecture.md lists them). That serves agent-driven verification well
and human playtesting not at all. For humans it is a distributed build instead
of a URL.

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

- Twelve shader files, 1,473 lines of GLSL. Eight carry `#version 330 core`
  (`uber.*` and `tile.*` in `libs/renderer/shaders/`, `planet.*` and `blit.*`
  in `libs/planet-view/shaders/`, which stays in worldsim) and four are
  headerless includes (`hash_noise`, `instancing`, `land`, `water` under
  `libs/renderer/shaders/includes/`, 938 of the lines) spliced in by a custom
  preprocessor (`libs/renderer/shader/ShaderPreprocessor.cpp`). For the
  renderer that moves, the port is a version header and precision qualifiers
  in four files; the include mechanism carries over unchanged.
- No geometry or compute shaders anywhere, no `glMapBuffer`, and all
  fifteen `glVertexAttribPointer` sites pass VBO offsets. WebGL2 has no
  geometry stage, and that is the usual blocker. It is absent. The one
  `GL_RGBA32F` use is a framebuffer colour attachment in
  `libs/engine/world/rendering/SurfaceFieldGolden.test.cpp:257` (attached at
  `:262`), a test of code that stays in worldsim; nothing that moves uses
  float render targets, so `EXT_color_buffer_float` is not needed.
- The GL 3.3 core context is created in two production sites:
  `libs/engine/application/AppLauncher.cpp:137-154` (hints, window,
  `glewInit`), a file headed for `libs/app`, and `apps/asset-cli/Main.cpp:134-149`,
  which makes its own hidden context and stays in worldsim. Five tests open
  windows too, four with GL contexts, two of those in libraries that move
  (`libs/renderer/resources/RenderToTexture.test.cpp:13-25`,
  `libs/ui/font/FontRenderer.test.cpp:29-39`). So the platform branch lands in
  one file in the engine, plus two test fixtures.
- GLM, nanosvg, stb and nlohmann-json are header-only or pure C/C++.

### Toolchain

Emscripten with `--use-port=contrib.glfw3` (its own GLFW3 implementation,
available since Emscripten 3.1.55), linking with `-sUSE_WEBGL2=1 -sFULL_ES3=1
-sALLOW_MEMORY_GROWTH=1`.

### Porting items

Each is small and each lives in one place, which is the point.

- GLEW compiles out. WebGL needs no extension loader.
- Shaders become `#version 300 es` with explicit precision qualifiers.
- The main loop inverts. `emscripten_set_main_loop` drives the frame instead
  of a `while` loop owning the program.
- The dev server compiles out. cpp-httplib cannot listen in a browser.
  Worldsim has no compile flag for it today, only the runtime
  `AppConfig::enableDebugServer` and `--http-port` (`DebugServer.cpp` has no
  `#if` at all), so the flag is something the extraction adds.
- Threads. In worldsim they live in `foundation` (the dev server's
  `std::thread` at `debug/DebugServer.cpp:260`, and a `threading/TaskPool`), in
  `engine`'s assets, ECS and chunk code (`std::thread`, `std::async`), in
  `planet-view`, and in `world` (`std::jthread`). `Application.cpp` only sleeps
  for frame pacing, and `renderer` and `ui` have nothing beyond `thread_local`
  scratch buffers. On the web they need SharedArrayBuffer plus COOP/COEP
  headers; neither new game needs them, so the simplest answer is a
  single-threaded web path.
- File I/O goes through preloading or MEMFS rather than the filesystem.
- `FULL_ES3` emulation costs performance wherever client-side arrays or
  `glMapBuffer` are used. Use VBOs and the flag stops mattering.

### Per game

- asteroids-roguelike: ideal. 2D, single screen, a few hundred flat shapes,
  no worldgen, no large assets.
- dual-deck-builder: good. UI-heavy and simulation-light, and a web build
  restores exactly the playtest channel the C++ migration takes away.
- worldsim: no, and it does not need one. A 1.3 GiB planet file and threaded
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
- Put the dev server behind a compile flag from the first commit. Worldsim has
  none (the server is gated at runtime only), so this is new discipline rather
  than carried-over discipline.
- Load assets through an indirection that can be MEMFS. Worldsim's only seam
  is `foundation/utils/ResourcePath`; every reader after it hits the
  filesystem directly.

All cheap upfront. All expensive to retrofit once three games depend on the API.

### Not verified

- Whether msdfgen runs at asset-build time or at runtime. At runtime it has to
  compile to WASM, which is probably fine and is unchecked.
- Download size. A WASM binary plus SDF atlases plus SVG assets is likely a few
  megabytes, acceptable for playtesting and unmeasured.
- Whether the web build gets gamepad support. The browser Gamepad API exists
  and Emscripten's GLFW port maps to it, but couch co-op in a browser tab is an
  odd proposition and may not be worth the verification effort.
- Audio, below.

### Audio is net-new

Worldsim has **no audio library at all**: nothing in `vcpkg.json`, nothing in
`libs/`. So the priority mix with ducking, the stem layering driven by
`pressure`, and the thrust loop whose pitch and filter track mass and velocity
in [asteroids-roguelike/docs/art-audio.md](https://github.com/kcjonson/asteroids-roguelike/blob/main/docs/art-audio.md) (`:72-93`) are engine work nobody has
started, and they are a fourth gap alongside the fixed-step loop, gamepad input
and determinism. That doc asks for parameter-driven playback, not synthesis;
"procedural SFX" appears only in `tech-stack.md:138-139`.

The WebAssembly target constrains the choice. miniaudio and OpenAL-soft both have
Emscripten backends over Web Audio; picking one of those keeps the web build
possible, and picking something platform-native closes it off. Whether audio
lives in the engine and which library it uses is decision B; the recommendation
is a new `audio` library over miniaudio, scheduled after `app` because the
asteroids feel prototype wants audible thrust.

## Sequencing

Designed so nothing blocks on anything it does not have to. Phase 0 is done,
and its answer put a step in front of phase 1.

### Phase 0, the audit (done 2026-10-25)

The question was whether `ui` respects worldsim's documented layering or
reaches into `engine`. It reaches in, narrowly: ten includes in eight files,
all resolving to two small engine headers, `input/InputTypes.h` (a 133-line
include-free enum header for `engine::Key` and `engine::MouseButton`) and
`clipboard/ClipboardManager.h` (a GLFW singleton that TextInput calls for cut,
copy and paste), and two of the ten are dead includes of `InputManager.h`. `ui` never touches Scene,
Application, the ECS, chunks, world, geometry or assets. The audit also found
a knot the first version of this document missed, running the other way:
`renderer/primitives/Primitives.cpp:12` includes `ui/font/FontRenderer.h` and
`Primitives.h:35` forward-declares `ui::FontRenderer` in its public API, so
renderer cannot be extracted on its own until FontRenderer moves. And the CMake graph is
worse than the include graph: `libs/ui/CMakeLists.txt` links all of `engine`
PUBLIC plus an httplib it never includes. Every file and line is in
[phase-0-audit.md](phase-0-audit.md). The extraction is not gated by ui's
design; it is gated by two header moves, one clipboard seam and CMake cleanup.

### Phase 0.5, untangle worldsim

Small PRs in worldsim, each leaving it building and running identically, in
roughly this order:

- Delete the two dead `InputManager.h` includes in `ui`
  (`components/button/Button.cpp:10`, `components/TextInput/TextInput.cpp:10`).
- Move `input/InputTypes.h` below `ui`: into `foundation` if decision D goes as
  proposed, into `ui` otherwise. Engine's `InputManager.h` includes it from
  there. The `engine::` namespace can stay for the first pass.
- Give `ui` a clipboard seam, a small interface or `std::function` hooks that
  `Application` installs, so TextInput stops calling
  `engine::ClipboardManager::Get()` (`TextInput.cpp:460,470,478`).
- Move `FontRenderer` from `libs/ui/font` to `libs/renderer/font`, namespace
  `renderer`, and drop renderer's PRIVATE `../ui` include dir.
- CMake: `ui` drops `engine` and `httplib`, `engine` drops the `nlohmann_json`
  it never includes. Re-run the include resolver and confirm `ui -> engine` and
  `renderer -> ui` are both zero.
- Move `DebugServer`, `PngEncoder`, `LockFreeRingBuffer` and
  `PerformanceMetrics` out of `foundation` into `libs/engine` (the proto-app),
  and invert `Logger::setDebugServer` into a generic sink callback. Foundation
  drops OpenGL and httplib. This one can slip into phase 1, but it has to land
  before foundation is extracted.

### Phase 1, the skeleton, then `foundation` and `renderer`

CMakeLists, `CMakePresets.json` copied, `vcpkg.json` without pugixml, lua and
sol2, CI on Windows, macOS and Linux plus an Emscripten link-only job. Then the
two generic libraries move and worldsim consumes them via FetchContent pinned
to a tag, still building and running identically. Four renderer design items
have to be right before anyone depends on them, because they shape the batch
sort key and the shader layout: blend state in the sort key, an offscreen
render target API, the shader version-swap discipline, and polygon, arc and
wide-line primitives (decision G).

### Phase 2, carve out `app`

The 16-file slice (`application/`, `scene/`, `input/`, `clipboard/`) plus what
the consumers need from a frame owner and worldsim never built:

- The `tick()` split and a fixed-step accumulator. Worldsim's loop is
  variable-step with a 120 FPS sleep-and-spin cap, but the body of
  `Application::run()` is already one frame's work
  (`libs/engine/application/Application.cpp:112-306`), so the split is
  mechanical; the accumulator is net-new.
- `InputSnapshot` and gamepad input with hot-plug and device-to-player-slot
  claiming. Worldsim has keyboard and mouse only.
- The clipboard, behind the seam from phase 0.5.
- A scene manager with a stack, overlays and typed payloads.
  `IScene::exportState()` is deleted on the way; it serves a route that no
  longer exists.
- A config file loader. Net-new: worldsim's `AppConfig` is a struct, and its
  only "config" is game XML under `assets/config/` parsed by game registries.
- An asset byte-provider that can be MEMFS. Net-new: `ResourcePath` is the
  only seam today and every reader after it hits the filesystem directly.
- The dev server, relocated from `foundation`, behind a compile flag, with
  game-registrable endpoints, tick stepping and pad injection.
- The PRNG fork API (decision A) in `foundation/random`.
- The sandbox harness.

Job A of the migration lands here. Worldsim consumes. The asteroids feel
prototype can start here.

### Phase 2b, audio

Decision B. After `app`, before `ui`, because the feel prototype wants audible
thrust and nothing in `ui` depends on it.

### Phase 3, extract `ui`

Largest and least certain, and nothing upstream waits on it. Against the
adopted spec (decision C), with multi-device focus (I) and the storage model
(F) settled on the way, and the spec's conformance scorecard as the acceptance
test.

### Phase 4, dual-deck-builder Job B

Tracked in dual-deck-builder's own project. Prerequisite there: the golden JSON
corpus (J). "Sim first" means the ~33k-line logic closure, verified by
differential JSON; presentation (`screens` 22,295 and game `ui` 14,749
non-test lines) last.

In parallel and blocked by none of it: the feel prototype for
asteroids-roguelike, which needs `foundation`, enough of `renderer` to draw a
few hundred flat shapes and the debug overlay's text, and the loop. No UI layer
at all.

The **WebAssembly target is a set of constraints, not a phase**, observed from
phase 1 onward (see "Design for it now"), with a link-only CI job from
phase 1. The first actual web build is worth attempting as soon as the
prototype draws something, because a browser build that works on a white
triangle and a dozen circles proves the toolchain before there is anything
complicated to debug.

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

The mitigation is also the opportunity: **asteroids-roguelike is the genericity
test.** No tiles, no chunks, no 3D, no pathfinding. If the engine serves it
without contortion the engine is generic; if it fights, that is the API talking
while there is still only one real consumer. Dual-deck-builder is the second
test and a harder one, since it is UI-heavy where asteroids is
simulation-heavy.

**Extraction stalls every game and ships nothing.** The sequencing exists for
this. Phases 0.5 and 1 are small PRs, phase 3 is optional for a long time,
phase 4 is its own project, and the prototype runs throughout.

**Versioning.** Three consumers, one developer, pinned tags. Manageable now,
becomes real with a second developer.

**The web build rots if it is not in CI.** A third build configuration that
nobody compiles breaks silently and then costs a day to revive. If the
WebAssembly target is worth having, it belongs in the engine repo's CI from the
first commit that supports it, even when all it does is link.

## Open

The questions that need Kevin are in [open-decisions.md](open-decisions.md),
each with a recommendation, the evidence, and the phase that needs the answer,
and each tracked on the board. By letter:

- A. The engine PRNG: dual-deck-builder's sfc32 with MurmurHash3 `fork`, or
  worldsim's Pcg32 with `deriveSeed`.
- B. Audio: in the engine or per game, and which library.
- C. Adopt dual-deck-builder's `docs/ui-rendering-spec/` as the normative spec
  for `ui`.
- D. Where input types live, which also settles the old app-versus-input split.
- E. The ECS core: ship it as an optional header module or leave it in worldsim.
- F. The `ui` storage model during extraction, shared_ptr graph or value
  semantics.
- G. Render targets, additive blending and particles: who owns them and when.
- H. Whether worldsim's vector/SVG asset conventions come with `renderer` or
  get parameterised out.
- I. Multi-device focus for the asteroids co-op shop.
- J. The differential harness for dual-deck-builder: a committed golden JSON
  corpus, which is work in that repo.

Resolved since the first version: the phase 0 audit (done, above), whether this
document moves into the salal repo (it did), and whether work is tracked in
Specboard (it is; `.mcp.json` holds the binding). The smaller unknowns, msdfgen
at build time or run time, WASM download size, and web gamepad support, stay
under "Not verified" above.
