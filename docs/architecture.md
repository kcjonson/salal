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
OpenGL 3.3 core via GLEW
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
| `foundation` | math, platform abstraction, logging, memory arenas, resource handles, seeded PRNG, and the plain-data input types in `input/` (`Key`, `MouseButton`, `GamepadButton`, `GamepadAxis`, the event structs, the accumulator behind the per-tick `InputSnapshot`, the `ActionMap`), none of which include GLFW | anything that knows about pixels; the HTTP dev server, which is `app`'s (it moved there from worldsim's `libs/engine/debug` in phase 2) |
| `renderer` | GL abstraction (behind a backend boundary, with OpenGL 3.3 core as the first backend; the design is merged and its first two PRs are in; see [open-decisions.md](open-decisions.md) decision G), shaders, Primitives, BatchRenderer, vector/SVG, the font renderer and SDF text rendering (the game owns the fonts and atlases; the renderer will take atlas sources from the game, a phase 2 item), coordinate system, draw metrics | layout, interaction, widgets |
| `ui` | Layer hierarchy, components, layout, focus, theme, text components and font theme tokens, text layout for widgets, animation, inspector | the window, the loop, game logic, the font renderer |
| `app` | window, fixed-step loop, scenes and the game's context object, the settings file, save mechanics (slots, safe writes, versioned upgrades), device input (GLFW callbacks, gamepads, hot-plug, device-to-player-slot claiming, key names for rebinding) feeding the per-tick snapshot, clipboard, dev server | anything drawable |
| `audio` (proposed) | a mixer with priority buses and ducking, looping voices with per-frame pitch, filter and gain, synchronized stems, one-shots with pitch, gain and pan; depends on `foundation` only | game-side sound design |

Input types in `foundation` are settled: decision D in
[open-decisions.md](open-decisions.md), 2026-10-26. The enums and the per-tick
snapshot have to sit below `ui` (its components take `Key` and `MouseButton` in
their public API, `libs/ui/focus/Focusable.h:51`,
`libs/ui/input/InputEvent.h:27`) and below a game's `sim` (which may not depend
on `ui` or `app`), so they are plain data in `foundation/input/` and everything
that touches GLFW stays in `app`. There is no separate `input` library. A
game's sim does not consume `InputSnapshot` itself; it takes a game-owned
action-level struct built from the `ActionMap`, and that struct is the replay
log.

The `audio` row is a proposal, and the text says so because this document is
otherwise normative. It is decision B: the recommendation is an engine library
beside `renderer` over miniaudio, scheduled after `app`, but neither the
library nor its placement is settled.

There is no ECS library: decision E in [open-decisions.md](open-decisions.md),
settled 2026-10-26, leaves worldsim's ECS core in worldsim, since no other
consumer wants one.

The backend boundary in the `renderer` row has a settled direction and a
design; decision G in [open-decisions.md](open-decisions.md) is settled
(2026-10-26) and [renderer-boundary.md](renderer-boundary.md) holds the design
and the PR sequence.
The first two steps are in: no public renderer header (every header under
`libs/renderer` outside `backend/`) reaches a GL, GLES, OpenGL, GLFW or glad
header, which `salal_check_layering()` walks at configure time, and consumers
see `TextureHandle`, `Viewport` and `Primitives` calls where they saw `GLuint`,
`GLFWwindow*` and the batch renderer. `backend/gl/` (the GL wrappers, `Shader`,
`ShaderLoader`, the texture table) is the explicit GL opt-in for a game's own
passes. Every texture the renderer makes lives in its texture table, and
`RenderTarget` is the first backend object. Behind the headers the layer is
still OpenGL throughout (the imported renderer had 182 direct GL calls in
twelve files, measured at worldsim `2cc7e40a`). OpenGL 3.3 core is the only
backend until the backend interface lands, and Vulkan is possible later, not
scheduled. Blend state in the batch sort key, that
render-target API, and the polygon, arc and wide-line primitives land before a
second consumer depends on the renderer API; worldsim consumes the renderer
behaviour-identical and takes API changes through a tag bump. The diagram
above is accurate for the code that exists.

