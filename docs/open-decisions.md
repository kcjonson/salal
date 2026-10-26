# Open decisions

Each of these is an item on the Specboard board, and the board holds its
status. This file holds the reasoning: the question, the evidence, the
recommendation, which phase needs the answer, and what deferring costs. When
one is decided the board closes the item and architecture.md gets the
decision; this file keeps the record. Written 2026-10-25 from the five
surveys behind [phase-0-audit.md](phase-0-audit.md),
[app-carve-out.md](app-carve-out.md) and
[consumer-requirements.md](consumer-requirements.md).

## Settled

Written into the docs as decided, with the reason.

| decision | reason |
|---|---|
| Work is tracked in Specboard | Kevin asked for it; worldsim and dual-deck-builder already are |
| FontRenderer moves into `renderer` (namespace `ui::` -> `Renderer::`) | removes the renderer -> ui knot (`worldsim/libs/renderer/primitives/Primitives.cpp:12`, `Primitives.h:35`) and gives text below ui, which the prototype's debug overlay needs; FontRenderer depends on nothing else in ui. `Renderer::` rather than the standards doc's lowercase because FontFamily, Primitives, BatchRenderer, Shader, CoordinateSystem, metrics/, gl/ and resources/Tile* are all `namespace Renderer` (only vector/ and two resources/ files are lowercase); a lowercase FontRenderer would force `Renderer::FontFamily` qualifiers and a second namespace block in Primitives.h, the shape being removed |
| The dev server moves out of `foundation` into `app`, behind a compile flag; tree snapshot, lint and the injection grammar stay as transport-free functions; `Logger::setDebugServer` becomes a sink callback; foundation becomes GL-free | it lives in `worldsim/libs/foundation/debug/DebugServer.cpp` today and drags OpenGL and httplib into foundation; architecture.md already assigns it to app |
| `Application::run()` splits into `tick()` plus a native `while (tick())`; the fixed-step accumulator with interpolation is net-new in `app` | the loop body (`worldsim/libs/engine/application/Application.cpp:112-306`) is already one frame's work; the frame contract is normative |
| `IScene::exportState()` is deleted during extraction | a pure-virtual contract for `/api/scene/state`, a route that no longer exists (`worldsim/libs/engine/scene/Scene.h:64-68`) |
| Worldsim's `assets`, ECS systems and components, construction, nav, vision and world/* stay in worldsim | game schema and game logic throughout (the classification in app-carve-out.md) |
| Phase 0 is complete; an "untangle worldsim" step precedes phase 1 | both ui -> engine and renderer -> ui must be cut before phase 1 can leave worldsim building |
| The ui-sandbox harness moves with the pure-UI scenes; game-flavoured scenes stay | the harness is generic; Planet, Grass, Tree and Salvage* link planet-view, world and assets |
| The TileAtlasBuilder bootstrap and asset-system init move out of AppLauncher into the game | `AppLauncher.cpp:209-226` and `:231-264` hardcode terrain surfaces and asset paths |

## A. Engine PRNG

Question. Does the engine's game PRNG become dual-deck-builder's sfc32 with
MurmurHash3-based `fork(name, attempt)`, worldsim's Pcg32 plus `deriveSeed`,
or both?

Evidence. Worldsim has `worldsim/libs/foundation/random/Pcg32.h` and
`SplitMix64.h` (`deriveSeed(worldSeed, streamId)`), deterministic and
cross-platform, with no named-stream fork; worldgen depends on them and the
header bans `std::mt19937` in worldgen. Dual-deck-builder has
`dual-deck-builder/src/renderer/game/core/Rng.ts` (sfc32, Murmur fork,
`RNG_VERSION = 1`) with golden vectors cross-checked against C, and every sim
stream derives through `fork('run', n)`, `fork('fight', f)` and so on. These
are different generators; bit-identical differential testing of Job B is only
possible if the C++ PRNG is dual-deck-builder's. Asteroids' tech-stack.md
names `xoshiro128**` and no forking (`asteroids-roguelike/docs/tech-stack.md:131-132`),
a third generator nobody depends on.

