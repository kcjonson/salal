# CLAUDE.md

Guidance for Claude Code working in this repository.

## What this is

**Salal** is a small 2D game engine in C++20 and OpenGL. It owns the window, the
frame, input and the scene graph, so the games on top of it only have to be
games. See [README.md](README.md).

It is being **extracted from worldsim**, which has already implemented this
architecture and is where the code comes from. `foundation`, `renderer` and
`app` are in and worldsim runs on all three; `ui` is a placeholder until phase 3.

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
- **[docs/shader-hooks.md](docs/shader-hooks.md)** is the base shader's hook
  contract: how a game adds vertex code to the one program at build time
  (`salal_base_shader()`), the hook points, uniform blocks, and what fails the
  build.
- **[docs/app-api.md](docs/app-api.md)** is the phase 2 design of `app`: the
  frame, input and the per-tick snapshot, scenes, settings and saves, the dev
  server, and the seven PRs that land it.
- **[docs/ui-design.md](docs/ui-design.md)** is the phase 3 design of `ui`,
  decided rule by rule against dual-deck-builder's ui-rendering-spec: where
  salal departs from the spec and why, starting with the ordering model.
- **[docs/ui-plan.md](docs/ui-plan.md)** is the phase 3 PR sequence: the
  renderer changes first, `ui` built in salal, then worldsim's conversion.
- **[docs/testing/README.md](docs/testing/README.md)** is the testing strategy:
  happy-path scripts run by cheap subagent fleets, local scene diffs for
  rendering code, the strict perf suite, and bounded unit tests.

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
`2cc7e40a`, 2026-10-26); `app` is built from worldsim's `libs/engine` slice
(phase 2, below); `ui` is a placeholder.
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
way, sequenced in [docs/renderer-boundary.md](docs/renderer-boundary.md): PRs 1
to 4 are in (`v0.2.0` to `v0.5.0`), and after them the base shader takes hooks
assembled at build time ([docs/shader-hooks.md](docs/shader-hooks.md)), which took
worldsim's grass deform out of the renderer and into a worldsim hook.
`foundation/random/Rng.h` is the engine's game PRNG, dual-deck-builder's `Rng`
ported bit for bit (decision A).

