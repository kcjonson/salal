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
(`sdf_minimal`, `polygon`, `arc`, `polyline`, `blend`, `input_test`, and
ui's `ui_` scenes; the `perf_` scenes are the perf suite's, below). A new
drawing feature adds a scene or extends one.

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

Four sandbox scenes, each fixed content from a fixed seed, with any motion a
function of the tick, so every frame of a tick draws the same. The first three
were built to measure what the instanced-quad and texture-slot rework
([ui-design.md](../ui-design.md), Per-draw data and Texture residency)
changed: the batcher's CPU time per quad, the bytes it uploads, and the draw
calls texture changes cost. The fourth is ui's walk.

| scene | what it draws | what it guards |
|---|---|---|
| `perf_quads` | 10,000 overlapping rects: radii, borders inside, centred and outside, some gradients and shadows, under a scrolling translate | per-quad batcher time and upload bytes: 10,527 instances in one draw call |
| `perf_text` | 223 list rows in six sizes, 11 to 24 px: a background, an image icon from twelve that cycle, a label in one font atlas and a value and unit in a second (the sandbox's font loaded twice) | glyph batching and texture slots: fourteen textures a frame, so a run is cut each time it needs a ninth, 38 draw calls a frame |
| `perf_world` | worldsim's VectorPerf 10,000 stars inside a clip, 120 stroked roads, a flush, 3,000 instanced trees, 400 rotating polygons from six cached silhouettes sorted by key, a barrier, 200 labels, then four clipped screen panels, under a panning, zooming camera | the world path: triangles as quads, strokes, the key sort, barriers, clips and flushes |
| `perf_ui` | 1,250 cards of eight ui components each, three deep, one in ten raised by zIndex, on a stage that moves with the tick, under a menu anchored to a card, a modal dialog over its scrim, two toasts and a tooltip | ui's walk and layout, its key sort and the overlay stack's placement, over 10,049 components |

### What a capture holds

`/api/control?action=capture&op=start` starts a capture and turns the
renderer's GPU timing on; `op=stop` puts the timing back as it found it and
answers with the summary. A capture holds every whole frame between the two.
Each of these is reported as p50, p99, mean and max over the frames (nearest
rank), or null over none:

| field | what it is |
|---|---|
| `frameMs` | start to start: the frame's work, present and pacing |
| `cpuMs` | poll, input, update, ui layout, render and the dev server's post-render step: the CPU's work, without present |
| `presentMs` | the swap, which waits when the GPU or the compositor is behind |
| `layoutMs` | ui's layout pass before step 9; it runs on frames with a tick, so at hundreds of frames a second its p50 is near 0 and its mean is what to read |
| `renderMs` | step 9, the scenes, their ui and the debug layers drawing, and the batch's flushes; in these scenes, which only draw, the batcher's and ui's walk's time |
| `flushMs` | of that, the flushes: the plan, the upload and the draw calls |
| `gpuMs` | GPU time of the frame's clears, flushes, instanced meshes and baked passes (below), over the frames whose results came back |
| `drawCalls`, `instances`, `vertices`, `triangles`, `uploadBytes` | the batch's counts: an instance a quad (104 bytes, six vertices, two triangles) and one for each copy of an instanced mesh, `uploadBytes` the instance bytes written |

Beside them: `gpuFrames` and `gpuDropped`, the GPU results that came back
and those that didn't in time; `gpuSupported`, false on a driver with no
timestamps; `minimizedFrames`; and what the run had at stop, its
`framebuffer`, whether it `resized`, and `vsync` (a settings file's
`video.vsync` beats `--no-vsync`).

GPU time comes from `GPUFrameTimer`: each clear, each flush's instanced draw
calls, each instanced mesh draw and each baked pass is a span between two of
the backend's timestamp queries (no GL past the boundary), a span inside
another is part of it (a depth count, so an inner end never closes the outer
span), and a frame's spans are summed, so a frame the CPU holds up reads what
the GPU did, not how long it waited. A frame is one `App::render`, which calls
`Primitives::beginFrameStats()` once; `Primitives::beginFrame()` starts a
batch only, so a pass that begins a batch of its own inside the frame (an
offscreen render, worldsim's `AssetRenderer`) adds to the frame's counts and
GPU time instead of starting another. Results are read four frames late and
never waited for; one not back by then is dropped and counted. It costs
nothing while no capture runs, reads 0 on the null backend, and on a GL driver
whose `GL_TIMESTAMP` has no counter bits (some of macOS's) it times nothing
and says so. `/api/metrics` carries the live per-frame counts too.

### The runner

`node scripts/perf.mjs` builds the sandbox Release with
`SALAL_DEV_SERVER_IN_RELEASE` in `build-perf/`, beside `build/`: Release
because it is what ships (RelWithDebInfo inlines less on MSVC), and its own
build tree so a perf run leaves the developer's build alone. Each run
launches one scene with `--scene`, `--http-port 8094` (never 8090) and
`--no-vsync`, waits for `/api/health`, checks the scene it asked for is the
one running, warms up 3 s, captures 5 s, reads `/api/metrics`, and exits
through `/api/control?action=exit`. On a machine with 16 or more logical CPUs
it pins the sandbox to one logical CPU on each of four cores, 4, 6, 8 and 10 (`0x0550`) (`--affinity` for another mask); on a
smaller one, or macOS, it runs unpinned, says so, and won't compare without an
explicit `--threshold`. Before each run it waits for any compiler, linker,
`ninja`, `cmake`, `ctest`, test binary, worldsim app or other sandbox to
finish and for its cores to be under 25% busy, and checks the port right
before launching. After a run, if one of those is running or the rest of the
machine was more than 20% busy during the capture, the run is made again, up
to three times, then kept and flagged.

```
node scripts/perf.mjs run                  # this checkout, into perf-results/local/
node scripts/perf.mjs compare              # where HEAD left origin/main, built in a scratch worktree, against this checkout
node scripts/perf.mjs compare --base v0.30.1
node scripts/perf.mjs noise                # one build's spread, and the gate's error rates on it
node scripts/perf.mjs noise --from <file>  # the error rates of a saved noise result again
```

Compare's default base is the merge-base of HEAD and `origin/main`, fetched
first, so a stale local `main` never makes the base older than the branch
point, and it prints both commits. It builds the base in a worktree under the
OS temp folder (one a checkout and ref, kept for the next run's incremental
build, and `git worktree prune` first, so a folder cleaned away doesn't block
it; `git worktree remove` it when done), or takes `--base-exe`, and runs the
two builds interleaved, five runs each, alternating which goes first. A scene
the base build lacks (from its `--help`) runs on the head only and is
reported, not gated; a base with none of the scenes is invalid.

### The gate

A build's figure for a timing is the fastest of its five runs' p50s, since
interference only slows a run. Its spread is how far the second fastest is
above it; a side whose spread is wider than the threshold hasn't reproduced
its figure. Exit codes:

- **1, regressed**: a gated timing more than its scene's threshold above the
  base, or a count up: draw calls, instances, vertices and upload bytes are
  fixed for fixed content, so a count fails when every head run is above every
  base run (a moving camera culls a little differently in each capture window,
  which moves perf_world's medians by a few hundredths of a percent);
  `--allow-counts` reports them instead.
- **3, inconclusive**: a side's spread wider than the threshold, a run that
  went ahead on a busy machine or was still disturbed after its retries, or
  scene content or a window size that differs between the builds.
- **2, invalid**: a bad option, a run that couldn't be made (a missing exe or
  scene, a port in use, a sandbox that didn't start), or data that can't be
  gated: a gated value or count missing, a timing zero or not finite, under 200
  frames, a minimized frame, a resize, vsync on, or GPU results for under 90%
  of a run's frames.

### Thresholds

| scene | gated, fastest of 5 | threshold | no-op fails | no-op inconclusive | +5% caught |
|---|---|---|---|---|---|
| `perf_quads` | `cpuMs`, `renderMs`, `gpuMs` p50 | 3% | 0% | 0.19%, 0.19%, 0% | 99.8%, 99.8%, 100% |
| `perf_text` | `cpuMs`, `renderMs`, `gpuMs` p50 | 3% | 0% | 0% | 100% |
| `perf_world` | `cpuMs`, `renderMs`, `gpuMs` p50 | 3% | 0% | 0% | 99.9% |
| `perf_ui` | `cpuMs`, `renderMs`, `gpuMs` p50 | 4% | 0.66%, 0.10%, 0.27% | 6.9%, 1.6%, 1.8% | 69%, 89%, 67% |

Calibrated on `kevinsgamingdesktop` (Ryzen 9 5950X, RTX 3090, Windows 11,
the sandbox's window 3072 by 1728) on 2026-10-31 from 12:50 UTC: `noise`, 20
runs a scene pinned to `0x0550`, after 15 minutes with no build or test on
the machine and none during it. The only other load was three idle webpack dev
servers, about 11% of the CPU, and a screensaver drawing on the GPU. Each
rate is over all 46.6 million ways of splitting the 20 runs into a base and a
head of five, the head scaled by 1.05 for the regression; resampling with
replacement, as the first calibration did, can put one run on both sides.

The 20 runs' spread, each the run's p50:

| scene | `cpuMs` p50: median, range, cv | `renderMs` p50 | `gpuMs` p50 |
|---|---|---|---|
| `perf_quads` | 0.944 ms, 7.7%, 2.2% | 0.936 ms, 7.5%, 2.2% | 0.288 ms, 3.9%, 1.1% |
| `perf_text` | 0.401 ms, 2.4%, 0.6% | 0.394 ms, 2.4%, 0.6% | 0.089 ms, 3.4%, 0.9% |
| `perf_world` | 3.725 ms, 16.9%, 3.6% | 3.708 ms, 16.9%, 3.6% | 0.292 ms, 10.2%, 2.1% |
| `perf_ui` | 2.999 ms, 8.6%, 2.7% | 2.839 ms, 7.8%, 2.4% | 0.376 ms, 5.4%, 1.6% |

The fastest of five matched or beat the median everywhere: `perf_quads` lands
about one launch in four 2 to 7% slow (0.96 to 1.01 ms against 0.94), which
the fastest ignores and which leaves the median's middle runs apart, so over
40% of its no-op compares came out inconclusive at every threshold from 2 to
4%. At 3%, the
scenes but `perf_ui` catch a 5% regression in all but a fraction of a percent
of splits without a false fail; 2% does too, but leaves no room for a session
noisier than this one. `perf_ui` varies launch to launch by itself (2.90 to
3.16 ms over the 20 runs, with no outlier or step), so `cpuMs` and `gpuMs`
need 4% (`renderMs` passes at 3% and is held at 4% with them), where a 5%
regression fails two compares in three on `cpuMs` and `gpuMs` and nine in
ten on `renderMs`, and a 10% one 93 to 98% of them.

The mask is the one thing not settled. `0x0550`, one logical CPU on each
of four cores, is the one measured, and tight. The same noise on `0x0FF0`
(logical CPUs 4 to 11, both threads of each core) started straight after it
and was discarded half a minute in, when another Claude session started
building on the machine; it didn't get another 15 quiet minutes before the
window closed at 14:00 UTC. So `0x0550` is the default without having been
measured against `0x0FF0`, and the next idle window should run
`noise --affinity 0x0FF0` and keep whichever is tighter. That session had
been building and testing dual-deck-builder every few minutes since 09:55 UTC,
which left one 15-minute gap in four hours; runs taken while it worked were
thrown away, since perf runs only while no other session works.

### The instanced batch, before and after

Not measured yet: the interleaved compare of the suite before the merge
(`8b9ae5f`, in a scratch worktree) against the merged head, and the merged
head's result for `perf-results/`, were queued behind the noise and didn't
get a quiet window before 14:00 UTC. The counts don't need one:

| scene | draw calls | vertices before, instances after | upload bytes a frame |
|---|---|---|---|
| `perf_quads` | 1, 1 | 42,108 vertices, 10,527 instances | 3,621,288, 1,094,808 (-70%) |
| `perf_text` | 671, 38 | 20,012 vertices, 5,003 instances | 1,721,032, 520,312 (-70%) |
| `perf_world` | 3, 3 | 185,578 vertices, 97,125 instances (3,000 of them trees) | about 12.86 MB, 9.89 MB (-23%) |
| `perf_ui` | new: 1 | 16,504 instances | 1,716,416 |

Read from a capture of each build; the counts are the same in every run and
every frame but `perf_world`'s, which its camera moves by a few hundred
bytes.

To run it: `node scripts/perf.mjs compare --base 8b9ae5f --allow-counts`
from a checkout of the merged head, which runs `perf_ui` on the head only.

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