Recommendation. Add `Rng` (sfc32 plus Murmur fork, with dual-deck-builder's
golden vectors as gtest cases) to `foundation/random` as the engine's game
PRNG. Keep Pcg32 and SplitMix64 as they are for worldsim's worldgen. Asteroids
drops the xoshiro mention and uses `Rng`.

Needed by. Phase 2 (the asteroids prototype draws from it) and before Job B
starts. Foundation moves in phase 1 without it; the addition can land any time
after.

If deferred. The asteroids prototype picks something, and its replays and
seeds become tied to it; dual-deck-builder's differential corpus cannot be run
against C++ at all.

## B. Audio

Question. Does audio live in the engine or per game, and on which library?

Evidence. Worldsim has none: no vcpkg port, no hits for any audio library,
only an unused `LogCategory::Audio`. Asteroids' feel prototype wants audible
thrust and impacts (`asteroids-roguelike/docs/physics.md:254-255`), and Quick
Run wants priority buses with ducking, a looping thrust voice with per-frame
pitch and filter, pitched one-shots and sample-synchronized stems driven by a
pressure scalar (`asteroids-roguelike/docs/art-audio.md:72-95`). Both
candidates asteroids names (`tech-stack.md:197-206`), miniaudio and
openal-soft, have Emscripten backends over Web Audio.

Recommendation. Engine. A new `audio` library beside `renderer`, depending on
foundation only: a mixer with priority buses and ducking, looping voices with
per-frame pitch, filter and gain, synchronized stems, one-shots with pitch,
gain and pan; miniaudio as the backend. Schedule it as phase 2b, after app and
before ui, because the prototype wants it.

Needed by. The asteroids feel prototype, so right after phase 2.

If deferred. The prototype ships silent, which its own checklist says
invalidates the feel test, or it grows a game-side audio path that later has
to be unpicked.

## C. Adopt dual-deck-builder's ui-rendering-spec as the normative spec for ui

Question. Does `dual-deck-builder/docs/ui-rendering-spec/` (18 chapters:
object model, input and focus, layout, style and theme, observability,
conformance scoring, OpenGL backend) become the spec `ui` is built to, with
its chapter 16 worldsim gap list (32 items) as the phase-3 worklist?

Evidence. The spec was abstracted from worldsim on 2026-09-07 with a C++
column, and dual-deck-builder's TS engine was rebuilt to it Aug to Oct 2026.
Chapter 14 already scores both implementations per rule (TS 85 yes / 15
partial of 100). Its component model (one base class, named z-layers,
MountContext instead of singletons) differs from architecture.md's
Layer/Component/Shapes trio and from worldsim's, so adopting it is a design
choice as well as a checklist.

Recommendation. Yes. It is the only artefact that describes both existing
implementations against the same rules, and the conformance scorecard is a
ready-made acceptance test for phase 3.

Needed by. Phase 3 design. Phases 1 and 2 are unaffected, except that the
batch sort key (G) and the input types (D) should not contradict it.

If deferred. Phase 3 designs ui from worldsim's code alone and re-derives what
the spec already settled; dual-deck-builder's presentation port then meets a
different component model from the one its 60k lines of tests were written
against.

## D. Where input types live

