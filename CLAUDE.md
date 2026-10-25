# CLAUDE.md

Guidance for Claude Code working in this repository.

## What this is

**Salal** is a small 2D game engine in C++20 and OpenGL. It owns the window, the
frame, input and the scene graph, so the games on top of it only have to be
games. See [README.md](README.md).

It is being **extracted from worldsim**, which has already implemented this
architecture and is where the code comes from. Nothing has been extracted yet.

## Project management: Specboard

Work here is tracked in Specboard over the Specboard MCP. The repo's
`.mcp.json` binds it to the right project, so never ask which one, and never
write the project name or UUID into a committed file, commit message or PR
body. Run `/whats-next` (the `specboard:whats-next` plugin skill) to see
current work and pick up an item; close out with `specboard:complete` once the
change is verified, not before.

Specs live in git, items live on the board. The output of a planning session is
an epic with a linked spec doc under `docs/`, not code. No status files, TODO
lists or progress logs in this repo, and no ticket keys inside spec docs.
Branch names follow `<type>/SAL-123-title` when the item is obvious, a short
descriptive name otherwise. Personal preferences are in the claude-config
repo's `specboard.md`.

## Read this first

- **[docs/architecture.md](docs/architecture.md)** is normative. The four-layer
  stack, what each library owns, the dependency rule, the component model, the
  frame contract, determinism, the inspector contract, and the WebAssembly
  constraints. Where it and the code disagree, the document is right. Where it
  says "proposed", it means it.
- **[docs/extraction.md](docs/extraction.md)** is the plan: what comes from
  where, in what order, what it costs, and what is still open.
- **[docs/phase-0-audit.md](docs/phase-0-audit.md)** is the layering audit of
  worldsim, done 2026-10-25: every violation with file and line, the reverse
  knot through `FontRenderer`, and the fix list.
- **[docs/app-carve-out.md](docs/app-carve-out.md)** maps worldsim's
  `libs/engine` into generic and game, directory by directory, and lists what
  `libs/app` is made of.
- **[docs/consumer-requirements.md](docs/consumer-requirements.md)** is what
  the three games need from the engine, and where their own docs conflict with
  these.

For how the first consumer intends to use this engine, see `tech-stack.md` in
`../asteroids-roguelike/docs/`. Its layer diagram, PRNG choice and primitive
casing predate this repo; the consumer-requirements doc lists the conflicts.

## Sibling repos on this machine

| Path | What it is |
|---|---|
| `../worldsim` | C++20, OpenGL. The source of the code. Active, large. Tracked in Specboard |
| `../asteroids-roguelike` | First new consumer. Design docs complete, no code. No CLAUDE.md, no `.claude/`, no tracking set up |
| `../dual-deck-builder` | Wasteland Wheels. TypeScript today, migrating to C++. Its `docs/ui-rendering-spec/` is a backend-agnostic UI spec abstracted from worldsim and the key reference for `ui` |

All three will consume this engine via CMake FetchContent pinned to a tag.

## Status and what to work on

Nothing is built. **Phase 0, the audit, is done** (2026-10-25); findings in
[docs/phase-0-audit.md](docs/phase-0-audit.md). Worldsim's `libs/ui` reaches
into `libs/engine` through two small headers (`input/InputTypes.h` and
`clipboard/ClipboardManager.h`), its `renderer` reaches back into `ui` through
`FontRenderer`, and its `foundation` carries the HTTP dev server with OpenGL
and httplib behind it.

Next is **phase 0.5, untangling worldsim**: small PRs in that repo (two dead
includes, `InputTypes.h` moved down, a clipboard seam, `FontRenderer` into
`renderer`, CMake cleanup, the dev server out of `foundation`), each leaving it
building and running identically. Then phase 1, the skeleton here and the
extraction of `foundation` and `renderer`; phase 2 carves `app` out of
`libs/engine`; `ui` is extracted last. The extraction doc has the detail and
the reasoning for the order.

Each extraction phase is its own branch and PR **in worldsim**, converting it to
consume this repo. Not one giant branch: worldsim is active, and a long-lived
extraction branch against an active repo is the standard way this fails.

## Hard rules

**The dependency rule.** A library may depend only on layers below it, enforced
through CMake target dependencies. No cycles.

```
app
 |
ui
 |
renderer, foundation
```

A consuming game's simulation layer may depend on neither `renderer` nor `ui`.
That is what keeps determinism and headless testing possible in the games, and
it is the first thing that quietly breaks if nobody is watching.

**WebAssembly constrains the renderer.** Emscripten is a target, so:

- Stay inside the GL ES 3.0 subset. No geometry shaders, no compute, no
  `glMapBuffer`.
- Author shaders so swapping `#version 330 core` for `#version 300 es` is the
  entire change.
- The fixed-step loop is a **tick function the platform calls**, not a loop that
  owns `main()`. `emscripten_set_main_loop` needs this, and it is better design
  anyway.
