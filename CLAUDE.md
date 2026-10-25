# CLAUDE.md

Guidance for Claude Code working in this repository.

## What this is

**Salal** is a small 2D game engine in C++20 and OpenGL. It owns the window, the
frame, input and the scene graph, so the games on top of it only have to be
games. See [README.md](README.md).

It is being **extracted from worldsim**, which has already implemented this
architecture and is where the code comes from. Nothing has been extracted yet.

## Read this first: the design lives in another repo

The plan, the inventory, the phasing, the risks and the open questions are in
**`../asteroids-roguelike/docs/engine-extraction.md`**. It is not in this repo
yet, and it is the thing to read before touching anything here.

Also worth reading, in that same folder, `tech-stack.md` for how the first
consumer intends to use this engine.

## Sibling repos on this machine

| Path | What it is |
|---|---|
| `../worldsim` | C++20, OpenGL. The source of the code. Active, large |
| `../asteroids-roguelike` | First new consumer. Design docs complete, no code |
| `../dual-deck-builder` | Wasteland Wheels. TypeScript today, migrating to C++ |

All three will consume this engine via CMake FetchContent pinned to a tag.

## Status and what to work on

Nothing is built. **Phase 0 is an audit** and everything else waits on it: does
worldsim's `libs/ui` actually respect its own documented dependency layering, or
does it reach into `libs/engine`? Worldsim's own CLAUDE.md warns that its code
may violate its documented standards, so this gets verified rather than assumed.

Then, in order: extract `foundation` and `renderer`, carve `app` out of
worldsim's `libs/engine`, extract `ui` last. The extraction doc has the detail
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

**WebAssembly constrains the renderer.** Emscripten is a target, so:

- Stay inside the GL ES 3.0 subset. No geometry shaders, no compute, no
  `glMapBuffer`.
- Author shaders so swapping `#version 330 core` for `#version 300 es` is the
  entire change.
- The fixed-step loop is a **tick function the platform calls**, not a loop that
  owns `main()`. `emscripten_set_main_loop` needs this, and it is better design
  anyway.
- The HTTP dev server sits behind a compile flag. No listening sockets in a
  browser.
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

> **A discrepancy worth knowing.** Worldsim's `CLAUDE.md` says
> "Classes/Functions: PascalCase" and gives `LoadTexture` as an example. Its
> coding-standards doc says functions are `camelCase` with `loadTexture()`, and
> that doc is explicitly declared the source of truth. **Functions are
> `camelCase`.** Do not copy the CLAUDE.md line.

Memory: prefer stack allocation, smart pointers over raw, memory arenas for
temporaries, resource handles for assets. Structured logging with categories,
`LOG_DEBUG` through `LOG_ERROR`.

**Terminology:** worldsim says "scene", not "screen". `SplashScene`. The
consuming games' design docs say "screen" because that is clearer for a player;
in code it is a `Scene`.

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
scene switching and shutdown. Don't check coordinates by eye.

This is a native app rendering every frame, not a web page. Shut it down as soon
as you are done with it.

## Build

Not set up yet. It will be worldsim's arrangement: CMake 3.25+ with vcpkg, Ninja
on macOS and Linux, Ninja Multi-Config with MSVC on Windows, ccache in front of
both. Copy `CMakePresets.json` from worldsim when phase 1 starts.

Expected dependencies: `opengl`, `glfw3`, `glew`, `glm`, `nlohmann-json`, `stb`,
`nanosvg`, `msdfgen`, `cpp-httplib`, `gtest`, `benchmark`, plus an audio library
(worldsim has none, so this is new; `miniaudio` or `openal-soft`, both of which
keep the WebAssembly build possible).

## Testing

gtest. Unit tests on anything with real math. Headless tests where a library
allows them, which is most of `foundation` and parts of `renderer`.

No tests on feel, and no screenshot baselines or pixel comparisons.

## Open decisions

Don't guess at these; ask.

- Whether work here is tracked in Specboard, as worldsim and dual-deck-builder
  are. Nothing is set up.
- Whether `app` is one library or splits into `app` and `input`.
- Whether worldsim's vector/SVG asset conventions come across with `renderer` or
  get parameterised out.
- When `engine-extraction.md` moves into this repo.
