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
| Perf suite | `perf_*` sandbox scenes, `scripts/perf.mjs` | before committing batching, shader, ui walk or app frame changes, and at every tag | the developer |

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

- A change to batching, the shader, ui's walk or app's frame runs it against
  `main` on the same machine, and a regression past the threshold fails it.
- Every tag's result is committed with the machine's name. When a game's frame
  rate drops after an engine bump, compare the old and new tags on the same
  machine: if the engine's numbers moved, it's the engine.

### The scenes

Three sandbox scenes, each fixed content from a fixed seed, with any motion a
function of the tick, so every frame of a tick draws the same. They are built
to measure what the instanced-quad and texture-slot rework
([ui-design.md](../ui-design.md), Per-draw data and Texture residency)
changes: the batcher's CPU time per quad, the bytes it uploads, and the draw
calls texture changes cost.

| scene | what it draws | what it guards |
|---|---|---|
| `perf_quads` | 10,000 overlapping rects: radii, borders inside, centred and outside, some gradients and shadows, under a scrolling translate | per-quad batcher time and upload bytes, one draw call |
| `perf_text` | 223 list rows in six sizes, 11 to 24 px: a background, an image icon from twelve that cycle, a label in one font atlas and a value and unit in a second (the sandbox's font loaded twice) | glyph batching and texture splits: three draw calls a row today (671 a frame), and fourteen textures a frame, more than eight resident slots hold, so a slow slot-full path shows |
| `perf_world` | worldsim's VectorPerf 10,000 stars inside a clip, 120 stroked roads, a flush, 3,000 instanced trees, 400 rotating polygons from six cached silhouettes sorted by z, a barrier, 200 labels, then four clipped screen panels, under a panning, zooming camera | the world path: triangles, strokes, the z sort, barriers, clips and flushes |

A ui tree with popups open and icons come with phase 3, when there is a ui
walk, a popup stack and an icon atlas to measure.

### What a capture holds

`/api/control?action=capture&op=start` starts a capture and turns the
renderer's GPU timing on; `op=stop` puts the timing back as it found it and
answers with the summary. A capture holds every whole frame between the two.
Each of these is reported as p50, p99, mean and max over the frames (nearest
rank), or null over none:

| field | what it is |
|---|---|
| `frameMs` | start to start: the frame's work, present and pacing |
| `cpuMs` | poll, input, update, render and the dev server's post-render step: the CPU's work, without present |
| `presentMs` | the swap, which waits when the GPU or the compositor is behind |
| `renderMs` | step 9, the scenes and overlays drawing and the batch's flushes; in these scenes, which only draw, the batcher's time |
| `flushMs` | of that, the flushes: the plan, the uploads and the draw calls |
| `gpuMs` | GPU time of the frame's clears, flushes, instanced draws and baked passes (below), over the frames whose results came back |
| `drawCalls`, `vertices`, `triangles`, `instances`, `uploadBytes` | the batch's counts, `uploadBytes` the vertex, index and instance bytes it wrote |

Beside them: `gpuFrames` and `gpuDropped`, the GPU results that came back
and those that didn't in time; `gpuSupported`, false on a driver with no
timestamps; `minimizedFrames`; and what the run had at stop, its
`framebuffer`, whether it `resized`, and `vsync` (a settings file's
`video.vsync` beats `--no-vsync`).

GPU time comes from `GPUFrameTimer`: each clear, flush draw, instanced draw
and baked pass is a span between two of the backend's timestamp queries
(no GL past the boundary), a span inside another is part of it, and a
frame's spans are summed, so a frame the CPU holds up reads what the GPU did,
not how long it waited. A frame is one `App::render`, which calls
`Primitives::beginFrameStats()` once, so a pass that begins a batch of its
own inside it (an offscreen render) adds to the frame's counts and GPU time.
Results are read four frames late and never waited for; one not back by then
is dropped and counted. It costs nothing while no capture runs, reads 0 on the
null backend, and on a GL driver whose `GL_TIMESTAMP` has no counter bits
(some of macOS's) it times nothing and says so. `/api/metrics` carries the
live per-frame counts too.

### The runner

`node scripts/perf.mjs` builds the sandbox Release with
`SALAL_DEV_SERVER_IN_RELEASE` in `build-perf/`, beside `build/`: Release
because it is what ships (RelWithDebInfo inlines less on MSVC), and its own
build tree so a perf run leaves the developer's build alone. Each run
launches one scene with `--scene`, `--http-port 8094` (never 8090) and
`--no-vsync`, waits for `/api/health`, checks the scene it asked for is the
one running, warms up 3 s, captures 5 s, reads `/api/metrics`, and exits
through `/api/control?action=exit`. On a machine with 16 or more logical CPUs
it pins the sandbox to logical CPUs 4 to 11 (`--affinity` for another mask);
on a smaller one, or macOS, it runs unpinned, says so, and won't compare
without an explicit `--threshold`. Before each run it waits for any compiler,
linker, `ninja` or other sandbox to finish and for its cores to be under 25%
busy, checks the port right before launching, and flags a run that went ahead
after the wait ran out.

```
node scripts/perf.mjs run                  # this checkout, into perf-results/local/
node scripts/perf.mjs compare              # the merge-base with main, built in a scratch worktree, against this checkout
node scripts/perf.mjs compare --base v0.20.0
node scripts/perf.mjs noise                # one build's spread, and the gate's error rates on it
```

Compare builds the base in a worktree under the OS temp folder (one a
checkout and ref, kept for the next run's incremental build; `git worktree
remove` it when done), or takes `--base-exe`, prints both commits, and runs
the two builds interleaved, five runs each, alternating which goes first. The
first usable base is the tag cut with the perf suite; anything older has no
perf scenes, which compare reports as invalid.

A build's figure for a timing is the fastest of its five runs, since
interference only slows a run. Exit codes:

- **1, regressed**: a gated timing more than its scene's threshold above the
  base, or a count up (draw calls, upload bytes, instances, vertices are
  fixed for fixed content; `--allow-counts` reports them instead).
- **3, inconclusive**: a side's two fastest runs differ by more than the
  threshold (it didn't reproduce its figure), a run went ahead on a busy
  machine, or the scene's content differs between the builds.
- **2, invalid**: a bad option, a run that couldn't be made (a missing scene,
  a port in use, a sandbox that didn't start), or data that can't be gated: a
  gated value missing, zero or not finite, under 200 frames, a minimized
  frame, a resize, vsync on, or GPU results for under 90% of a run's frames.

Tails, present, flush time and `perf_world`'s GPU time are reported, not
gated.

### Thresholds, provisional

| scene | gated, min of 5, threshold |
|---|---|
| `perf_quads` | `cpuMs` and `renderMs` p50, 3%; `gpuMs` p50, 3% |
| `perf_text` | `cpuMs` and `renderMs` p50, 3%; `gpuMs` p50, 3% |
| `perf_world` | `cpuMs` and `renderMs` p50, 3%; `gpuMs` not gated |

These are provisional. They come from runs on `kevinsgamingdesktop` (Ryzen 9
5950X, RTX 3090, Windows 11) on 2026-10-30 while the machine was in use and
other sessions were building on it, which aren't valid baselines. Resampled,
min of five at 3% falsely failed a build against itself in at most 1.0% of
trials on the CPU timings and missed a 5% regression in at most 7.3%, but
`perf_world`'s GPU time sat at one of two levels a third apart and falsely
failed 25% of trials, so it isn't gated. Two sessions an hour apart differed
by up to a third (`perf_text` 0.72 against 0.48 ms) while each was tight, and
unpinned runs moved by a third launch to launch, which is why the runner pins
and why a decision is always a compare, its builds interleaved in one
session, and never a run read against a committed result. In one run of
`perf_world` present waited in some frames (a `presentMs` mean of 3.06 ms
against a p50 of 0.14), so its frame time's p50 doesn't add up from the
others' p50s; the means do.

To set them for good, on an idle machine: `node scripts/perf.mjs noise` with
the default mask and with `--affinity 0x0550` (one logical CPU a core), and
an A/A compare (`compare --base HEAD`), then the statistic and each scene's
threshold from what those show.

### Results

`perf-results/<machine>/<tag>.json`, committed: `<machine>` the host name in
lower case. `perf-results/local/` holds everything else (ad hoc runs,
compares, noise) and is ignored.

At a tag, from a clean checkout of it:

1. `node scripts/perf.mjs run --record`: writes
   `perf-results/<machine>/<tag>.json`, refused unless HEAD is exactly a tag
   with nothing uncommitted.
2. Commit that one file to `main`.

A committed result is a record, read beside others from the same machine and
window size (the sandbox's window is 80% of the primary monitor's work area),
and never a threshold. When a game's frame rate drops after an engine bump,
`compare --base <old tag>` from a checkout of the new one answers it; the
committed files say where to look.

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
