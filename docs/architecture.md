# Architecture

**Normative.** This is the specification the engine implements and every
consumer conforms to, including dual-deck-builder while it is still TypeScript.
Where this document and the code disagree, the document is right and the code is
a bug.

## The four layers

```
Components      scene graph, shapes, widgets, style
    |
Primitives      immediate-mode: drawRect, drawText, drawCircle
    |
BatchRenderer   command buffer: accumulate, sort by state, flush
    |
OpenGL 3.3+     via GLEW natively, WebGL2 via Emscripten
```

Components call Primitives every frame. Primitives feed the BatchRenderer. The
BatchRenderer submits. **A game's own drawing goes through the same Primitives
API the UI does**, which is the point of having it: one way to draw a rectangle.

Primitives functions are `camelCase`, `drawRect` and not `DrawRect`, like every
other function under worldsim's coding standards. Older copies of the README
and asteroids-roguelike's `tech-stack.md:66` write them PascalCase; this
document is the one that is right.

## Libraries

| Library | Owns | Does not own |
|---|---|---|
| `foundation` | math, platform abstraction, logging, memory arenas, resource handles, seeded PRNG. Proposed: the plain-data input types (`Key`, `MouseButton`, gamepad enums, the per-tick `InputSnapshot`) | anything that knows about pixels; the HTTP dev server, which sits here in worldsim today and moves to `app` |
| `renderer` | GL abstraction, shaders, Primitives, BatchRenderer, vector/SVG, fonts and glyph generation, SDF text rendering, coordinate system, draw metrics | layout, interaction, widgets |
| `ui` | Layer hierarchy, components, layout, focus, theme, text components and font theme tokens, text layout for widgets, animation, inspector | the window, the loop, game logic, the font renderer |
| `app` | window, fixed-step loop, scenes, config, device input (the GLFW poller, hot-plug, device-to-player-slot claiming), clipboard, dev server | anything drawable |
| `audio` (proposed) | a mixer with priority buses and ducking, looping voices with per-frame pitch, filter and gain, synchronized stems, one-shots with pitch, gain and pan; depends on `foundation` only | game-side sound design |

Two of those rows are proposals, and the text says so because this document is
otherwise normative. Input types in `foundation` is decision D in
[open-decisions.md](open-decisions.md): the enums and the per-tick snapshot
have to sit below `ui` (its components take `Key` and `MouseButton` in their
public API, `libs/ui/focus/Focusable.h:51`, `libs/ui/input/InputEvent.h:27`)
and below a game's `sim` (which consumes the snapshot and may not depend on
`ui`), so the proposal puts them in `foundation` as plain data and keeps
everything that touches GLFW in `app`, with no separate `input` library. The
`audio` row is decision B: the recommendation is an engine library beside
`renderer` over miniaudio with its Emscripten backend, scheduled after `app`,
but neither the library nor its placement is settled.

Fonts moving into `renderer` is settled, and forced. Worldsim's
`renderer/primitives/Primitives.cpp:12` includes `ui/font/FontRenderer.h` and
`Primitives.h:35` forward-declares `ui::FontRenderer` in its public API, the
one place the renderer reaches up into ui, and `FontRenderer` itself depends
on nothing else in ui. Moving it removes that knot and gives a game text
without the `ui` library, which asteroids-roguelike's debug overlay needs from
day one.

### The dependency rule

A library may depend only on layers below it. No cycles. Enforced through CMake
target dependencies, not convention.

```
app
 |
ui
 |
renderer, foundation
```

`audio`, if adopted, sits beside `renderer` and depends on `foundation` only.

And for consumers: **a game's simulation layer may depend on neither `renderer`
nor `ui`.** That is what keeps determinism and headless testing possible, and it
is the first thing that quietly breaks if nobody is watching.

## The component model

Three roles:

- **Layer** owns position, size, and parent-child hierarchy. Not interactive.
- **Component** extends Layer with interaction: pointer events, wheel, focus.
- **Shapes** are concrete leaves: Rectangle, Circle, Line, Text, Triangle.

Widgets (Button, TextInput) are compositions of shapes, not new primitives.

Worldsim has the three as classes, from the colonysim port. Dual-deck-builder's
current code has one `Component` base that is both leaf and container, with
Rectangle, Circle, Line, Polygon, Triangle and Text as subclasses and "layer"
meaning a named z-layer (`src/renderer/engine/components/Component.ts`,
`draw/layers.ts`); its spec's object model (chapter 8) is the candidate
reference for the C++ `ui` if decision C adopts the spec.

The shape list above is not enough for the consumers. asteroids-roguelike's
arena needs a filled polygon with a per-frame transform (asteroid silhouettes,
`docs/art-audio.md:15-16, 44-47`), a ring or arc with partial fill (the HUD
ring and hold-fill indicator, `art-audio.md:62-63`), a line with width (beams
and the predicted shot path, `enemies.md:47`, `assists.md:92-94`), a stroke on
a silhouette (the rim light), clipping by a silhouette, and a transform stack
for drawing wrap ghosts (`physics.md:93-94`). These are required additions to
Primitives and the shape set, not speculative ones. Dual-deck-builder's draw
API already has `drawPolygon` and `drawPolyline`.

