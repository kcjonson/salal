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
| The dev server moves out of `foundation` into `app`, behind a compile flag; tree snapshot, lint and the injection grammar stay as transport-free functions; `Logger::setDebugServer` becomes a sink callback; foundation becomes GL-free | it lived in `worldsim/libs/foundation/debug/DebugServer.cpp` and dragged OpenGL and httplib into foundation; architecture.md already assigns it to app. [worldsim#297](https://github.com/kcjonson/worldsim/pull/297) moved it to `libs/engine/debug` and added the sink; the move into `app` is phase 2 |
| `Application::run()` splits into `tick()` plus a native `while (tick())`; the fixed-step accumulator with interpolation is net-new in `app` | the loop body (`worldsim/libs/engine/application/Application.cpp:112-306`) is already one frame's work; the frame contract is normative |
| `IScene::exportState()` is deleted during extraction | a pure-virtual contract for `/api/scene/state`, a route that no longer exists (`worldsim/libs/engine/scene/Scene.h:64-68`) |
| Worldsim's `assets`, ECS systems and components, construction, nav, vision and world/* stay in worldsim | game schema and game logic throughout (the classification in app-carve-out.md) |
| Phase 0 is complete; an "untangle worldsim" step precedes phase 1 | both ui -> engine and renderer -> ui must be cut before phase 1 can leave worldsim building |
| The ui-sandbox harness moves with the pure-UI scenes; game-flavoured scenes stay | the harness is generic; Planet, Grass, Tree and Salvage* link planet-view, world and assets |
| The TileAtlasBuilder bootstrap and asset-system init move out of AppLauncher into the game | `AppLauncher.cpp:209-226` and `:231-264` hardcode terrain surfaces and asset paths |
| Input types live in `foundation/input/`, the device poller in `app`, and there is no separate `input` library (decision D, 2026-10-26) | every surveyed engine keeps input types in a leaf module below UI and gameplay with the platform layer depending on it (design and evidence under D below) |
| WebAssembly is not an engine target (2026-10-26) | a temporary playtest idea for dual-deck-builder's migration that had started shaping the engine (the GL ES 3.0 subset rule pushed decision G to LDR-only render targets, the Emscripten CI job cost two vcpkg commits); dual-deck-builder's TypeScript build stays the playtest URL |
| Imports are plain copies at a named worldsim commit, one commit per library, with the formatting pass in its own commit (2026-10-26) | history stays in worldsim, where blame for an imported file is `git log 870dd755 -- libs/foundation`; a byte-identical copy can be checked against its source, and formatting stays out of that diff (24 of foundation's 38 files were out of line with the shared `.clang-format`). No filter-repo. Foundation arrived this way in [salal#8](https://github.com/kcjonson/salal/pull/8) |
| Fonts, the font files and the atlas tool stay game-side (2026-10-26); this narrows decision H | worldsim keeps `fonts/`, `tools/generate_sdf_atlas` and the font-atlas target, salal ships no fonts, and `msdfgen` leaves salal's `vcpkg.json`. `FontRenderer` itself is in `renderer` (above) and moves as it is. What remains of H is an API that takes atlas sources and stops calling `std::exit`, not whether salal ships fonts |
| `-ffp-contract=off` is an engine-wide rule (2026-10-26) | set PUBLIC on `salal_foundation` for GCC, Clang and AppleClang (not clang-cl), so every consumer inherits it. The first macOS arm64 run failed three golden-bit tests by one ULP (`DeterministicMath` cos, `HashNoise` value and gradient noise) because Clang fuses `a*b+c` into an FMA there; worldsim's headers claim bit-identical results across x64, ARM and WASM; that had never been tested on ARM |
| The renderer sits behind a backend boundary; OpenGL 3.3 core is the first backend, Vulkan possible later (direction settled 2026-10-26; the design is merged as [renderer-boundary.md](renderer-boundary.md)) | worldsim's renderer is OpenGL throughout: 19 GL or GLFW leaks in 16 public headers and 182 direct GL calls in twelve renderer files (measured at worldsim `2cc7e40a`), and `BatchRenderer.h` and `FontRenderer.h` hand `<GL/glew.h>` and `GLuint` to every includer. Four PRs tagged `v0.2.0` through `v0.5.0`, with a worldsim pin after each, are sequenced in [renderer-boundary.md](renderer-boundary.md); PRs 1 to 4 are in (`v0.2.0` to `v0.5.0`), and worldsim's grass deform has since moved out of the base shader into a worldsim hook ([shader-hooks.md](shader-hooks.md)) |
| The renderer owns offscreen render targets, and additive blend is a per-group key in the batch sort key that splits a run rather than sorting; particles stay game-side; float colour attachments are allowed (decision G, 2026-10-26; the questions the design left open are decided in [renderer-boundary.md](renderer-boundary.md)) | blend state and the render-target API have to settle before a second consumer depends on the renderer API, so none is retrofitted onto an API three games call; GL 3.3 core has float colour attachments without an extension; particles start as a pool over `Primitives` and are promoted to an engine pool when a second game wants one |

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
pressure scalar (`asteroids-roguelike/docs/art-audio.md:72-95`). Asteroids
names two candidates (`tech-stack.md:197-206`), miniaudio and openal-soft.

Recommendation. Engine. A new `audio` library beside `renderer`, depending on
foundation only: a mixer with priority buses and ducking, looping voices with
per-frame pitch, filter and gain, synchronized stems, one-shots with pitch,
gain and pan; miniaudio as the backend (single header, permissive licence,
cross-platform). Schedule it as phase 2b, after app and before ui, because the
prototype wants it.

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
batch sort key (G) and the settled input types (D) should not contradict it.

If deferred. Phase 3 designs ui from worldsim's code alone and re-derives what
the spec already settled; dual-deck-builder's presentation port then meets a
different component model from the one its 60k lines of tests were written
against.

## D. Where input types live

Settled 2026-10-26, after a survey of how established engines place input
types.

Question. Where do `Key`, `MouseButton`, the gamepad enums and the per-tick
input snapshot live, given that ui needs the enums (26 files, 98 uses of
`engine::Key` and `engine::MouseButton` in worldsim's ui) and a game's sim
needs input in some form, and neither may depend on `app`?

Decision. Input types live in `foundation/input/`, the device poller lives in
`app`, and there is no separate `input` library. The directory is the seam if
one is ever wanted (Godot's `core/input` is the precedent).

- `foundation/input/` holds `Key` (the GLFW token set, with position
  semantics), `MouseButton`, `GamepadButton`, `GamepadAxis`, the event structs,
  the accumulator that folds events into a per-tick `InputSnapshot`, and an
  `ActionMap`. The snapshot carries, per button, the down state and a
  transition count, plus axes, pointer and player slot; the `ActionMap`
  evaluates a snapshot into named action states. Nothing in the directory
  includes GLFW, so headless tests and the dev server's `/api/input` build
  snapshots without a window.
- `app` owns GLFW callbacks (not polling), gamepad state through
  `glfwGetGamepadState` with the SDL controller database mappings, hot-plug,
  device-to-player-slot claiming, and `glfwGetKeyName` for the key names the
  rebinding UI shows. It feeds the accumulator and consumes a snapshot once per
  tick.
- `ui` consumes the ordered event list and the enums: shortcuts, focus, and
  text as char events.
- A game's sim takes its own action-level POD, built from `ActionMap` output in
  the game's tick. That POD is the replay log, recorded next to the seed, so
  bindings and keyboard layouts never reach determinism and the sim's input
  type is the game's rather than foundation's `InputSnapshot`.

Two rules from the survey ride along.

Transition counts, not per-frame "just pressed" flags. A flag cleared once per
rendered frame is wrong under a fixed step: two ticks in one frame both see it,
and a frame with no tick loses it. Bevy has had that open since 0.8
([bevy#6183](https://github.com/bevyengine/bevy/issues/6183); #10077 fixed
`EventReader` in `FixedUpdate` but not `ButtonInput`), and Unity tracks the same
gap as ISXB-1006 while advising that `WasPressedThisFrame` be read in `Update`
and passed through to `FixedUpdate`. The fixes converge on consuming events per
fixed step or timestamping them. Handmade Hero's `halfTransitionCount` is the
cheap version and is what the snapshot carries, so a press and release inside
one tick is not lost.

Keys are physical positions. SDL (scancodes for WASD, keycodes for named keys,
text events for text), Godot (`physical_keycode`, the one meant for game
input), Unity (`Key`, a US reference layout), Bevy (`KeyCode`, the W3C code),
O3DE (ANSI physical positions) and GLFW's own tokens all agree; Unreal's
virtual-key `FKey` is the outlier. Worldsim's `Key` is a rename of GLFW tokens,
so it already means a position and the move changes nothing there. The rebinding
UI asks `app` for the layout-aware name.

Evidence. Today the enums are in `worldsim/libs/engine/input/InputTypes.h` (133
lines, no includes) and ui includes them from engine, the main cause of the
ui -> engine breach. Asteroids' tech-stack.md puts input in its `engine`
(`asteroids-roguelike/docs/tech-stack.md:153`), which would put the snapshot
above sim. Dual-deck-builder's dispatcher is a queue of `PlatformInput` drained
per frame, with no snapshot and no device layer.

The survey covered Unreal, Unity's Input System, Godot 4, Bevy, SDL3 and GLFW,
raylib, Hazel, Halley, O3DE, Flax and Defold, plus GGPO, Factorio, Overwatch
(GDC 2017) and Handmade Hero for replay. Every engine keeps key enums and input
state in a leaf module below both UI and gameplay, and the window or platform
layer depends on it, not the reverse. In Unreal, InputCore sits below
ApplicationCore, which hands in integer codes that `FInputKeyManager` maps to
`FKey`. Godot has `core/os/keyboard.h` and `core/input`, which `servers/display`
includes. `bevy_winit` depends on `bevy_input`, whose own dependencies are app,
ecs, math and platform. Halley keeps input in core with SDL in a plugin; O3DE
has `AzFramework/Input` with platform code as private implementations; Flax has
`Engine/Input` with no module dependencies and `Platform/Windows/WindowsInput.cpp`
above it; Hazel has `Core/KeyCodes.h`; raylib and Handmade Hero share one
header. Nobody puts the enums in the UI layer. Player-slot claiming above
devices and below gameplay has precedent as well (Unity's `InputUser`, O3DE's
`User`).

Deterministic systems record a game-defined struct, not raw keys. GGPO's input
packet is fixed-size; Factorio transfers only player actions and replays them;
Overwatch samples input per 16 ms command frame and replays buffered inputs
after a rollback; Handmade Hero's `game_input` crosses a stdint-only header
(`dt`, mouse, and controllers with named buttons such as `moveUp`, each with a
`halfTransitionCount` and `isEndedDown`). Gameplay-facing engine APIs are
action-first: Enhanced Input, Unity Actions, Godot's `is_action_pressed`, Flax
virtual input, O3DE event bindings, and Defold, which exposes nothing else. On
the polling side, GLFW's
[input guide](https://www.glfw.org/docs/latest/input_guide.html) warns that
`glfwGetKey` can miss state changes and points at callbacks.

Unblocks. Untangling step b (`InputTypes.h` moves into `foundation/input/`) and
phase 2 (the snapshot, accumulator and `ActionMap`). Step b is done: that move
and dropping `engine` from ui's link list were the last of phase 0.5
([worldsim#298](https://github.com/kcjonson/worldsim/pull/298), 2026-10-26;
the namespace is now `foundation::`), and the rest of the untangle landed on
2026-10-25/26 ([worldsim#291](https://github.com/kcjonson/worldsim/pull/291)
through [worldsim#297](https://github.com/kcjonson/worldsim/pull/297)).

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

Settled 2026-10-26: the renderer owns offscreen render targets, and additive
blend is a per-group key in the batch sort key that splits a run rather than
sorting; particles stay game-side; float colour attachments are allowed. The
recommendation below was ratified as written, and the four questions it left
open are decided in [renderer-boundary.md](renderer-boundary.md).

Question. Who owns offscreen render targets, blend state, the polygon, arc and
wide-line primitives, and particles, and when, given that the batch sort key,
the render-target API and the primitives have to settle before a second consumer
depends on the renderer API (after worldsim pins `v0.1.0`), and asteroids wants
bloom, additive glow and particles by Quick Run?

Evidence. `asteroids-roguelike/docs/art-audio.md:17-19, :100-102` ask for
real bloom, additive glow and particle work; the prototype is white shapes
(`:104-106`). Neither salal nor worldsim mentions render targets, blend modes
or particles. Worldsim's one `GL_RGBA32F` use is a framebuffer colour
attachment in
`worldsim/libs/engine/world/rendering/SurfaceFieldGolden.test.cpp:257`
(`glFramebufferTexture2D` at `:262`), a test of code that stays in worldsim;
nothing that moves to salal uses float render targets. Dual-deck-builder's
batcher sorts by layer ordinal only. The polygon, arc and wide-line primitives
are the required shape additions listed in [architecture.md](architecture.md).

GL isolation, measured at worldsim `2cc7e40a` for
[renderer-boundary.md](renderer-boundary.md): 19 GL and GLFW leaks in 16
renderer headers, 182 GL calls in 12 renderer files behind them, 346 calls in
the game's own renderers and about 480 outside the renderer in all.

Decision. Renderer gets blend state in the batch sort key, an offscreen
render-target API, and polygon, arc and wide-line primitives, all landed before
a second consumer depends on the renderer API, so none is retrofitted onto an
API three games call. That point is after worldsim pins `v0.1.0`: worldsim
consumes the renderer as it is, behaviour-identical, and takes API changes
through a tag bump. Float colour attachments are allowed; GL 3.3 core has them
without an extension. Particles start game-side over Primitives; an engine pool
is promoted when a second game wants one. The design, with the public-surface
audit, the target declarations, the backend seam and the PR sequence, is
[renderer-boundary.md](renderer-boundary.md).

The boundary lands as four salal PRs tagged `v0.2.0` through `v0.5.0`, each
followed by a worldsim pin: GL off the public surface, textures and the render
target as backend objects, blend state in the sort key with the new primitives,
then the backend interface. File lists, target declarations and the settled
questions are in [renderer-boundary.md](renderer-boundary.md).

Needed by. Settled, so no longer a gate. It was needed before a second
consumer depends on the renderer API, which is after worldsim pins `v0.1.0`:
blend state in the sort key, the render-target API (the second of the four
PRs) and the polygon, arc and wide-line primitives. Bloom and particles
themselves are Quick Run or later.

If deferred. Moot, as it is settled. The cost would have been blend state
added to the sort key after three games depend on its ordering, and additive
draws as per-call GL state changes that defeat the batcher.

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

Narrowed 2026-10-26: fonts, the font files and the atlas tool are game-side
(Settled), so the font half of this question is no longer whether salal ships
assets. It is the API `FontRenderer` takes for atlas sources, and getting
`std::exit` out of its initialisation, a phase 2 follow-up.

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
