# Extracting a shared engine

Surveyed 2026-10-25, revised the same day after Kevin's decision to migrate
dual-deck-builder to C++, and revised again the same day after the Phase 0
surveys. Updated 2026-10-26 with what phase 0.5 landed in worldsim, and again
with how phase 1 is being executed and what scoping it turned up.

This is the engine repo. `foundation` is imported (a plain copy of worldsim at
`870dd755`); `renderer`, `ui` and `app` are placeholders until their phases
land. "Sequencing" below says where each stands.

## Corrections in this revision (2026-10-25)

Checked by five read-only surveys against worldsim main `db335a2b`,
dual-deck-builder `afcd7b16` and asteroids-roguelike `176064c`, and fixed in
place: dual-deck-builder's engine imports zero game code (not one) and was
rebuilt to a spec abstracted from worldsim; its 740-line sim core drags a
~32,900-line import closure; worldsim's dev server lives in `foundation`, has
twelve routes and no compile flag; worldsim already has a deterministic PRNG
and lacks only the fork API; Phase 0 is done and adds an untangling step
before phase 1.
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
| `foundation` | 45 | 6,739 | Yes. The HTTP dev server was in `foundation/debug/` when counted, with OpenGL and httplib linked for it; it moved to `libs/engine/debug` on 2026-10-25/26 ([worldsim#297](https://github.com/kcjonson/worldsim/pull/297)) and foundation now links only glm |
| `renderer` | 69 | 10,021 | Yes. `FontRenderer` moved in from `ui` on 2026-10-25/26 ([worldsim#293](https://github.com/kcjonson/worldsim/pull/293)); `Primitives.cpp` already included it |
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
game, and the HTTP dev server sat one layer lower still, in
`libs/foundation/debug/DebugServer.cpp`, which is why foundation linked OpenGL
and httplib (`libs/foundation/CMakeLists.txt:28-37`); it has since moved to
`libs/engine/debug` ([worldsim#297](https://github.com/kcjonson/worldsim/pull/297)), and foundation links only glm. There is no config
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
                             stb nanosvg cpp-httplib gtest benchmark
                             (worldsim's pugixml, lua and sol2 serve its
                             assets target and stay behind; msdfgen serves
                             the font atlas tool, also behind, and leaves
                             with the renderer import)
  libs/
    foundation/              math, platform, logging, arenas, resource handles,
                             seeded PRNG with forking, input types
    renderer/                gl, shader, primitives, batch, vector, text,
                             font renderer (no font files), coords, metrics
    ui/                      component, components, layout, focus, theme,
                             shapes, animation, inspector
    app/                     window, fixed-step loop, scenes, config,
                             device input (keyboard, mouse, gamepad),
                             dev server
  apps/
    sandbox/                 the ui-sandbox harness and its pure-UI scenes
  tests/
  docs/
    architecture.md          the four-layer stack, normative
    coding-standards.md      moved from worldsim
```

Code arrives by plain copy, not a history rewrite: one commit per library,
named `Import <lib> from worldsim @ <sha>`, byte-identical to worldsim at that
commit, with the clang-format pass as its own commit after it. History stays in
worldsim, where blame for an imported file is
`git log 870dd755 -- libs/foundation`. No filter-repo; a plain copy can be
checked against its source line for line.

Fonts stay with the game. Worldsim keeps `fonts/`, `tools/generate_sdf_atlas`,
the font-atlas target and its three copy steps; salal
ships no fonts and `msdfgen` leaves its manifest. `FontRenderer` moves as it is,
`Initialize()` included. An API that takes atlas sources and stops calling
`std::exit` is a phase 2 follow-up, and is what is left of decision H.

`libs/app/` is new and is the hard part: the generic slice carved out of
worldsim's `libs/engine` (16 files, 2,902 lines: `application/`, `scene/`,
`input/`, `clipboard/`), plus the dev server and its metrics plumbing relocated
up from `libs/engine/debug` (it left `foundation` in phase 0.5) and `renderer`
(another ~2,100 lines), plus the fixed-step
loop, gamepad input and PRNG fork API that [asteroids-roguelike/docs/tech-stack.md](https://github.com/kcjonson/asteroids-roguelike/blob/main/docs/tech-stack.md) found missing. Those belong to the
engine, because all three games want them. The file list with sizes is in
[app-carve-out.md](app-carve-out.md).

Determinism is a partial gap rather than an absent one. `libs/foundation/random/`
already has `Pcg32.h` and `SplitMix64.h` with `deriveSeed(seed, streamId)`,
deterministic and identical across architectures by its own header comment.
What is missing is a named-stream `fork` API and the discipline: `std::mt19937`
is still used in `engine` and the apps outside worldgen. Which generator the
engine standardises on is decision A.

Found along the way: worldsim's headers claim bit-identical results across x64,
ARM and WASM; that had never been tested on ARM. The first macOS (arm64) run of
foundation's tests failed three golden-bit tests by one ULP, `DeterministicMath`
cos and `HashNoise` value and gradient noise. Clang fuses `a*b+c` into an FMA
wherever the target has one (always on arm64, on x64 only with `-mfma`), and the
goldens were computed without fusion. `salal_foundation` now sets
`-ffp-contract=off` PUBLIC for GCC, Clang and AppleClang (not clang-cl); the
math is header-inline, so every consumer's translation units need the flag and
inherit it. It is an engine-wide rule now (Settled, in
[open-decisions.md](open-decisions.md)).

`animation` moves into `ui` from dual-deck-builder's design; worldsim has no
equivalent. `font` moves the other way, out of `ui` and into `renderer`:
worldsim's `renderer/primitives/Primitives.cpp:12` already includes
`ui/font/FontRenderer.h`, the one place renderer reaches up into ui, and
`FontRenderer` itself uses nothing else in ui. Moving it removes that knot and
gives the asteroids prototype's debug overlay text without the ui library. The
move landed in worldsim as [worldsim#293](https://github.com/kcjonson/worldsim/pull/293).

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

Worldsim's side, as scoped for phase 1: FetchContent over https with
`GIT_TAG v0.1.0` and `GIT_SHALLOW`, every `SALAL_BUILD_*` option OFF so only the
libraries build, and `FETCHCONTENT_SOURCE_DIR_SALAL` pointed at a local checkout
for iteration. No manifest changes and no include-line changes in worldsim; the
imported code keeps its include spellings and its namespaces. The libraries are
`salal_<lib>` targets with `salal::<lib>` aliases, and a configure-time check
fails the configure on an upward dependency, or on GL, GLFW or httplib reaching
`foundation`.

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

The playtest URL stays the TypeScript build until parity. That build is also
the differential oracle (next section), so it stays anyway, and the C++ build
is playtested natively: a distributed binary for humans, plus worldsim's HTTP
dev server for agent-driven verification (screenshot capture, element-tree
inspection, layout linting and synthetic input injection, five of its twelve
routes; the rest are health, metrics, a dev-command bag, a state handshake,
two SSE streams and an index page, and architecture.md lists them).

### One rule that does not apply here

Worldsim's CLAUDE.md says to delete old code aggressively and never leave legacy
fallbacks. That is right within a language and wrong across this migration: the
TypeScript implementation is the oracle for the differential tests and has to
stay until parity. Deleting it early throws away the only thing that proves the
port correct.

## WebAssembly, dropped

A browser build was on the table as a playtest channel for the C++ migration,
to put dual-deck-builder's playtest URL back. Kevin dropped it on 2026-10-26:
it was only ever a temporary playtest idea, and it had started shaping the
engine. The GL ES 3.0 subset rule had pushed decision G to LDR-only render
targets, and the Emscripten CI job had cost two vcpkg commits.
Dual-deck-builder's TypeScript build remains the playtest URL during the
migration; any future web build is a best-effort experiment in that repo, with
no engine guarantees.

## Audio is net-new

Worldsim has **no audio library at all**: nothing in `vcpkg.json`, nothing in
`libs/`. So the priority mix with ducking, the stem layering driven by
`pressure`, and the thrust loop whose pitch and filter track mass and velocity
in [asteroids-roguelike/docs/art-audio.md](https://github.com/kcjonson/asteroids-roguelike/blob/main/docs/art-audio.md) (`:72-93`) are engine work nobody has
started, and they are a fourth gap alongside the fixed-step loop, gamepad input
and determinism. That doc asks for parameter-driven playback, not synthesis;
"procedural SFX" appears only in `tech-stack.md:138-139`.

Whether audio lives in the engine and which library it uses is decision B; the
recommendation is a new `audio` library over miniaudio (single header,
permissive licence, cross-platform), scheduled after `app` because the
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

Done 2026-10-25/26, eight PRs leaving worldsim main at `870dd755`:
[worldsim#292](https://github.com/kcjonson/worldsim/pull/292) (dead includes),
[worldsim#294](https://github.com/kcjonson/worldsim/pull/294) (clipboard seam), [worldsim#293](https://github.com/kcjonson/worldsim/pull/293) (`FontRenderer`), [worldsim#295](https://github.com/kcjonson/worldsim/pull/295) (CMake),
[worldsim#297](https://github.com/kcjonson/worldsim/pull/297) (dev server) and [worldsim#298](https://github.com/kcjonson/worldsim/pull/298) (`InputTypes.h` into `foundation/input/`,
decision D, with `engine` dropped from `ui`'s link list), plus [worldsim#291](https://github.com/kcjonson/worldsim/pull/291) (the include
resolver) and [worldsim#296](https://github.com/kcjonson/worldsim/pull/296) (asset bootstrap out of `AppLauncher`). The
resolver reports `ui -> engine`, `renderer -> ui` and `foundation -> engine` all
at zero. The dev server now lives in `libs/engine/debug`, so phase 2's "dev
server moves into app" is a move from engine, not from foundation. The list
below is the plan as written.

Small PRs in worldsim, each leaving it building and running identically, in
roughly this order:

- Delete the two dead `InputManager.h` includes in `ui`
  (`components/button/Button.cpp:10`, `components/TextInput/TextInput.cpp:10`).
- Move `input/InputTypes.h` below `ui`, into `foundation/input/` (decision D).
  Engine's `InputManager.h` includes it from there. The `engine::` namespace
  can stay for the first pass.
- Give `ui` a clipboard seam, a small interface or `std::function` hooks that
  `Application` installs, so TextInput stops calling
  `engine::ClipboardManager::Get()` (`TextInput.cpp:460,470,478`).
- Move `FontRenderer` from `libs/ui/font` to `libs/renderer/font`, namespace
  `Renderer`, and drop renderer's PRIVATE `../ui` include dir. `Renderer`
  rather than the standards doc's lowercase because FontFamily, Primitives,
  BatchRenderer, Shader, CoordinateSystem, metrics/, gl/ and resources/Tile*
  are all `namespace Renderer` (only vector/ and two resources/ files are
  lowercase); a lowercase FontRenderer would force `Renderer::FontFamily`
  qualifiers and a second namespace block in Primitives.h, the shape being
  removed. Settling renderer's lib-wide namespace case (`Renderer` vs
  `renderer`; the standards doc says lowercase) is a worldsim follow-up.
- CMake: `ui` drops `engine` and `httplib`, `engine` drops the `nlohmann_json`
  it never includes. Re-run the include resolver and confirm `ui -> engine` and
  `renderer -> ui` are both zero.
- Move `DebugServer`, `PngEncoder`, `LockFreeRingBuffer` and
  `PerformanceMetrics` out of `foundation` into `libs/engine` (the proto-app),
  and invert `Logger::setDebugServer` into a generic sink callback. Foundation
  drops OpenGL and httplib. This one can slip into phase 1, but it has to land
  before foundation is extracted.

### Phase 1, the skeleton, then `foundation` and `renderer`

Under way. The skeleton (CMake, `CMakePresets.json` copied, a `vcpkg.json`
without pugixml, lua and sol2, CI on Windows, macOS and Linux) was in before
the first step below. Then the two generic libraries move and worldsim consumes
them via FetchContent pinned to a tag, still building and running identically.
The order is small PRs alternating between the two repos, with worldsim's
pre-moves first so each import is a pure move. State as of 2026-10-26:

1. Skeleton hardening, salal [#7](https://github.com/kcjonson/salal/pull/7). `salal_<lib>` targets with
   `salal::<lib>` aliases, `SALAL_BUILD_UI/APP/SANDBOX/TESTS` options that
   default on only for a top-level build, and a configure-time layering check
   that also keeps `foundation` GL-free. Done, merged as `a2144ac0`.
2. Foundation import, salal [#8](https://github.com/kcjonson/salal/pull/8). A byte-identical plain copy of worldsim
   @ `870dd755` (38 files; the placeholder `Foundation.*` deleted), with the
   clang-format pass as a separate commit because 24 of the 38 files were out of
   line with the shared `.clang-format`. Done, merged as `e7e2da06`.
3. Worldsim pre-move, worldsim [#299](https://github.com/kcjonson/worldsim/pull/299). The tile-pass shaders move into
   `libs/engine/world/rendering/shaders` so the renderer ships only its own;
   the renderer's GL test fixtures now request a 3.3 core context; each app
   mirrors shaders beside its exe on every build. Done, merged as `2cc7e40a`.
4. Renderer import, salal. From worldsim @ `2cc7e40a`: 79 files, the
   placeholder `Renderer.*` deleted, stb through `find_package`, msdfgen out of
   the manifest. The GL fixtures skip without a context, so headless CI passes
   and a Windows machine with a display runs them. In progress.
5. Tag `v0.1.0`. Next.
6. Worldsim consumes, worldsim. Delete `libs/foundation` and `libs/renderer`,
   add the FetchContent block, move about a dozen `target_link_libraries` lines
   to the `salal::` targets, point the four `<app>-shaders` targets'
   `add_dependencies` at `salal_renderer_shaders` (asset-cli, asset-manager,
   ui-sandbox and world-sim), fix the tests-heavy path filter and the include
   resolver's roots. Verify with a full Windows build, ctest, ui-sandbox
   screenshots on text_shapes, sdf_minimal, svg and button plus the game scene,
   identical to the pre-branch shots, and green CI. Next.
7. The GL-boundary PRs, salal, after step 6 so worldsim pins a tag before the
   API moves underneath it; tag `v0.2.0` when they are done. Next.

Settled while scoping, beyond the import method and fonts under "What the repo
looks like":

- The renderer ships three shader files, `uber.vert`, `uber.frag` and
  `includes/instancing.glsl`, staged to `${CMAKE_BINARY_DIR}/shaders` (the
  top-level build directory, even when salal is a subproject). The tile pass is
  the game's, which is why it moves first.
- `DEVELOPMENT_BUILD` stays a PUBLIC per-config definition on
  `salal_foundation`, because `Log.h` compiles the debug levels out without it.
  The rename to `SALAL_DEVELOPMENT_BUILD` comes before a third consumer.
- `salal_add_tests` also builds `salal_<lib>_benchmarks`, unregistered with
  ctest.

The GL boundary is a phase 1 design item, implemented after the import as its
own PRs. Worldsim's renderer has about 180 direct GL calls in about ten files
(measured on worldsim main @ `870dd755`, before the renderer import), and
`BatchRenderer.h` and `FontRenderer.h` put `<GL/glew.h>` in front of every
includer; the measurements are under decision G in
[open-decisions.md](open-decisions.md). The target is a backend boundary with
OpenGL 3.3 core as the first backend and Vulkan possible later. The order is GL
off the public surface (about 15 files), then every texture through
`gl/GLTexture`, then the offscreen render-target API designed as a backend
object. The full backend interface is a later refactor. Blend state in the batch
sort key, the offscreen render-target API, and the polygon, arc and wide-line
primitives must land before a second consumer depends on the renderer API, which
is after worldsim pins `v0.1.0` (worldsim consumes the renderer as it is,
behaviour-identical, and takes API changes through a tag bump). Blend state and
the primitives shape the batch sort key and the shader layout.

Follow-ups recorded and kept out of the moves, so the imports stay pure:

- `FontRenderer`: an atlas-sources API, no `std::exit`, data-driven families
  (the neighbourhood of decision H).
- `ResourcePath`: drop the `cwd/build/apps/ui-sandbox` search, implement the
  Linux `getExecutableDir`.
- `MetricsCollector` and `PerformanceMetrics`: tile, entity and visible-chunk
  counts belong in a game-supplied struct, and `PerformanceMetrics` is still
  `namespace Foundation`.
- `Primitives`' tile atlas (`setTileAtlas`) and `TileAtlasBuilder`'s hardcoded
  path are worldsim-shaped.
- The namespace case split (`Foundation` and `foundation`, `Renderer` and
  `renderer`), across both repos.
- `libs/game-systems` links `renderer` and uses nothing from it; worldgen-cli
  and geometry barely touch foundation.
- `RenderToTexture`'s `glGetIntegerv` save and restore.

### Phase 2, carve out `app`

The 16-file slice (`application/`, `scene/`, `input/`, `clipboard/`) plus what
the consumers need from a frame owner and worldsim never built:

- The `tick()` split and a fixed-step accumulator. Worldsim's loop is
  variable-step with a 120 FPS sleep-and-spin cap, but the body of
  `Application::run()` is already one frame's work
  (`libs/engine/application/Application.cpp:112-306`), so the split is
  mechanical; the accumulator is net-new.
- Gamepad input with hot-plug and device-to-player-slot claiming, and GLFW
  callbacks feeding the accumulator that produces `InputSnapshot`. The types,
  accumulator and `ActionMap` live in `foundation/input/` (decision D); `app`
  owns the device layer. Worldsim has keyboard and mouse only.
- The clipboard, behind the seam from phase 0.5.
- A scene manager with a stack, overlays and typed payloads.
  `IScene::exportState()` is deleted on the way; it serves a route that no
  longer exists.
- A config file loader. Net-new: worldsim's `AppConfig` is a struct, and its
  only "config" is game XML under `assets/config/` parsed by game registries.
- A byte-provider seam between asset lookup and file reads, only if a consumer
  needs one; today worldsim's `findResourceString` path search is enough.
- The dev server, relocated from `libs/engine/debug`, behind a compile flag, with
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

## Open

The questions that need Kevin are in [open-decisions.md](open-decisions.md),
each with a recommendation, the evidence, and the phase that needs the answer,
and each tracked on the board. By letter:

- A. The engine PRNG: dual-deck-builder's sfc32 with MurmurHash3 `fork`, or
  worldsim's Pcg32 with `deriveSeed`.
- B. Audio: in the engine or per game, and which library.
- C. Adopt dual-deck-builder's `docs/ui-rendering-spec/` as the normative spec
  for `ui`.
- E. The ECS core: ship it as an optional header module or leave it in worldsim.
- F. The `ui` storage model during extraction, shared_ptr graph or value
  semantics.
- G. Render targets, additive blending and particles: who owns them and when.
- H. Whether worldsim's vector/SVG asset conventions come with `renderer` or
  get parameterised out, and what API `FontRenderer` takes for atlas sources
  (the fonts themselves stay in the game).
- I. Multi-device focus for the asteroids co-op shop.
- J. The differential harness for dual-deck-builder: a committed golden JSON
  corpus, which is work in that repo.

Resolved since the first version: the phase 0 audit (done, above), whether this
document moves into the salal repo (it did), whether work is tracked in
Specboard (it is; `.mcp.json` holds the binding), the browser build (dropped,
above), where input types live (decision D, 2026-10-26: `foundation/input/`,
with the poller in `app`), and, 2026-10-26, that imports are plain copies, that
fonts and the atlas tool stay game-side, that floating-point contraction is off
engine-wide, and that the renderer sits behind a backend boundary (all
Settled).