Style is plain data: colour, border, corner radius, composed rather than
inherited.

### A candidate spec for `ui`

Dual-deck-builder's `docs/ui-rendering-spec/` (eighteen chapters, started
2026-09-07 as a backend-agnostic spec abstracted from worldsim's C++) covers
the object model, input and focus, layout, style and theme, and observability,
with a conformance scorecard for both implementations
(`14-testing-and-conformance.md`: the TypeScript engine scores 85 yes and 15
partial of 100 required rules) and a 32-item worldsim gap list
(`16-backend-opengl.md`). Adopting it as the normative spec for salal's `ui`,
with the gap list as the phase 3 worklist, is decision C in
[open-decisions.md](open-decisions.md). Until that is decided this document
stays the normative one and the spec is a reference.

## The frame contract

**The loop is a tick function the platform calls. It is not a loop that owns
`main()`.**

```
tick(realDeltaSeconds) -> void
render(interpolationAlpha) -> void
```

Fixed timestep with an accumulator, render interpolating between the last two
states, accumulator clamped so one long frame cannot cascade into a death
spiral. The default rate is a consumer's choice; `asteroids-roguelike` wants
120 Hz.

This shape is not negotiable, for two reasons. `emscripten_set_main_loop`
requires the platform to drive the frame, so a `while` loop in the engine closes
off the web build entirely. And a tick function is testable headlessly, where a
loop is not.

Worldsim does not have it today. `Application::run()` owns a variable-step
loop with a 120 FPS sleep-and-spin cap and no accumulator anywhere in `libs/`
(`libs/engine/application/Application.cpp:111-122`, `:276-287`). But the loop
body is already one frame's work, so the split into `bool tick()` with `run()`
as the native `while (tick())` is mechanical; the accumulator and the
interpolating render are net-new in `app`.

The accumulator freezes while paused and on focus loss, so a backgrounded
window does not come back and replay ten seconds of ticks.

Input is polled **once per sim tick**, not once per rendered frame, and collected
into a per-tick snapshot. Inputs are recorded alongside the seed so any session
replays.

## Determinism

Required of `sim` layers in consumers, and of anything in the engine they call.

- All randomness from a single seeded PRNG with forking, so subsystems draw from
  independent streams derived from one seed.
- Nothing unseeded. No wall-clock reads in simulation.
- No reliance on hash-container iteration order.
- No platform-varying floating-point behaviour where it can be avoided.

This buys replays, bug reports reproducible from a seed and an input log,
differential testing between implementations, and a path to netplay that does
not require rewriting the core.

No engine PRNG exists yet, and which one it will be is decision A in
[open-decisions.md](open-decisions.md). Worldsim has
`foundation/random/Pcg32.h` and `SplitMix64.h` with `deriveSeed(seed,
streamId)`, deterministic and cross-platform, but no named-stream `fork`.
Dual-deck-builder has sfc32 with a MurmurHash3-based `fork(name, attempt)` and
language-neutral golden vectors (`src/renderer/game/core/Rng.ts`). They are
different generators, and bit-identical differential testing against the
TypeScript sim needs the latter. The recommendation is both: dual-deck-builder's
as the engine's game PRNG, worldsim's kept as it is for worldgen.

Dual-deck-builder's TypeScript sim core is written to this bar, and its spec
requires identical JSON output in Node and the browser from the same config and
seed. Nothing demonstrates that yet: there is no browser sim and no committed
golden corpus (decision J). Once the corpus exists the C++ port is held to the
same guarantee, which is what makes that migration verifiable rather than
hopeful.

## Coordinates and DPI

Logical pixels are the API surface. Physical pixels exist only inside
`renderer`. Anything a consumer positions, measures, or screenshots is in
logical pixels, and the inspector reports logical pixels so a screenshot and an
element tree can be compared directly.

## The inspector contract

Every app built on the engine exposes, behind a compile flag, at least:

| Endpoint | Returns |
|---|---|
| `/api/ui/tree` | element hierarchy with logical-pixel bounds |
| `/api/ui/lint` | layout invariant violations, `count` zero when clean |
| `/api/ui/screenshot` | PNG |
| `/api/input` | synthetic input injection: pointer and keys in logical pixels, plus gamepad buttons and axes |
| `/api/control` | scene switch, pause, resume, reload, exit |

Those five are the contract. Worldsim's server
(`libs/foundation/debug/DebugServer.cpp`) has twelve routes today: the five,
plus `/api/health`, `/api/metrics`, `/api/dev/<verb>` (a generic command bag),
`/api/state?what=` (the synchronous handshake that `tree` and `lint` ride on),
`/stream/metrics` and `/stream/logs` (server-sent events), and `/` (an HTML
index). It binds `127.0.0.1` only. Two things it does not do that the
consumers need: `/api/input` takes pointer and key events only, and games
cannot register endpoints of their own. Both change in `app`. A game whose
arena is drawn through Primitives is invisible to `/api/ui/tree`, so verifying
it takes game-registered routes (sim state, step N ticks, set seed) and pad and
axis injection.

