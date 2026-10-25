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

## Libraries

| Library | Owns | Does not own |
|---|---|---|
| `foundation` | math, platform abstraction, logging, memory arenas, resource handles, seeded PRNG | anything that knows about pixels |
| `renderer` | GL abstraction, shaders, Primitives, BatchRenderer, vector/SVG, SDF text, coordinate system, draw metrics | layout, interaction, widgets |
| `ui` | Layer hierarchy, components, layout, focus, theme, fonts, animation, inspector | the window, the loop, game logic |
| `app` | window, fixed-step loop, scenes, config, input, dev server | anything drawable |

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

And for consumers: **a game's simulation layer may depend on neither `renderer`
nor `ui`.** That is what keeps determinism and headless testing possible, and it
is the first thing that quietly breaks if nobody is watching.

## The component model

Three roles, which worldsim and dual-deck-builder already share because both
descend from the same design:

- **Layer** owns position, size, and parent-child hierarchy. Not interactive.
- **Component** extends Layer with interaction: pointer events, wheel, focus.
- **Shapes** are concrete leaves: Rectangle, Circle, Line, Text, Triangle.

Widgets (Button, TextInput) are compositions of shapes, not new primitives.

Style is plain data: colour, border, corner radius, composed rather than
inherited.

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

Dual-deck-builder's existing TypeScript sim core already meets this bar and its
spec guarantees identical JSON output in Node and the browser from the same
config and seed. The C++ port is held to the same guarantee, which is what makes
that migration verifiable rather than hopeful.

## Coordinates and DPI

Logical pixels are the API surface. Physical pixels exist only inside
`renderer`. Anything a consumer positions, measures, or screenshots is in
logical pixels, and the inspector reports logical pixels so a screenshot and an
element tree can be compared directly.

## The inspector contract

Every app built on the engine exposes, behind a compile flag:

| Endpoint | Returns |
|---|---|
| `/api/ui/tree` | element hierarchy with logical-pixel bounds |
| `/api/ui/lint` | layout invariant violations, `count` zero when clean |
| `/api/ui/screenshot` | PNG |
| `/api/input` | synthetic input injection in logical pixels |
| `/api/control` | scene switch, pause, resume, reload, exit |

This is how UI is verified without a human watching. Check `lint` is clean
before trusting a screenshot, and read bounds from `tree` rather than measuring
by eye.

The flag matters: no listening sockets in a browser, and none in a shipped build.

## WebAssembly constraints

Emscripten is a supported target, which constrains the renderer permanently:

- Stay inside the GL ES 3.0 subset. No geometry shaders, no compute shaders, no
  `glMapBuffer`.
- Shaders are authored so that swapping `#version 330 core` for
  `#version 300 es` plus precision qualifiers is the entire change.
- No GL extension loader on the web path; GLEW compiles out.
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

**`shared_ptr` graphs.** Worldsim's UI stores
`std::vector<std::shared_ptr<Layer>>`, inherited from the colonysim port. Rust's
ownership model fights that pattern hard, and the mechanical translation lands
in `Rc<RefCell<T>>`, which the Rust community treats as a smell for good
reasons.

Worth knowing: worldsim's own integration doc already identified value semantics
(`std::vector<ConcreteType>`, dense ids, no pointers) as the "research ideal",
and chose `shared_ptr` deliberately as the pragmatic option. **That choice is
the portability debt**, and it is also the thing most worth revisiting during
extraction, because value semantics are better C++ on their own merits: better
cache behaviour, no reference cycles, no lifetime ambiguity.

The `sim` layers in consumers are already data-oriented with dense ids and
component arrays. They would port to Rust almost mechanically. The UI layer, as
it stands, would not. If Rust is genuinely on the horizon, the UI layer is where
the attention belongs, and extraction is the moment it is cheapest to change.

None of this is a reason to act today. It is a reason to prefer composition over
inheritance and values over shared pointers when the choice comes up, which is
free.