- The HTTP dev server sits behind a compile flag. No listening sockets in a
  browser. Worldsim has no such flag today (the server is always compiled into
  `foundation` and switched on at runtime), so this is new discipline, added
  when the server moves into `app`.
- Asset loading goes through an indirection that can be MEMFS.

These are cheap now and expensive once three games depend on the API.

**Do not change worldsim's behaviour as a side effect.** A layering violation
found there is a finding to record, and fixing it is a separate branch and PR in
that repo.

**Do not implement games here.** Game logic, scenes, data and art belong to the
consuming repos. If something feels game-specific, it is.

## Conventions

Worldsim's, so code moves between the repos without reformatting.
`.clang-format` and `.clang-tidy` are copied from there.

**`../worldsim/docs/technical/cpp-coding-standards.md` is the source of truth**,
and it overrides whatever the existing code happens to do.

| Thing | Style |
|---|---|
| Classes, structs, enums | `PascalCase` |
| Functions and methods | `camelCase` |
| Variables and members | `camelCase`, no prefix |
| Constants | `k` prefix |
| Files | headers and implementation side by side, `#pragma once` |

Tabs, width 4, column limit 140, braces attached, designated initializers where
a call takes several parameters.

> **A discrepancy worth knowing.** Worldsim's `CLAUDE.md` said
> "Classes/Functions: PascalCase" with `LoadTexture` as the example, while its
> coding-standards doc says functions are `camelCase` with `loadTexture()` and
> is declared the source of truth. Fixed in
> [worldsim#289](https://github.com/kcjonson/worldsim/pull/289), but an older
> checkout will still have the wrong line. **Functions are `camelCase`.**

Memory: prefer stack allocation, smart pointers over raw, memory arenas for
temporaries, resource handles for assets. Structured logging with categories,
`LOG_DEBUG` through `LOG_ERROR`.

**Terminology:** worldsim says "scene", not "screen". `SplashScene`. The
consuming games' design docs say "screen" because that is clearer for a player;
in code it is a `Scene`.

**Scripts and tooling:** New scripts and tooling are Node (ESM, `.mjs`, no
dependencies unless there is a `package.json` for them). No Python.

## Clean code, no legacy

Worldsim's rule, and it applies here. When you replace something, delete what it
replaced in the same commit. No `useNewSystem` flags, no fallback paths, no
functions kept "for reference", no version suffixes, no TODO comments about
removing legacy code. One way to do each thing.

## Verifying changes

There is no game in this repo, so the verification loop is the **sandbox app**
plus the HTTP dev server, both of which come across from worldsim and do not
exist here yet.

Once they do: `/api/ui/screenshot`, `/api/ui/tree` for element bounds,
`/api/ui/lint` for layout invariant violations (expect `"count":0` before
screenshotting anything), `/api/input` for synthetic input, `/api/control` for
scene switching and shutdown. Those five are the contract every app exposes.
Worldsim's server has twelve routes today (the others are health, metrics, a
dev-command bag, a state handshake, two SSE streams and an index page), and
games will be able to register their own. Don't check coordinates by eye.

This is a native app rendering every frame, not a web page. Shut it down as soon
as you are done with it.

## Build

Not set up yet. It will be worldsim's arrangement: CMake 3.25+ with vcpkg, Ninja
on macOS and Linux, Ninja Multi-Config with MSVC on Windows, ccache in front of
both. Copy `CMakePresets.json` from worldsim when phase 1 starts.

Expected dependencies: `opengl`, `glfw3`, `glew`, `glm`, `nlohmann-json`, `stb`,
`nanosvg`, `msdfgen`, `cpp-httplib`, `gtest`, `benchmark`. Worldsim's
`pugixml`, `lua` and `sol2` serve its `assets` target and stay behind. An audio
library is proposed and not decided (worldsim has none; `miniaudio` with its
Emscripten backend is the recommendation).

## Testing

gtest. Unit tests on anything with real math. Headless tests where a library
allows them, which is most of `foundation` and parts of `renderer`.

No tests on feel, and no screenshot baselines or pixel comparisons.

## Open decisions

Don't guess at these; ask. The live list, each with a recommendation and the
phase that needs the answer, is
[docs/open-decisions.md](docs/open-decisions.md), mirrored as items on the
board: the engine PRNG, audio, adopting dual-deck-builder's UI spec, where
input types live, the ECS core, the `ui` storage model, render targets and
particles, vector/SVG conventions, multi-device focus, and the
dual-deck-builder golden corpus.

Resolved since the first version of this file: work is tracked in Specboard
(see above). The app-versus-input split now has a proposal, plain-data input
types in `foundation` with the device poller in `app` and no separate library,
and is decision D there. The "engine-extraction.md" item is dropped; that file
never existed in worldsim, and the plan is
[docs/extraction.md](docs/extraction.md).