Fonts moving into `renderer` is settled, and forced. Worldsim's
`renderer/primitives/Primitives.cpp:12` includes `ui/font/FontRenderer.h` and
`Primitives.h:35` forward-declares `ui::FontRenderer` in its public API, the one
place the renderer reaches up into ui, and `FontRenderer` itself depends on
nothing else in ui. Moving it removes that knot and gives a game text without
the `ui` library, which asteroids-roguelike's debug overlay needs from day one.
The font files, the atlas generator and the font-atlas build target stay in the
game; salal ships no fonts. The renderer takes its atlases from the game:
`FontRenderer::Initialize` gets a span of `{FontFamily, pngPath, jsonPath}` and
the default family, a `FontFamily` is a small id the game picks (salal names
none), a family that fails to load is logged and draws with the default, and a
failure is the return value, never a process exit. A glyph quad carries the
atlas its UVs index and that atlas's distance range, so `Primitives::drawGlyphs`
takes no family and text draws with the atlas it was laid out in, at its range.
Wrapped text keeps the family and scale it was wrapped in: `generateWrappedGlyphQuads`
takes `wrapText`'s result, not a family or a scale of its own. A quad laid out before a
second `Initialize` carries a destroyed atlas and draws nothing. The atlas JSON
is msdf-atlas-gen's, read as it writes it: glyphs keyed by code point, either
`yOrigin`, any `emSize`, atlas bounds in texels and kerning pairs, which
measuring, layout and wrapping apply. Its bounds already hold the half of the
distance range the edge fades over, so a quad is the glyph's plane bounds and
samples its atlas bounds, reading half a texel past them where bilinear
filtering reaches; msdf-atlas-gen insets the bounds by that half texel from
each glyph's box, so packed boxes touch and no read crosses into another's.
Loading warns about glyphs whose reads overlap (`FontRenderer::crampedGlyphs`),
which draw slivers of each other, and loads them anyway. Text is UTF-8, walked
by code point everywhere it is measured, laid out or wrapped; a code point the
atlas lacks draws as U+FFFD, or `?` where it lacks that too, and each
ill-formed sequence (Unicode's maximal subpart, or a lone byte) as one U+FFFD.

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

One node interface for every element, leaf shape or composite widget
(decided 2026-10-30, [ui-design.md](ui-design.md)). A node renders its own
draws only; the framework walks children, so every node is laid out, linted,
hit-tested and focusable the same way, and a clickable Text or an Image drop
target needs no new class. Widgets (Button, TextInput) are compositions of
nodes, not new primitives.

Worldsim has three roles from the colonysim port (`IComponent`, `ILayer`,
`Component`), non-interactive shapes, design-system classes outside the tree,
and composites that draw their own children; phase 3 replaces all of that.
Dual-deck-builder's code already has one `Component` base that is both leaf
and container (`src/renderer/engine/components/Component.ts`), the shape its
spec's chapter 8 describes.

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
(`16-backend-opengl.md`). It is not adopted wholesale (decision C in
[open-decisions.md](open-decisions.md)): phase 3 revisits it rule by rule on
native merits, and [ui-design.md](ui-design.md) records where salal departs
from it and why. Where ui-design.md and the spec disagree, ui-design.md wins.

## The frame contract

**The loop is a tick function the platform calls. It is not a loop that owns
`main()`.**

```
bool tick()                 one frame: poll, dispatch, fixed ticks, update, render(alpha), present
run()                       the native runner: while (tick()) pace();
Scene::fixedUpdate(tick)    0..n times per frame, at a fixed step, reading no clock
Scene::render(alpha)        alpha into the current fixed step
```

`tick()` reads time through a `Clock` the platform supplies, so a test drives
it with a clock it advances and never sleeps; [app-api.md](app-api.md) has the
frame step by step.

Fixed timestep with an accumulator, render interpolating between the last two
states, accumulator clamped so one long frame cannot cascade into a death
spiral. The default rate is a consumer's choice; `asteroids-roguelike` wants
120 Hz.

This shape is not negotiable, for two reasons. A tick function is testable
headlessly, where a loop is not, and the platform, not the engine, owns the
frame.

Worldsim does not have it today. `Application::run()` owns a variable-step
loop with a 120 FPS sleep-and-spin cap and no accumulator anywhere in `libs/`
(`libs/engine/application/Application.cpp:111-122`, `:276-287`). But the loop
body is already one frame's work, so the split into `bool tick()` with `run()`
as the native `while (tick())` is mechanical; the accumulator and the
interpolating render are net-new in `app`.

The accumulator freezes while paused and on focus loss, so a backgrounded
window does not come back and replay ten seconds of ticks.

Input events are accumulated from platform callbacks and consumed **once per
sim tick**, not once per rendered frame, into a per-tick snapshot carrying
per-button down state and transition counts, so a press and release inside one
tick is not lost. Inputs are recorded at the action level alongside the seed so
any session replays.

## Determinism

Required of `sim` layers in consumers, and of anything in the engine they call.

- All randomness from a single seeded PRNG with forking, so subsystems draw from
  independent streams derived from one seed.
- Nothing unseeded. No wall-clock reads in simulation.
- No reliance on hash-container iteration order.
- No platform-varying floating-point behaviour where it can be avoided.
- Builds with floating-point contraction off on GCC and Clang
  (`-ffp-contract=off`, inherited from `foundation`). Clang fuses `a*b+c` into
  one rounded FMA wherever the target has one, which is always on arm64, so
  golden values computed on one architecture can fail by an ULP on the other.

This buys replays, bug reports reproducible from a seed and an input log,
differential testing between implementations, and a path to netplay that does
not require rewriting the core.

The engine's game PRNG is `Rng` in `foundation/random/Rng.h` (decision A in
[open-decisions.md](open-decisions.md), settled 2026-10-26): dual-deck-builder's
sfc32 with a MurmurHash3-based `fork(name, attempt)`, bit-identical to its
`src/renderer/game/core/Rng.ts` at `RNG_VERSION` 1, whose golden vectors run as
gtests on every CI platform. A stream is its uint32 seed, and `fork` derives a
child stream from the parent's seed, a name and an attempt without reading or
advancing the parent. `Rng` has no unseeded constructor and no copy; root seeds
come from `freshSeed()`, in its own header so a sim can leave it out.
Dual-deck-builder's own sim doesn't yet: a fight given no stream roots one from
`freshSeed()` (`Battle.ts:331`), and so does the AI evaluator given no seed
(`AIEvaluator.ts:77`), so its port has to make those streams required
parameters. Bit-identical differential testing against the TypeScript sim needs
this generator. Worldsim's `Pcg32.h` and `SplitMix64.h`, with `deriveSeed(seed,
streamId)`, stay beside it as they are for worldgen.

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
(`libs/engine/debug/DebugServer.cpp`) has twelve routes today: the five,
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

The flag matters: no listening sockets in a shipped build. `app` has it
(`SALAL_DEV_SERVER`), and worldsim's apps run on that server.

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
revision history was never updated. The code followed neither: each
`Component` keeps its children by value in a per-parent bump arena with
position-based handles (checked 2026-10-30). Salal's `ui` replaces that with a
per-scene slot map and generational ids (decision F, settled in
[ui-design.md](ui-design.md), "Component API"), the shape Rust UI frameworks
use, so the UI layer no longer carries the portability debt this section
worried about.

The `sim` layers in consumers are already data-oriented with dense ids and
component arrays. They would port to Rust almost mechanically. The UI layer, as
it stands, would not. If Rust is on the horizon, the UI layer is where
the attention belongs, and extraction is the moment it is cheapest to change.

None of this is a reason to act today. It is a reason to prefer composition over
inheritance and values over shared pointers when the choice comes up, which is
free.
