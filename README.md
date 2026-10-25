# Salal

A small 2D game engine in C++20 and OpenGL. Owns the window, the frame, input,
and the scene graph, so the games on top of it only have to be games.

Named after *Gaultheria shallon*, the glossy evergreen shrub that carpets the
floor of Pacific Northwest coastal forests. It is the understory: the layer
everything else grows up out of, which is the job this repo has.

## Status

**Nothing here yet.** The repo exists to hold the namespace and the plan.

The engine is being extracted from [worldsim](https://github.com/kcjonson/worldsim),
which has implemented this architecture once already and is where the code comes
from.

- [docs/architecture.md](docs/architecture.md) is the normative spec every
  consumer conforms to.
- [docs/extraction.md](docs/extraction.md) is the plan: what comes from where,
  in what order, and what is still open.

Phase 0 is an audit of whether worldsim's `libs/ui` actually respects its own
dependency layering. Everything else waits on the answer.

## Planned shape

```
libs/
  foundation/   math, platform, logging, arenas, resource handles, seeded PRNG
  renderer/     gl, shader, primitives, batching, vector/SVG, SDF text, coords
  ui/           components, layout, focus, theme, shapes, font, animation
  app/          window, fixed-step loop, scenes, config, input, dev server
apps/
  sandbox/      boots straight into a single UI scene
```

One rule: a library may depend only on layers below it, enforced through CMake
target dependencies. A consuming game's simulation layer may depend on neither
`renderer` nor `ui`.

### The four-layer render stack

```
Components      scene graph, shapes, widgets, style
Primitives      immediate-mode: DrawRect, DrawText, DrawCircle
BatchRenderer   command buffer, sorts by state, minimal draw calls
OpenGL 3.3+     via GLEW, or WebGL2 via Emscripten
```

## What it is not

No scene editor, no physics, no asset importers, no skeletal animation, no
scripting runtime, no networking. Physics belongs to whichever game needs it.

It is an engine by the test that matters, which is that it owns the frame and
calls your code rather than the other way round. It is not Unity and should not
be described as though it were.

## Consumers

- [worldsim](https://github.com/kcjonson/worldsim), the source of the code
- [asteroids-roguelike](https://github.com/kcjonson/asteroids-roguelike), the
  genericity test, since it has no tiles, chunks, 3D or pathfinding to hide a
  worldsim-shaped API behind
- dual-deck-builder, migrating from TypeScript

Consumed via CMake FetchContent, pinned to a tag per game.

## Targets

Windows, macOS and Linux natively. WebAssembly through Emscripten as a playtest
channel, which constrains the renderer to the GL ES 3.0 subset and means the
frame loop has to be a tick function the platform calls rather than a loop that
owns `main()`.
