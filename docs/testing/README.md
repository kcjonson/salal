# Testing

We test for actual regressions. Every game's art is prototype, so nothing is
protected pixel for pixel, and nothing in CI compares images. Decided
2026-10-30.

| Layer | Where | When | Who |
|---|---|---|---|
| Unit tests | `libs/*/*.test.cpp` | CI on three OSes, every push | CI |
| GL readback tests | the same files, skipped headless | renderer changes, locally | the developer |
| Happy-path scripts | `docs/testing/happy-path/*.md` | before every commit | a cheap subagent fleet |
| Scene diffs | `scripts/scene-diff.mjs` (to build) | before committing drawing or rendering code | the developer, a subagent judging |
| Perf suite | `perf_*` sandbox scenes, `scripts/perf.mjs` (to build) | before committing batching, shader, ui walk or app frame changes, and at every tag | the developer |

## Happy-path scripts

Test scripts written for a human, one file per area (the sandbox, the dev
server, then ui widgets). Each is a numbered list, one line a step: what to
do, then what you should see.

```
1. `--scene=arc`: three rings filled to 25, 50 and 75 percent, no gaps or seams.
2. Click the 50 percent ring: nothing changes (arcs aren't interactive).
```

As short as they can be. A feature that lands adds its lines; a feature that
goes removes them.

Before committing, the relevant scripts are run as a manual regression pass:
one cheap subagent (Haiku-class) per script, driving the sandbox through the
dev server (`/api/ui/screenshot`, `tree`, `lint`, `input`, `control`), using
judgement and fuzzy matching the way a human tester would, and reporting each
step as pass or fail with a screenshot of anything that failed.

## Layout lint and tree

Helpers salal ships for building game UI, not tests: `/api/ui/lint` checks
layout invariants (overlapping siblings, children spilling out of parents,
off-screen nodes, zero sizes, overflowing text, unreachable controls) and
`/api/ui/tree` gives every element's bounds. Agents call them on demand while
building and checking a game's screens, expecting a lint count of 0 before
screenshotting. They never run in CI.

## Sandbox scenes and scene diffs

The sandbox scenes are the rendering test pages, for manual testing by people
and agents and for the scene diffs: one scene per primitive or feature
(`sdf_minimal`, `polygon`, `arc`, `polyline`, `blend`, `input_test`; ui scenes
come with phase 3). A new drawing feature adds a scene or extends one.

When drawing or rendering code changes (`libs/renderer`'s primitives, shaders
and backend, ui's drawing), the scene diff runs before committing: it builds
`main` in a scratch worktree, renders every scene paused at a fixed step from
both builds, and diffs them. A cheap subagent looks at each scene that differs
and says whether the change is the intended one or a regression. Locally only,
never in CI, and no baseline images are committed. This is the one place
salal compares pixels.

## Performance

Strict, and the engine's own. Rendering performance dropping in gameplay is
the regression the games have hit most, and a game needs to know whether its
frame rate fell because of its own change or the engine's.

The perf suite is a set of stress scenes in the sandbox (thousands of quads,
text, icons, a ui tree with popups open, world-like batches with flushes) and
a runner: a release build, vsync off, a warm-up, then a fixed window, reading
`/api/metrics` for CPU and GPU frame time (p50, p99), batcher time, draw
calls, instances and bytes uploaded, written as JSON to `perf-results/`.

- A change to batching, the shader, ui's walk or app's frame runs it against
  `main` on the same machine, and a regression past the threshold fails it.
  The threshold is set from measured run-to-run noise.
- Every tag's result is committed with the machine's name. When a game's frame
  rate drops after an engine bump, run the suite at the old and new tags on
  the same machine: if the engine's numbers moved, it's the engine.

## Unit tests

gtest, beside the code, on anything with real math, headless where a library
allows. Bounded: before adding a test, look for one covering the same path,
and merge duplicates when touching a suite. The renderer's GL tests assert
computed values read back from a `RenderTarget`, never stored images, and skip
when there's no GL context.

No tests on feel.

## CI

Build, unit tests and clang-format on Windows, macOS and Linux. Nothing that
needs a GPU, no perf runs, no image comparison.