Question. Where do `Key`, `MouseButton`, the gamepad enums and the per-tick
`InputSnapshot` live, given that ui needs the enums (26 files, 98 uses of
`engine::Key` and `engine::MouseButton` in worldsim's ui) and a game's sim
needs the snapshot, and neither may depend on `app`?

Evidence. Today the enums are in `worldsim/libs/engine/input/InputTypes.h`
(133 lines, no includes) and ui includes them from engine, the main cause of
the ui -> engine breach. Asteroids' tech-stack.md puts input in its `engine`
(`asteroids-roguelike/docs/tech-stack.md:153`), which would put the snapshot
type above sim. Dual-deck-builder's dispatcher is a queue of `PlatformInput`
drained per frame, with no snapshot and no device layer. extraction.md's open
item "one library or `app` plus `input`" is this question.

Recommendation. Plain-data enums and `InputSnapshot` in `foundation`, where
ui, app and a game's sim can all see them without breaking the dependency
rule. The GLFW-backed poller, hot-plug and device-to-player-slot claiming in
`app`. No separate `input` library.

Needed by. Untangling step b (where InputTypes.h moves to) and phase 2 (the
snapshot).

If deferred. InputTypes.h goes into `libs/ui/input/` as the fallback, which
fixes worldsim's breach but leaves sim unable to consume a snapshot type
without a second copy.

## E. ECS core

Question. Does the 7-header, 689-line ECS core
(`worldsim/libs/engine/ecs/{EntityID,ComponentPool,Registry,View,ISystem,World,ECS}.h`)
ship as an optional module in salal or stay in worldsim?

Evidence. It is generic, header-only, namespace `ecs`, std-only includes; 31
files in apps/world-sim use it. Asteroids' sim is data-oriented and "not an
ECS framework" (`asteroids-roguelike/docs/tech-stack.md:165-168`);
dual-deck-builder has no ECS.

Recommendation. Leave it in worldsim. No other consumer wants it, and
worldsim's own coding standards say "no ECS for UI components".

Needed by. Phase 2, when the app slice is listed.

If deferred. Nothing; it stays where it is by default.

## F. ui storage model during extraction

Question. Does worldsim's ui store its tree as a `shared_ptr` graph or with
value semantics, and does extraction change it?

Evidence. `worldsim/docs/technical/ui-framework/colonysim-integration-architecture.md`
records two contradictory decisions: 2025-10-29 "port with shared_ptr,
profile, move to value semantics only if measured" (sections :103-166, :414,
:489, :673) and 2025-11-03 "use value semantics with std::variant from the
start" (:168-172), with the revision history never updated. Which one the
code follows was not checked; `libs/ui/layer` is 176 lines and components are
12,765. architecture.md's portability section conditions its shared_ptr claim
on this being checked.

Recommendation. Read `libs/ui/layer` before phase 3 and state the fact in
architecture.md. If the code is a shared_ptr graph, extraction is the moment
to change it, and decision C's spec (no singletons, reconciled children)
points the same way.

Needed by. Phase 3 design. Nothing earlier.

If deferred. Phase 3 inherits whichever model is there, unexamined, and
architecture.md keeps hedging about worldsim's own code.

## G. Render targets, additive blending, particles

Question. Who owns offscreen render targets, blend state and particles, and
when, given that the batch sort key is fixed in phase 1 and asteroids wants
bloom, additive glow and particles by Quick Run?

Evidence. `asteroids-roguelike/docs/art-audio.md:17-19, :100-102` ask for
real bloom, additive glow and particle work; the prototype is white shapes
(`:104-106`). Neither salal nor worldsim mentions render targets, blend modes
or particles. Worldsim's one `GL_RGBA32F` use is a framebuffer colour
attachment in
`worldsim/libs/engine/world/rendering/SurfaceFieldGolden.test.cpp:257`
(`glFramebufferTexture2D` at `:262`), a test of code that stays in worldsim;
nothing that moves to salal uses float render targets. Float attachments
(RGBA16F, RGBA32F) are not colour-renderable in core ES 3.0 without
EXT_color_buffer_float, and salal forbids an extension loader on the web.
Dual-deck-builder's batcher sorts by layer ordinal only.

Recommendation. Renderer gets blend state in the batch sort key and an
offscreen render-target API in the phase 1 design, so neither is retrofitted
onto an API three games call. Bloom is LDR, which keeps the renderer inside
core ES 3.0 without EXT_color_buffer_float, an extension no extracted code
needs today. Particles start game-side over Primitives; an engine pool is
promoted when a second game wants one.

