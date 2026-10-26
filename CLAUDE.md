# CLAUDE.md

Guidance for Claude Code working in this repository.

## What this is

**Salal** is a small 2D game engine in C++20 and OpenGL. It owns the window, the
frame, input and the scene graph, so the games on top of it only have to be
games. See [README.md](README.md).

It is being **extracted from worldsim**, which has already implemented this
architecture and is where the code comes from. `foundation` and `renderer` are
imported; `ui` and `app` are placeholders.

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
  frame contract, determinism, and the inspector contract. Where it and the
  code disagree, the document is right. Where it says "proposed", it means it.
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
- **[docs/renderer-boundary.md](docs/renderer-boundary.md)** is the renderer
  backend-boundary design: the GL leaks the public surface had, the target
  handles and declarations, the backend seam, and the PR sequence that lands it.

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

The build skeleton is in, with CI on Windows, macOS and Linux. **`foundation`
and `renderer` are imported** (plain copies of worldsim at `870dd755` and
`2cc7e40a`, 2026-10-26); `ui` and `app` are placeholders, as is the sandbox.
**Phase 0, the audit, is done** (2026-10-25); findings in
[docs/phase-0-audit.md](docs/phase-0-audit.md). Worldsim's `libs/ui` reached
into `libs/engine` through two small headers (`input/InputTypes.h` and
`clipboard/ClipboardManager.h`), its `renderer` reached back into `ui` through
`FontRenderer`, and its `foundation` carried the HTTP dev server with OpenGL
and httplib behind it.

**Phase 0.5, untangling worldsim, is done** (eight PRs, [worldsim#291](https://github.com/kcjonson/worldsim/pull/291) through [worldsim#298](https://github.com/kcjonson/worldsim/pull/298), merged 2026-10-25/26): the dead
includes are gone, the clipboard is a seam, `FontRenderer` is in `renderer`,
the CMake cleanup landed, the dev server left `foundation` for
`libs/engine/debug`, and `InputTypes.h` is in `foundation/input/` with `engine`
off ui's link list (decision D, settled 2026-10-26).

Phase 1 landed (2026-10-26): `foundation` and `renderer` are imported, `v0.1.0`
is tagged, and worldsim consumes this repo. The renderer boundary PRs are under
way, sequenced in [docs/renderer-boundary.md](docs/renderer-boundary.md): PR 1
(GL off the public surface) is in (`v0.2.0`), PR 2 is next. Phase 2 carves
`app` out of `libs/engine`; `ui` is extracted last.
The extraction doc has the detail and the reasoning for the order.

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

**Two seams.**

- The fixed-step loop is a **tick function the platform calls**, not a loop that
  owns `main()`. A tick function is testable headlessly, and the platform, not
  the engine, owns the frame.
- The HTTP dev server sits behind a compile flag. No listening sockets in a
  shipped build. Worldsim has no such flag today (the server is always compiled
  into `libs/engine` and switched on at runtime), so this is new discipline,
  added when the server moves into `app`.

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
plus the HTTP dev server. The sandbox is a stub today (`apps/sandbox`, links
`salal::app` and exits); the real one and the server come across from worldsim.

Once they are in: `/api/ui/screenshot`, `/api/ui/tree` for element bounds,
`/api/ui/lint` for layout invariant violations (expect `"count":0` before
screenshotting anything), `/api/input` for synthetic input, `/api/control` for
scene switching and shutdown. Those five are the contract every app exposes.
Worldsim's server has twelve routes today (the others are health, metrics, a
dev-command bag, a state handshake, two SSE streams and an index page), and
games will be able to register their own. Don't check coordinates by eye.

This is a native app rendering every frame, not a web page. Shut it down as soon
as you are done with it.

## Build

Worldsim's arrangement: CMake 3.25+ with vcpkg (`VCPKG_ROOT` set), Ninja on
macOS and Linux, Ninja Multi-Config with MSVC on Windows, ccache in front of
both. `CMakePresets.json` is worldsim's, unchanged.

```
cmake --preset windows        # or: cmake --preset default -DCMAKE_BUILD_TYPE=Debug
cmake --build --preset windows
ctest --test-dir build -C Debug
```

The `windows` preset names `cl` bare, so run configure from a Visual Studio
developer shell (or after `vcvars64.bat`), where it resolves.

`foundation` and `renderer` are worldsim's code, imported as plain copies. `ui`
and `app` are placeholders until later phases move their code in: one
translation unit and one gtest each, there to exercise the wiring. The
placeholder namespaces (`ui`, `app`, lowercase per the coding-standards doc) are
not a decision about the namespaces of moved worldsim code; that casing (the
imported renderer has both `Renderer` and `renderer`) is an open worldsim
follow-up. Blame and history for imported code live in worldsim;
`git remote add worldsim ../worldsim && git fetch worldsim` makes
`git blame worldsim/main -- libs/<lib>/<file>` work from here.

The renderer stages its shaders into `build/shaders/` (the top-level build tree,
even as a subproject), which consumers mirror next to each executable, and it
ships no fonts: `FontRenderer::Initialize` looks for
`fonts/<Family>-SDF.{png,json}` beside the executable, worldsim supplies them,
and an API that takes atlas sources is a phase 2 item.

Consumers inherit `-ffp-contract=off` from `salal::foundation` on GCC and Clang.
The deterministic math and hash noise are header-inline polynomials; Clang fuses
`a*b+c` into an FMA on arm64 and the golden bit patterns, computed on x64,
moved by one ULP the first time the tests ran on macOS. Do not drop the flag.

Each lib's `*.test.cpp` files become `salal_<lib>_tests` through
`salal_add_tests` in `cmake/SalalTesting.cmake`.
Targets are `salal_<lib>` behind `salal::<lib>` aliases, so a FetchContent
consumer's own `ui` or `engine` can't collide, and `SALAL_BUILD_UI`,
`SALAL_BUILD_APP`, `SALAL_BUILD_SANDBOX` and `SALAL_BUILD_TESTS` (on standalone,
off as a subproject) switch off what it doesn't link. CI
(`.github/workflows/ci.yml`) builds and tests on Windows, macOS and Linux and
checks clang-format 21.

Dependencies: `opengl`, `glfw3`, `glew`, `glm`, `nlohmann-json`, `stb`,
`nanosvg`, `cpp-httplib`, `gtest`, `benchmark`. Worldsim's `pugixml`, `lua` and
`sol2` serve its `assets` target, `msdfgen` its font-atlas tool, and all four
stay behind. An audio library is proposed and not decided (worldsim has none;
`miniaudio` is the recommendation).

## Testing

gtest. Unit tests on anything with real math. Headless tests where a library
allows them, which is most of `foundation` and parts of `renderer`.

No tests on feel, and no screenshot baselines or pixel comparisons.

## Open decisions

Don't guess at these; ask. The live list, each with a recommendation and the
phase that needs the answer, is
[docs/open-decisions.md](docs/open-decisions.md), mirrored as items on the
board: the engine PRNG, audio, adopting dual-deck-builder's UI spec, the
ECS core, the `ui` storage model, vector/SVG conventions, multi-device focus,
and the dual-deck-builder golden corpus.

Resolved since the first version of this file: work is tracked in Specboard
(see above). Input types in `foundation/input/`, poller in `app`, settled
2026-10-26 (decision D). The "engine-extraction.md" item is dropped; that file
never existed in worldsim, and the plan is
[docs/extraction.md](docs/extraction.md).
