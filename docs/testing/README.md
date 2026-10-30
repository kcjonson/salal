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
| `perf_text` | 223 list rows in six sizes, 11 to 24 px: a background, an image icon, a label in one font atlas and a value and unit in a second (the sandbox's font loaded twice) | glyph batching, and texture splits: three draw calls a row today (671 a frame), which texture slots make one |
| `perf_world` | worldsim's VectorPerf 10,000 stars inside a clip, 120 stroked roads, a flush, 3,000 instanced trees, 400 rotating polygons from six cached silhouettes sorted by z, a barrier, 200 labels, then four clipped screen panels, under a panning, zooming camera | the world path: triangles, strokes, the z sort, barriers, clips and flushes |

A ui tree with popups open and icons come with phase 3, when there is a ui
walk, a popup stack and an icon atlas to measure.

### What a capture holds

`/api/control?action=capture&op=start` starts a capture and turns the
renderer's GPU timing on; `op=stop` turns it off and answers with the
summary. A capture holds every whole frame between the two. Each of these is
reported as p50, p99, mean and max over the frames (nearest rank):

| field | what it is |
|---|---|
| `frameMs` | start to start: the frame's work, present and pacing |
| `cpuMs` | poll, input, update, render and the dev server's post-render step: the CPU's work, without present |
| `presentMs` | the swap, which waits when the GPU or the compositor is behind |
| `renderMs` | step 9, the scenes and overlays drawing and the batch's flushes; in these scenes, which only draw, the batcher's time |
| `flushMs` | of that, the flushes: the plan, the uploads and the draw calls |
| `gpuMs` | GPU time of each clear, flush, instanced draw and baked pass from a pair of timestamp queries, summed per frame (`GPUFrameTimer`), so a frame the CPU holds up reads what the GPU did, not how long it waited; read four frames late, never stalling |
| `drawCalls`, `vertices`, `triangles`, `instances`, `uploadBytes` | the batch's counts, `uploadBytes` the vertex, index and instance bytes it wrote |

GPU timing sits behind the backend's timestamp queries (no GL past the
boundary), costs nothing while no capture runs, and reads 0 on the null
backend. `/api/metrics` carries the live per-frame counts too.

### The runner

`node scripts/perf.mjs` builds the sandbox Release with
`SALAL_DEV_SERVER_IN_RELEASE` in `build-perf/`, beside `build/`: Release
because it is what ships (RelWithDebInfo inlines less on MSVC), and its own
build tree so a perf run leaves the developer's build alone. Each run
launches one scene with `--scene`, `--http-port 8094` (checked free first;
never 8090) and `--no-vsync`, pinned to logical CPUs 4 to 11, waits for
`/api/health`, warms up 3 s, captures 5 s, reads `/api/metrics`, and exits
through `/api/control?action=exit`. Before each run it waits for any `cl`,
`link` or `ninja` (another session's build) to finish and for its cores to
be under 25% busy, and notes in the run what it gave up waiting for. A build's figure for a statistic is the median of its five
runs.

```
node scripts/perf.mjs run                  # this checkout, into perf-results/local/
node scripts/perf.mjs compare              # main, built in a scratch worktree, against this checkout
node scripts/perf.mjs compare --base v0.20.0
node scripts/perf.mjs noise --runs 10      # one build's run-to-run spread
```

Compare builds the base in a worktree under the OS temp folder
(`salal-perf/<ref>`, kept for the next run's incremental build; `git worktree
remove` it when done), or takes `--base-exe`, and runs the two interleaved,
alternating which goes first. It fails when the head's `cpuMs` or `renderMs`
p50 is more than 4% above the base's, or its `gpuMs` p50 in `perf_quads`.
The other GPU times, tails, present and the counts are reported, not gated:
the rework changes the counts on purpose.

### Noise and the threshold

Measured on `kevinsgamingdesktop` (Ryzen 9 5950X, RTX 3090, Windows 11),
2026-10-30: ten runs of one Release build in each of two sessions about an
hour apart, p50s. Range, CV and A/A gap are the worse of the two sessions.

| scene | statistic | median ms, session 1 | session 2 | range | CV | A/A gap, 5 runs a side | range unpinned |
|---|---|---|---|---|---|---|---|
| `perf_quads` | `cpuMs` | 0.917 | 0.794 | 2.3% | 0.8% | 1.2% | 14.0% |
| | `gpuMs` | 0.205 | 0.205 | 0.0% | 0.0% | 0.0% | 1.2% |
| `perf_text` | `cpuMs` | 0.719 | 0.475 | 4.7% | 1.3% | 1.0% | 36.8% |
| | `gpuMs` | 0.069 | 0.069 | 5.9% | 2.0% | 1.5% | 5.9% |
| `perf_world` | `cpuMs` | 4.347 | 3.852 | 15.7% | 4.8% | 0.7% | 15.3% |
| | `gpuMs` | 0.284 | 0.178 | 37.2% | 17.8% | 4.4% | 49.8% |

The A/A gap is the worst gap between the medians of two disjoint groups of
five runs: what a compare of one build against itself could show. `renderMs`
moves with `cpuMs`. Unpinned, a launch landed on a different core or CCD
and `perf_text`'s CPU time moved by a third, which is why the runner pins.
`perf_world`'s 15.7% range in session 2 is one run of ten; the median of five
doesn't see it.

The threshold, 4%, is over three times the worst CPU A/A gap, so noise alone
doesn't fail a compare, and a 5% regression clears it. An A/A compare, this
build against the same commit built in the scratch worktree, moved no CPU
figure more than 0.8%, but `perf_text`'s GPU time, 70 us, where a few
microseconds are 3%, moved 3.0%, so GPU time is gated in `perf_quads` only.
`perf_world`'s isn't either: in session 1 its present waited about 5 ms in
some launches and not others (a frame of 9.7 or 4.4 ms, the CPU's time the
same), and the GPU's clocks, and its time, followed.

Between sessions the level moved by up to a third (`perf_text` 0.719 to
0.475 ms) while each session stayed tight. The cause isn't pinned down;
other sessions' builds and tests on the same machine are the likely one, so
the runner now waits for the cores it runs on to be under 25% busy and
records their load before each run and the rest of the machine's during it.
This is why a decision is always a compare, its two builds interleaved in
one session, and never a run read against a committed result.

### Results

`perf-results/<machine>/<describe>.json`, committed: `<machine>` the host
name in lower case, `<describe>` `git describe --tags` of the commit
measured, which at a tag is the tag. `perf-results/local/` holds everything
else (ad hoc runs, compares, noise) and is ignored.

At a tag, from a clean checkout of it:

1. `node scripts/perf.mjs run --record`: writes
   `perf-results/<machine>/<tag>.json`, refused with uncommitted changes.
2. Commit that one file to `main`.

A committed result is a record, read beside others from the same machine and
window size (the sandbox's window is 80% of the primary monitor's work area),
and never a threshold: sessions differ by more than one does (above). When a
game's frame rate drops after an engine bump, `compare --base <old tag>` from
a checkout of the new one answers it; the committed files say where to look.

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