This is how UI is verified without a human watching. Check `lint` is clean
before trusting a screenshot, and read bounds from `tree` rather than measuring
by eye.

The flag matters: no listening sockets in a browser, and none in a shipped build.
Worldsim has no such flag today; the server is always compiled into
`foundation` and enabled at runtime by `AppConfig::enableDebugServer` or
`--http-port`.

## WebAssembly constraints

Emscripten is a supported target, which constrains the renderer permanently:

- Stay inside the GL ES 3.0 subset. No geometry shaders, no compute shaders, no
  `glMapBuffer`.
- Shaders are authored so that swapping `#version 330 core` for
  `#version 300 es` plus precision qualifiers is the entire change. Worldsim's
  include preprocessor (`libs/renderer/shader/ShaderPreprocessor.cpp`) splices
  headerless `.glsl` includes, so only top-level files carry a version line.
- No GL extension loader on the web path; GLEW compiles out.
- Float colour attachments are not core ES 3.0 and need
  `EXT_color_buffer_float` under WebGL2. Worldsim's only `GL_RGBA32F` use is a
  framebuffer colour attachment in a test of code that stays behind
  (`libs/engine/world/rendering/SurfaceFieldGolden.test.cpp:257`,
  `glFramebufferTexture2D` at `:262`); nothing that moves to salal uses float
  render targets. Bloom and any other offscreen pass stay LDR, which keeps the
  renderer inside core ES 3.0.
- Asset loading goes through an indirection that can be MEMFS.
- Prefer VBOs over client-side arrays, so `FULL_ES3` emulation never costs
  anything.
- Single-threaded web path. Threads need SharedArrayBuffer plus COOP/COEP
  headers and no consumer needs them.

## Portability, including Rust

The engine is C++20 and staying there. But a rewrite in Rust is plausible in a
year or two, and the choices that would make that cheap or expensive are being
made now, so they are worth naming.

**Casing is a non-issue.** Rust does enforce naming, through `rustc` lints that
warn by default (`non_camel_case_types`, `non_snake_case`,
`non_upper_case_globals`) and an ecosystem that is uniform about it. Types and
traits are `UpperCamelCase`, functions and variables and modules are
`snake_case`, constants and statics are `SCREAMING_SNAKE_CASE`.

So a port renames every function and every constant, and only types carry over
unchanged. That is a `sed` pass and a `cargo clippy` run, which is the cheapest
part of any rewrite. **Do not distort the C++ conventions to anticipate it.**

**The structural choices are what actually cost.** Two of them:

**Inheritance.** Rust has none. An inheritance chain from Layer to Component to
a concrete widget is the single hardest thing in this design to port, because it
has to become traits plus composition, and that is a redesign rather than a
translation. Prefer composition now: a widget that *holds* a Rectangle and a
Text ports; a widget that *is* a Rectangle does not.

**`shared_ptr` graphs.** Worldsim's colonysim port was planned around
`std::vector<std::shared_ptr<Layer>>`. Rust's ownership model fights that
pattern hard, and the mechanical translation lands in `Rc<RefCell<T>>`, which
the Rust community treats as a smell for good reasons.

Worth knowing: worldsim's integration doc
(`docs/technical/ui-framework/colonysim-integration-architecture.md`) records
two contradictory decisions. The original, dated 2025-10-29, identified value
semantics (`std::vector<ConcreteType>`, dense ids, no pointers) as the
"research ideal" and chose `shared_ptr` as the pragmatic option, with a move
to values only if profiling demanded it; sections at `:103-166`, `:414`,
`:489` and `:673` still say so. Its "Architectural Decision" section was then
overwritten in place on 2025-11-03 to say value semantics with `std::variant`
from the start, skipping the shared_ptr step entirely (`:168-172`), and the
revision history was never updated. Which one the code followed is unverified:
`libs/ui/layer` is 176 lines, `components` is 12,765, and nobody has yet
checked what `Layer` stores. That is decision F in
[open-decisions.md](open-decisions.md), a phase 3 question. If the code took
the shared_ptr path, **that is the portability debt**, and extraction is the
moment it is cheapest to change, because value semantics are better C++ on
their own merits: better cache behaviour, no reference cycles, no lifetime
ambiguity.

The `sim` layers in consumers are already data-oriented with dense ids and
component arrays. They would port to Rust almost mechanically. The UI layer, as
it stands, would not. If Rust is on the horizon, the UI layer is where
the attention belongs, and extraction is the moment it is cheapest to change.

None of this is a reason to act today. It is a reason to prefer composition over
inheritance and values over shared pointers when the choice comes up, which is
free.