**Phase 2, carving `app` out of `libs/engine`, is done** (2026-10-28), in the
seven PRs [docs/app-api.md](docs/app-api.md) sequences. Salal PRs 1 to 6 are
`v0.10.0` to `v0.15.0`: the input types, the accumulator and `ActionMap` in
`foundation/input/`, then `app`'s clock, GLFW and headless platforms, frame,
scene stack, overlays and context object, then the GLFW device layer with
gamepads, player slots, key names, capture, axis edges and the clipboard, then
the dev server behind its compile flags, then the settings file and saves, then
the sandbox harness, a scene picker drawn with `Primitives` over worldsim's
renderer-level scenes. Worldsim pinned `v0.15.0` ([worldsim#313](https://github.com/kcjonson/worldsim/pull/313))
and then converted its three GUI apps onto `app` ([worldsim#314](https://github.com/kcjonson/worldsim/pull/314)),
which deleted its own application, scene, input, clipboard and debug code. The
accepted behaviour differences are owned in app-api.md. Next is phase 2b, audio,
once decision B is settled, then phase 3, `ui`. The extraction doc has the detail
and the reasoning for the order.

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
  shipped build. `SALAL_DEV_SERVER` is defined in Debug and RelWithDebInfo
  unless `SALAL_WITH_DEV_SERVER` is off, and in Release only with
  `SALAL_DEV_SERVER_IN_RELEASE`; elsewhere `DevServer`'s calls are inline
  no-ops and nothing links cpp-httplib, which the layering check holds to.
  Worldsim's apps turn the server on where `app::kDevServerCompiledIn` is true,
  or for `--http-port`, so a plain Release launch opens no socket and logs
  nothing.

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
plus the HTTP dev server. The sandbox (`apps/sandbox`) runs worldsim's
renderer-level test scenes (`sdf_minimal`, `polygon`, `arc`, `polyline`,
`blend` and `input_test`) on the GLFW platform, under a scene picker drawn with
`Primitives` (F1, or the tab in the bottom-right corner; `/api/state?what=picker`
has its state and the tab's and rows' bounds). It serves the dev server on
127.0.0.1:8090 in Debug and RelWithDebInfo (`--http-port <port>` for another).
Run `build/apps/sandbox/Debug/sandbox.exe --scene=<name>` in the background,
which starts in that scene with the picker closed, and stop it with
`curl "http://127.0.0.1:8090/api/control?action=exit"`, which answers once the
app is torn down and the port is free.

`/api/ui/screenshot`, `/api/ui/tree` for element bounds, `/api/ui/lint` for
layout invariant violations (expect `"count":0` before screenshotting
anything), `/api/input` for synthetic input, `/api/control` for scene
switching, pause, `step&ticks=<n>` and shutdown. Those five are the contract
every app exposes; an app with no ui answers tree and lint empty. The rest are
health, metrics, `/api/state?what=` (`scene` is every app's), the game's
`/api/dev/<verb>` routes, two SSE streams and an index page; games register
routes, control actions and state providers ([docs/app-api.md](docs/app-api.md),
"Dev server"). Don't check coordinates by eye.

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

Configure and build from a shell with the MSVC environment: `cl` on PATH plus
`INCLUDE` and `LIB`. Worldsim's `scripts/setup-msvc-env.ps1` persists those to
the User environment, so any new shell has them. A Visual Studio developer shell
(or `vcvars64.bat`) works too, but it repoints `VCPKG_ROOT` at the VS-bundled
vcpkg (no binary cache, every port built from source). A top-level configure
refuses that toolchain, before project() installs anything. Set `VCPKG_ROOT`
back to your own checkout, then configure with `--fresh` or delete `build/`: the
first configure binds the toolchain, and fixing `VCPKG_ROOT` later won't rebind it.

The `windows` preset picks MSVC by setting `CC` and `CXX` to `cl` in its
environment, which CMake reads only on a build dir's first configure, so later
`cmake --preset windows` runs keep the cache. A `build/` first configured with
the wrong compiler or vcpkg, or one an older preset broke (it deleted the cache
and `find_package` has failed since), needs one `cmake --fresh --preset windows`,
or delete `build/`.

`foundation` and `renderer` are worldsim's code, imported as plain copies.
`app` is worldsim's `libs/engine` slice rebuilt to [docs/app-api.md](docs/app-api.md),
in namespace `app`. `ui` is a placeholder until phase 3 moves its code in: one
translation unit and one gtest, there to exercise the wiring. Its placeholder
namespace (`ui`, lowercase per the coding-standards doc) is not a decision
about the namespaces of moved worldsim code; that casing (the
imported renderer has both `Renderer` and `renderer`) is an open worldsim
follow-up. Blame and history for imported code live in worldsim;
`git remote add worldsim ../worldsim && git fetch worldsim` makes
`git blame worldsim/main -- libs/<lib>/<file>` work from here.

The renderer's base shader is assembled and compiled into each consumer at build
time by `salal_base_shader()` ([docs/shader-hooks.md](docs/shader-hooks.md)),
with the consumer's hooks, so no shader files sit beside an executable; it ships
no fonts: a game hands `FontRenderer::Initialize` its atlases, each a PNG and
JSON path under a `FontFamily` id the game defines, and names its default
family; a bad set comes back as `false`, never a process exit, and a glyph
whose atlas cell is too small for what its quad samples is a warning at load.
Glyph quads carry their atlas and its range, so `drawGlyphs` takes no family,
and wrapped text carries the family and scale it was wrapped in.

A theme is Design Tokens (2025.10, with the Resolver module) loaded at run time
into `ui::Theme` ([docs/ui-design.md](docs/ui-design.md), "Theme"). ui's own
tokens are `libs/ui/tokens/ui.tokens.json`, three tiers, always the bottom
layer. `salal_tokens(<target> <dir>)` (`cmake/SalalTokens.cmake`) runs
`salal_tokens_tool`, a host tool built in-tree on nlohmann-json, which checks
the folder over ui's tokens in every mode, embeds it, and generates typed
`<ns>::styles` and `<ns>::tokens` handles, so a misspelt style doesn't compile.

Consumers inherit `-ffp-contract=off` from `salal::foundation` on GCC and Clang.
The deterministic math and hash noise are header-inline polynomials; Clang fuses
`a*b+c` into an FMA on arm64 and the golden bit patterns, computed on x64,
moved by one ULP the first time the tests ran on macOS. Do not drop the flag.

Each lib's `*.test.cpp` files become `salal_<lib>_tests` through
`salal_add_tests` in `cmake/SalalTesting.cmake`.
Targets are `salal_<lib>` behind `salal::<lib>` aliases, so a FetchContent
consumer's own `ui` or `engine` can't collide, and `SALAL_BUILD_UI`,
`SALAL_BUILD_APP`, `SALAL_BUILD_SANDBOX` and `SALAL_BUILD_TESTS` (on standalone,
off as a subproject) switch off what it doesn't link. `SALAL_WITH_DEV_SERVER`
(on) and `SALAL_DEV_SERVER_IN_RELEASE` (off) hold for a subproject too, so a
game sets them in its own configure; a configuration of the game's own (a
"Profile") gets no server. CI
(`.github/workflows/ci.yml`) builds and tests on Windows, macOS and Linux and
checks clang-format 21.

Dependencies: `opengl`, `glfw3`, `glew`, `glm`, `nlohmann-json`, `stb`,
`nanosvg`, `cpp-httplib` (app's dev server; a game configured with
`SALAL_WITH_DEV_SERVER` off needn't have it), `gtest`, `benchmark`, and
`glslang` with its `tools` feature as a host dependency, whose
`glslangValidator` checks the base shader at build time (a FetchContent
consumer adds it to its own manifest), and `msdfgen` without its default
features, also a host dependency, which `salal_icons(<target> <dir>)`
(`cmake/SalalIcons.cmake`) builds its icon tool on: every SVG in the folder
checked against ui-design.md's icon rules, one MSDF atlas, its JSON and a name
table embedded in the target (a game that builds no icons needn't have it).
Worldsim's `pugixml`, `lua` and `sol2` serve its `assets` target and stay behind.
An audio library is proposed and not decided
(worldsim has none; `miniaudio` is the recommendation).

## Testing

[docs/testing/README.md](docs/testing/README.md) is the strategy. In short:
look for actual regressions, since every game's art is prototype and nothing
is protected pixel for pixel.

- Before committing, run the relevant happy-path scripts
  (`docs/testing/happy-path/`) as a manual regression pass with the cheapest
  subagent fleet that can, using judgement, not exact comparison. A feature
  adds its lines.
- Drawing or rendering changes also diff the sandbox scenes against `main`,
  locally, never in CI, with no baselines committed.
- Batching, shader, ui walk or app frame changes run the perf suite against
  `main`. Performance is strict.
- Unit tests (gtest) on real math, bounded: check for duplicates first. GL
  tests assert computed values, never stored images.

No tests on feel.

## Open decisions

Don't guess at these; ask. The live list, each with a recommendation and the
phase that needs the answer, is
[docs/open-decisions.md](docs/open-decisions.md), mirrored as items on the
board: audio, dual-deck-builder's UI spec (being decided rule by rule in
[docs/ui-design.md](docs/ui-design.md), not adopted wholesale), vector/SVG conventions, multi-device focus, and the dual-deck-builder golden
corpus.

Resolved since the first version of this file: work is tracked in Specboard
(see above). Input types in `foundation/input/`, poller in `app`, settled
2026-10-26 (decision D). The engine's game PRNG is dual-deck-builder's `Rng`,
in `foundation/random/Rng.h`, with Pcg32 and SplitMix64 kept for worldgen, and
the ECS core stays in worldsim, both settled 2026-10-26 (decisions A and E).
The "engine-extraction.md" item is dropped; that file never existed in
worldsim, and the plan is [docs/extraction.md](docs/extraction.md).