Needed by. Phase 1, for the sort key and the render-target API. Bloom and
particles themselves are Quick Run or later.

If deferred. Blend state gets added to the sort key after three games depend
on its ordering, and additive draws start as per-call GL state changes that
defeat the batcher.

## H. Vector and SVG asset conventions

Question. Do worldsim's vector and SVG asset conventions (nanosvg load,
tessellation in `renderer/vector`, TileAtlasBuilder's hardcoded
`assets/tiles/surfaces/<name>/pattern.svg` at
`worldsim/libs/renderer/resources/TileAtlasBuilder.cpp:64`) come across with
`renderer` as they are, or get parameterised?

Evidence. `renderer/vector` is 3,941 lines. Asteroids calls the vector
pipeline a good fit for its silhouettes
(`asteroids-roguelike/docs/tech-stack.md:79-84`). Dual-deck-builder has its
own `draw` and `rendering` layers (drawImage with nine-slice, an art atlas, a
texture store with budgeted uploads) that may disagree with worldsim's
conventions. Worldsim's asset reads are direct filesystem with ResourcePath as
the only seam (app-carve-out.md).

Recommendation. Still open. The vector tessellation moves as code; the path
conventions and TileAtlasBuilder's bootstrap stay game-side (the bootstrap is
already settled). Decide the rest when dual-deck-builder's presentation port
shows what its art atlas needs.

Needed by. Phase 1 at the latest for what moves; the convention question can
wait until Job B's presentation phase.

If deferred. Low cost. Worldsim keeps working either way; the risk is that
renderer's public API grows a worldsim-shaped asset path that
dual-deck-builder then has to route around.

## I. Multi-device focus

Question. Does ui support two simultaneous focus contexts bound to different
devices, for the asteroids co-op wave shop where both players shop from the
same pool at once (`asteroids-roguelike/docs/screens.md:151-152, :265-266`)?

Evidence. Worldsim's `focus` is 1,422 lines and nothing in it is per-player.
Dual-deck-builder's FocusManager has scopes, groups and directional focus
with an `inputMode` of pointer, keyboard or controller, and nothing in the
survey of it is per-device. The asteroids design calls simultaneous shopping
"the current call", so a turn-based or split-pane fallback is available.

Recommendation. Phase 3 question. Design the focus manager so a focus context
is a value that can be instantiated per device rather than a singleton, which
decision C's spec already implies, and leave simultaneous shopping to the
asteroids repo to confirm it still wants.

Needed by. Phase 3, and only if asteroids keeps the simultaneous call.

If deferred. The co-op shop goes turn-based or split-pane; nothing else in any
consumer needs it.

## J. Differential harness for dual-deck-builder

Question. Who produces the golden JSON corpus that Job B's differential tests
diff against, and when?

Evidence. None is committed. `node scripts/balance-sim.mjs` runs `fights` and
`gen` modes with `--json`; no workflow invokes it; no Node-versus-browser
harness exists and no browser sim exists. Anchors today are test pins (seed 7
in `Fights.test.ts`, seed 1 in `Gen.test.ts`), the Rng goldens and the
baseline tables in `dual-deck-builder/docs/specs/Balance Simulation.md`.

Recommendation. Before Job B starts, dual-deck-builder generates a corpus from
`balance-sim --json` over a fixed set of configs and seeds and commits it,
with the float-discipline notes (`-ffp-contract=off`, rounding, JSON number
formatting, key order) written down next to it. Dual-deck-builder's work,
tracked in its project and named here so salal's phase 4 does not start
without it.

Needed by. Phase 4, Job B's first step.

If deferred. Job B has no oracle beyond the test pins, and "port the sim first
with differential verification" becomes "port the sim and hope".
