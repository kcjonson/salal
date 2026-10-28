# Salal

A small 2D game engine in C++20 and OpenGL. Owns the window, the frame, input,
and the scene graph, so the games on top of it only have to be games.

Named after *Gaultheria shallon*, the glossy evergreen shrub that carpets the
floor of Pacific Northwest coastal forests. It is the understory: the layer
everything else grows up out of, which is the job this repo has.

## Status

**`foundation` and `renderer` are imported** from worldsim as plain copies,
and worldsim consumes salal through FetchContent, pinned to a tag. The
renderer's backend boundary is under way ([docs/renderer-boundary.md](docs/renderer-boundary.md),
PRs 1 to 4 in), with a null backend for headless tests, and the base shader
takes the hooks a game assembles into it at build time
([docs/shader-hooks.md](docs/shader-hooks.md)). Phase 2 is carving `app` out
of worldsim ([docs/app-api.md](docs/app-api.md)): the input types, the frame,
the platforms, the scene stack and the devices are in; the dev server, settings
and saves, and the sandbox harness follow. `ui` is a placeholder until phase 3.
CI builds and tests on Windows, macOS and Linux.

The engine is being extracted from [worldsim](https://github.com/kcjonson/worldsim),
which has implemented this architecture once already and is where the code comes
from.

- [docs/architecture.md](docs/architecture.md) is the normative spec every
  consumer conforms to.
- [docs/extraction.md](docs/extraction.md) is the plan: what comes from where,
  in what order, and what is still open.
- [docs/phase-0-audit.md](docs/phase-0-audit.md) is the layering audit of
  worldsim, done 2026-10-25.

Phase 0, the audit of whether worldsim's `libs/ui` respects its own dependency
layering, is done. It reaches into `libs/engine` through two small headers and
nothing else, and worldsim's renderer reaches back into `ui` for its font
renderer. Both get untangled in worldsim before anything is extracted; the fix
list is in the audit doc and the order of work after it is in the extraction
doc.

## Planned shape

```
libs/
  foundation/   math, platform, logging, arenas, resource handles, seeded PRNG, input types
  renderer/     gl, shader, primitives, batching, vector/SVG, fonts, SDF text, coords
  ui/           components, layout, focus, theme, shapes, animation
  app/          window, fixed-step loop, scenes, config, device input, dev server
  audio/        proposed, not decided: mixer, buses, stems, one-shots
apps/
  sandbox/      boots straight into a single UI scene
```

The font renderer lives in `renderer` rather than `ui`, so text is available
without the UI library. `audio` is a proposal, recorded with the other open
questions in [docs/open-decisions.md](docs/open-decisions.md); the other four
libraries are settled.

One rule: a library may depend only on layers below it, enforced through CMake
target dependencies. A consuming game's simulation layer may depend on neither
`renderer` nor `ui`.

### The four-layer render stack

```
Components      scene graph, shapes, widgets, style
Primitives      immediate-mode: drawRect, drawText, drawCircle
BatchRenderer   command buffer, sorts by state, minimal draw calls
OpenGL 3.3+     via GLEW
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

Windows, macOS and Linux natively.
