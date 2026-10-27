# Consumer requirements

What the three consumers need from the engine, in one place, so the API is
shaped by all three and not by worldsim alone. Sources: the asteroids-roguelike
survey (repo HEAD 176064c, 2026-10-25), the dual-deck-builder survey (HEAD
afcd7b16, 2026-10-24), and the worldsim surveys (main @ db335a2b,
2026-10-25). Paths are `repo/path:line`.

Tags on the asteroids items: [P] the feel prototype; [QR] Quick Run, the first
shippable mode; [later] campaign or polish. "Required" means the asteroids docs
state it; "inferred" means it follows from what they state.

## Asteroids-roguelike

No source code exists; 19 files, 17 of them design docs, about 2,924 lines,
and no CLAUDE.md, `.claude/` or `.mcp.json`. The `.gitignore` is left over from a dead TypeScript plan and
has no CMake or vcpkg entries. tech-stack.md asks for C++20, OpenGL 3.3+
through GLEW, GLFW3, GLM, CMake 3.25+ with vcpkg, Ninja, MSVC on Windows,
ccache, and worldsim's conventions
(`asteroids-roguelike/docs/tech-stack.md:13-21, :172-183`). Dependencies at
`:197-206`: opengl, glfw3, glew, glm, nlohmann-json, stb, nanosvg, msdfgen,
cpp-httplib, gtest, benchmark, plus an audio library (miniaudio or
openal-soft). Lua, sol2 and pugixml dropped. It adopts the four-layer render
stack as-is (`:59-75`) and draws the arena through the same Primitives API the
UI uses; the HUD is diegetic, in world space, and needs no ui components
(`asteroids-roguelike/docs/art-audio.md:58-68`). It wants "roughly a quarter
of worldsim's code" (`tech-stack.md:110`).

### Frame loop [P], required

- Fixed step at 120 Hz, render interpolating between the last two states
  (`tech-stack.md:117-122`; `physics.md:236-244`). Tunnelling happens at
  60 Hz, and "every number in physics.md assumes it".
- Clamp the accumulator (`tech-stack.md:122`).
- A tick function the platform calls, not a loop owning main() (`:257-259`).
- Thrust response with no perceptible latency (`physics.md:254-255`).
  Interpolation costs up to one tick, about 8 ms. Inferred: vsync control and
  a steady-clock accumulator.
- Settings has a video section (`screens.md:17, 91`). Inferred: fullscreen,
  resolution and vsync toggles.
- Pause freezes both players (`screens.md:141`). Inferred: the accumulator
  freezes while paused and on focus loss.

### Input [P] keyboard plus one pad, [QR] the rest

- Gamepad through GLFW; couch co-op needs two pads with hot-plug and
  per-device player-slot claiming, polled once per sim tick, with keyboard
  events folded into the same per-tick snapshot (`tech-stack.md:124-128`;
  `multiplayer.md:113`).
- Layouts (`multiplayer.md:111-117`): two pads; one pad plus keyboard; split
  keyboard (arrows and right-hand keys for P1, WASD and left-hand keys for
  P2).
- Slot claiming: solo, whoever presses a button becomes P1
  (`multiplayer.md:119-120`). Co-op, two cards, each claimed by pressing a
  button on the device that will fly it (`multiplayer.md:124-126`;
  `screens.md:111-112`). Needs per-device identity and an "any device
  pressed" event. At most two player slots, but three or more devices at
  once.
- Controls (`physics.md:9-17`): rotate left and right, thrust, fire. Pad:
  left stick or triggers rotate (open), right trigger thrust, face button
  fire. Keyboard: arrows or WASD, space.
- Analog versus digital rotation is open (`physics.md:19-22`;
  `open-questions.md:108-114`), so the snapshot must carry float axes with
  deadzone as well as buttons.
- Rebinding: Settings has an input-binding section
  (`screens.md:17, 91, 255`). Implies action maps per device per player,
  persisted. Inferred: a capture-next-input API.
- Inferred: a press shorter than a tick must not be lost at 120 Hz. The
  snapshot's per-button transition counts cover it (decision D).
- Mouse and pointer are never mentioned. Controller and keyboard only.

### PRNG and determinism [P], required

- One seeded PRNG (`xoshiro128**` "or similar"), nothing unseeded in sim, no
  reliance on hash-container iteration order, no platform-varying floating
  point where avoidable, inputs recorded with the seed
  (`tech-stack.md:130-135`; `physics.md:246-248`).
- "fork" appears nowhere in the asteroids docs; forking comes from salal's
  architecture.md. Asteroids still needs independent streams: spawn, drops,
  salvage, shop offers and reroll (`modes.md:78-82`), fragment break, and a
  cosmetic or VFX stream that must not perturb sim.
- The Arena contract takes `arenaSeed` (`screens.md:135`). Headless tests run
  a seeded sortie N ticks and assert invariants (`tech-stack.md:213-216`).
- Inferred: PRNG state serializable; the fork hash explicitly specified (not
  `std::hash`); cross-platform replay needs deterministic float behaviour (no
  FMA contraction, careful trig).

### Audio

Nothing exists in worldsim: no vcpkg port, zero hits for miniaudio, openal,
SDL_mixer, SDL2, fmod, portaudio or soloud, only an unused
`LogCategory::Audio`.

- Priority mix: warnings beat impacts beat thrust beat music; duck everything
  below a warning (`art-audio.md:72-74`). Buses or priority groups with
  ducking. [QR]
- Thrust: a continuous layered loop whose pitch and filter track mass and
  velocity (`art-audio.md:76-78`), with a spool-up envelope (`:36-37`). A
  looping voice with per-frame pitch and filter. [P], because the feel
  checklist wants thrust audible.
- Impacts pitched by fragment size in a rising melodic figure (`:80-82`);
  non-breaking hits vary with impulse (`:84-87`). Per-play pitch and gain.
- Music is stem-based, layering with `pressure`: a drone bed, then percussion
  and harmonics at thresholds (`:89-93`). Sample-synchronized stems with
  per-stem gain driven by a game scalar. [QR]
- Station audio close, quiet and dry; silence after a sortie is a reward
  (`:95`).
- "Procedural SFX" appears only in `tech-stack.md:138-139`; art-audio.md asks
  for parameter-driven playback, not synthesis. The "four-channel" reference
  (`:91-93`) suggests stereo panning by screen position (inferred).
- Settings has an audio section (`screens.md:17, 91`): master, music and sfx
  volumes (inferred).

### Rendering

- Flat shapes: asteroids are flat silhouettes with a single bright rim light
  (`art-audio.md:15-16`), plus rings, plumes and beams. architecture.md's
  shape list (Rectangle, Circle, Line, Text, Triangle) has no arbitrary
  polygon.
- Irregular, asymmetric asteroid silhouettes are required
  (`art-audio.md:44-47`); generated versus authored is unstated. The
  vector/SVG pipeline is called a good fit (`tech-stack.md:79-84`).
- SDF text crisp at any scale, HUD at couch distance, station screens dense
  with numbers (`tech-stack.md:85-86`).
- Bloom, additive glow and particles: "real bloom, real additive glow, and
  proper particle work" (`art-audio.md:17-19`); "flat shapes, procedural
  glow, particle systems doing the heavy lifting" (`:100-102`). Neither
  tech-stack.md nor salal mentions render targets, blend modes or particles.
  [QR/later]; the prototype is white shapes (`art-audio.md:104-106`).
- Also required: a HUD ring around the ship, a hold-fill indicator on the
  hull silhouette (`art-audio.md:62-63`), warning rings, the predicted shot
  path (`assists.md:92-94`), a beam (`enemies.md:47`).
- Wrap ghosts: draw ghosts of every entity within one screen width or height
  of an edge (`physics.md:93-94`). Scale: about 60 asteroids, 4 drones, loose
  ore, 2 ships (`art-audio.md:5-6`), "a few hundred flat shapes", times up to
  4 for ghosts, plus particles.
- A fixed single-screen arena: no camera zoom, no split screen
  (`multiplayer.md:136-137`). Inferred: a fixed-aspect virtual viewport with
  letterboxing under a resizable window, which no doc addresses.
- Accessibility settings that touch rendering: colorblind palettes, text
  size, screen shake, flash reduction (`screens.md:91-92`). Inferred: a
  runtime-switchable theme palette, a UI text scale, and shake and flash
  hooks.

Shape primitives missing from the current list: filled polygon with a
per-frame transform, ring or arc (partial fill), line with width (beams,
predicted path), stroke or rim-light on a silhouette, clip or mask by
silhouette (hold-fill), and a transform or camera stack for wrap ghosts. Bloom
needs an FBO or render-to-texture, a blur chain, and additive blend state in
the batch sort key; HDR bloom wants float colour attachments, which GL 3.3
core has. Particles exist nowhere in salal or worldsim; game-side over
Primitives is the cheap start, an engine pool with batch-friendly additive
draw the likely end state. Decision G, settled 2026-10-26.

### Inspector

- Wants `/api/ui/screenshot`, `/api/ui/tree`, `/api/ui/lint`, `/api/input`,
  `/api/control`, lint 0 before a screenshot, bounds from the tree
  (`tech-stack.md:88-97, :218-220`). The ui-sandbox second executable comes
  with it (`:97-98`).
- The dev server sits behind a compile flag (`:259`).
- Inferred: an arena drawn with Primitives is invisible to `/api/ui/tree`.
  Verifying it needs game-registered endpoints (sim state, step N ticks, set
  or read the seed, pause), synthetic pad and axis injection in `/api/input`,
  and replay record and playback.
- The debug overlay "from day one": velocity, mass, thrust-to-mass, angular
  velocity, entity counts, draw calls, pressure; "half of it is free" from
  BatchRenderer stats (`tech-stack.md:224-230`).

### Scene stack and payloads

- Pause over the Arena leading to the Fit editor and back
  (`screens.md:66-69`); the Fit editor reached from two places (`:122-126`);
  the Belt map closes back to wherever it opened from (`:210`). So: overlays,
  return-to-caller, a stack.
- Typed payloads: Arena takes
  `{ mode, players[], arenaSeed, spawnTable, pressureCurve, routeEffects? }`
  and Player setup takes `{ mode, campaign?, route?, targetScore? }`
  (`screens.md:120, 135`).
- Arena <-> Wave shop round trips (`screens.md:57-58`) need run-scoped state
  outside the scene; run-scoped versus persistent state is `modes.md:5-9`.
- The scene hook shape (fixed update, variable update, render(alpha)) is
  unspecified in salal.
- Boot skippable on any input (`screens.md:76-77`); Run summary retry is one
  keypress (`:156-158`).

### Persistence [QR] saves and settings, [later] the rest

Profiles and per-player fits (`assists.md:44`; `multiplayer.md:128`;
`screens.md:113-114`), campaign saves with Continue
(`screens.md:82-83, 164-165`; `modes.md:132-133` mentions save migrations),
local leaderboards bracketed by fit (`modes.md:109-115`; `screens.md:99-102`),
settings. Salal has config only. Needs a versioned save abstraction.
Leaderboards are local only (inferred; no backend, `tech-stack.md:283-287`).

### UI inventory and controller-first rules

No widget list exists in the asteroids docs; this is implied by screens.md. 19
screens, all fully usable by pad and keyboard.

- Controller-first, no hover. Disabled controls explain themselves on screen,
  not in a tooltip (`screens.md:83-86, :173-174`). So a disabled-with-reason
  widget state readable under focus navigation.
- Widgets implied: buttons with disabled plus reason, selection cards, toggle
  lists, sliders (volumes, text size, the co-op "personal cut" slider at
  `multiplayer.md:53-54`), save rows (`screens.md:164`), leaderboard tables,
  side-by-side panes (Outfitting `:184-191`), side panels
  (`:167-171, 201-202`), a progress indicator (Boot `:76`), key-binding
  capture, a confirm dialog. The Belt map is a custom-drawn scene
  (`:206-208`).
- No confirmation on fit changes (`screens.md:128`); confirm only for
  quit-losing-a-run (`:141-142`).
- The co-op wave shop: both players shop from the same pool at the same time
  (`screens.md:151-152, 265-266`), two simultaneous focus contexts bound to
  devices. Worldsim's `focus` is 1,422 lines and nothing in it is per-player.
  The design calls simultaneous "the current call", so this is negotiable
  (open decision I).
- Shared-screen rules (`multiplayer.md:131-143`): one fixed screen,
  per-player colour plus silhouette, nothing important communicated by sound
  without a visual partner (the docs bend this with audio-only handicaps at
  `assists.md:133` and `enemies.md:94`).
- The HUD is drawn in world space rather than with ui components: diegetic,
  anchored to the ship, never in the corners (`art-audio.md:58-68`;
  `multiplayer.md:140-141`), with interpolated positions and wrap ghosts.
- Per-player state everywhere; two player slots max. No localization
  (`tech-stack.md:286`).

### The feel prototype's real scope

Stated scope: a white triangle in a field of white circles, with mass, impulse
and recoil; sim, the fixed-step loop, enough renderer for a few hundred flat
shapes, no UI layer (`tech-stack.md:239-243`; `open-questions.md:11-34`;
`physics.md:250-268`). Exit criterion: "a plain white ship in a field of white
circles is fun for ten minutes" (`physics.md:266-268`).

What the checklist implies on top of that: a window with keyboard and pad
input; audible thrust and impacts (`physics.md:254-255`), so a minimal audio
path; a mass-visible thruster plume (`physics.md:256-257`;
`art-audio.md:32-37`); a cargo value that raises mass
(`open-questions.md:18-19`); and the debug overlay, which needs text. So the
prototype needs text, audio and `app` (window, input, loop), none of which the
"no UI layer" framing admits. Text is why FontRenderer moves into `renderer`
(settled); audio is open decision B, scheduled after app for this reason; app
is phase 2, which is where the prototype starts.

### Testing

gtest for toroidal distance, collision and mass conservation; headless seeded
sortie tests (`tech-stack.md:208-222`). No feel tests. Needs foundation (and
ideally the tick core) to build headless with no GL or GLFW (`:213-216`).
Physics is hand-written in sim: circles, one convex hull per ship, impulses at
contact points, angular state on asteroids, toroidal space; no Box2D or
Chipmunk (`tech-stack.md:26-37`). From the engine it asks foundation math,
memory arenas for per-tick scratch, and a headless-linkable foundation. sim is
data-oriented, "not an ECS framework" (`:165-168`).

Game-side, for awareness: wrap-aware interpolation, and a sim-to-presentation
event channel (impact at contact point with impulse, break size, pressure
thresholds), since sim cannot call audio or VFX.

Not doing (`tech-stack.md:283-287`): Unity or Godot, general physics, netplay,
level editor, localization, analytics, backend. Platforms: Windows first, then
macOS and Linux (`open-questions.md:167-169`).

### Conflicts between the asteroids docs and salal's

To fix in the asteroids repo later; recorded here so the engine side does not
inherit them.

| # | conflict | where | resolution |
|---|---|---|---|
| 1 | The layer diagram draws `app > engine > ui, sim > renderer, foundation`. Salal has no `engine` library; asteroids' `engine` is salal's `app`, its `app` is the game executable. It puts engine above sim, and sim level with ui above renderer, while `:160-161` says sim never depends on renderer or ui | `tech-stack.md:150-158` | redraw with salal's four libraries; sim depends on foundation only |
| 2 | "Leave dual-deck-builder in TypeScript" | `tech-stack.md:266-271` | stale; salal decided the opposite |
| 3 | Loop, gamepad and PRNG are "in sim or next to it" at `:114-116` and belong to the engine at `:273-277`; "four things" at `:114` against "three things" at `:275` | `tech-stack.md` | the later reading holds; they are engine work |
| 4 | `xoshiro128**`, no forking | `tech-stack.md:131-132` | salal requires forking, and DDB parity wants sfc32 plus Murmur (decision A); drop xoshiro |
| 5 | Audio status in salal is ambiguous: extraction.md lists "no audio" under what the engine is not and later says "the engine, probably"; architecture.md's library table has no audio row; no phase, no vcpkg entry | salal docs | decision B; architecture.md gets a proposed `audio` row |
| 6 | The prototype needs text (debug overlay), audio and `app`, while the docs say "no UI layer" and ui is extracted last | `tech-stack.md:239-243`; salal sequencing | FontRenderer into renderer; audio as phase 2b; the prototype starts at phase 2 |
| 7 | `/api/input` is "synthetic input injection in logical pixels" (pointer), asteroids has no pointer, and the arena is absent from `/api/ui/tree` | architecture.md | `/api/input` grows pad and axis injection; games register endpoints |
| 8 | Monorepo and "copy worldsim" language predates salal; "worldsim's OpenGL engine" | `tech-stack.md:3-4, :23, :39-57, :266-281`; `docs/README.md:18` | rewrite against salal |
| 9 | Input placed in `engine` (salal's `app`, above ui and renderer), so a sim consuming a snapshot type defined there would break the dependency rule | `tech-stack.md:153` | decision D, settled 2026-10-26: input types in `foundation/input/`, poller in `app`; sim consumes a game-owned action POD built from foundation's `ActionMap` |
| 10 | `DrawRect`/`DrawText`/`DrawCircle` PascalCase | `tech-stack.md:66`; salal README | functions are camelCase: `drawRect` |
| 11 | PRNG listed under foundation but sequenced in phase 2 "carve out app" | architecture.md; extraction.md | the fork API is in foundation, landed ahead of phase 2 as `random/Rng.h` (decision A); no conflict |

Checked and not conflicts: nothing assumes a loop owning main; screens and
scenes are reconciled; 120 Hz as a consumer choice matches; no physics,
networking or scripting matches; input consumed per tick matches; no feel tests
matches.

## Dual-deck-builder

TypeScript and WebGL2 today, migrating to C++. `src/renderer/engine` is 238
files / 68,185 lines (140 files / 37,163 non-test); `src/renderer/game` 523 /
138,266 (319 / 77,744 non-test); tests are 43.8% of the game layer (204 files,
60,522 lines). Engine imports zero game code; the only specifiers leaving
`engine/` are 20 asset and script imports (shaders, font atlases). Game
imports engine in 143 non-test files, but mechanics, ai and sim import nothing
from it. The scene lifecycle (Screen, ScreenManager) lives in `game/core`, so
an `app` equivalent would be derived from `src/index.ts` (Application),
`game/Game.ts`, `game/core/Screen.ts` and `game/core/ScreenManager.ts`.

### What its engine design contributes

The TS engine was rebuilt Aug to Oct 2026 to
`dual-deck-builder/docs/ui-rendering-spec/` (README plus chapters 00 to 17,
five research reports, five reviews), written 2026-09-07 as "a
backend-agnostic specification abstracted from worldsim (C++, OpenGL) ... so
that a second implementation (TypeScript, WebGL2) can be built to it".
Chapter 01 has a module map with a C++ (worldsim) column; chapter 14 scores
both implementations per rule (TS at 85 yes / 15 partial of 100 required
rows); chapter 16 has the OpenGL mapping, a 32-item worldsim gap list (16.2)
and a migration order for worldsim (16.4); research/01-03 are worldsim audits.
"The TS engine is better" means it conforms to a spec worldsim helped define,
in places where worldsim does not. It is the candidate normative spec for
salal's `ui` (open decision C), with chapter 16's gap list as the phase-3
worklist. Decision records sit in
`dual-deck-builder/docs/AI_TECHNICAL_DECISIONS/` (input-dispatcher,
focus-manager, drag-service, clock-and-animator, mount-context-and-frame-order,
overlay-and-popup-services, tree-snapshot-schema, uber-shader, font-pipeline,
stack-layout, theme-tokens, seeded-prng).

What the C++ `ui` should take from it, by subsystem.

Input and focus (`engine/input/`, 3,217 non-test lines: events.ts,
PointerAdapter.ts, Dispatcher.ts 1,274, FocusManager.ts 695, DragService.ts
439, HotkeyTable.ts, hitTest.ts). A platform adapter fills a queue of
`PlatformInput` (pointer down/move/up/cancel, leave, wheel, key down/up, blur;
positions already logical) and a dispatcher drains it once per frame, as the
first step of the frame, against the retained tree. It has no action maps, no
device layer and no per-tick snapshot. Hit testing walks the same tree in the
same order as render, honouring clip, content offset, inverse transform and
`pointerEvents`; disabled components occlude but do not receive; roots sit in
tiers (scene, overlay, diagnostic). Events bubble target to parent until
`consume()`; no capture phase; hover derived centrally; click synthesised at
the nearest common ancestor of press and release; capture per pointer. Key
order: Tab, focused keydown bubbling, Enter/Space activate and Escape cancel,
arrows to focus group then directional focus, InputObserver, HotkeyTable.
FocusManager: tab order derived from the tree and cached per scope, scopes
for modal trapping, roving groups, directional focus by a distance score,
`fixup()` after every layout, an `inputMode` of pointer, keyboard or
controller. DragService with threshold tokens (4 px mouse, 10 px touch) and a
ghost layer. The C++ analogue is GLFW callbacks pushing PlatformInput. Known
departures from the spec a port should carry: no char event, click-threshold
cancels clicks only on drag sources, text fields pass F-keys and paging keys
through, cursor inheritance differs.

Gamepad: none. No `getGamepads` anywhere; scaffolding only (`InputMode
'controller'`, `PointerType 'virtual'`, `ActionSource 'controller'`,
directional focus, activate and cancel). The adapter is specified in spec
R15.40 (poll once per frame, D-pad and left stick to focusDirection, confirm
and cancel to activate and cancel, right stick to a virtual pointer) and
deferred.

Observability (`engine/debug/`, 2,896 non-test lines). The transport is
browser window globals driven by Playwright `page.evaluate`, dev builds only
(`__DEV_TOOLS__`): `window.__ui.tree()` and `.lint()`, `window.__app` (scene,
reload, navigate, pause, resume, settleAnimations, status, holdFrames,
stepFrame, releaseFrames), `window.__dev.input(...)`, `window.__perf`. No
HTTP, no sockets, no screenshot or framebuffer capture (spec R13.34 describes
one; none exists; the Playwright golden harness was deleted 2026-10-19 and the
rule is "no pixel baselines"). The portable part is three pure functions, the
same schema worldsim serves over HTTP:

- Tree snapshot (`treeSnapshot.ts`, spec R13.22): a pure function of roots
  plus viewport. `Document {viewport:{width,height}, roots:[node]}`; node
  fields id, type, bounds, screenBounds, margin, zIndex, layer, visible,
  enabled, opacity, clip, contentOffset, transform, focusable, pointerEvents,
  handlesPointer, state{hovered, pressed, focused, focusVisible, selected,
  open, active, dropActive, checked?}, text{content, measured, overflow,
  wrap}, value, style, inkBounds, scroll, parked, stack, parts, children.
  Absent if the engine cannot back it; null means applies but unknown. Never
  throws, never forces layout.
- Layout lint (`layoutLint.ts`): a pure function over the snapshot, seven
  rules (sibling-overlap, child-outside-parent, outside-viewport,
  zero-or-negative-size, text-overflow, unreachable-interactive,
  target-size), result
  `{count, violations:[{rule, path, bounds, otherPath?, otherBounds?, bucket?}]}`,
  epsilon 0.5 px, dormant rules flagged. Count 0 is the intended merge gate
  (R13.29), not enforced in CI.
- Injection grammar (`inputScript.ts`): `move,x,y[,pointerId[,pointerType]]`,
  `down,x,y[,button[,pointerId[,pointerType]]]`, `up,...`, `click,...`,
  `cancel[,pointerId]`, `scroll,x,y,delta`, `keydown,<key>`, `keyup,<key>`;
  logical px, ignored while paused, nothing throws.

Perf: FrameTimer with five disjoint sections (input, update, layout, render,
flush) over a rolling 120-frame window; GpuTimer on EXT_disjoint_timer_query;
batcher counters carrying flush and split reasons; an in-engine
DeveloperOverlay in the diagnostic root tier, drawn through the engine's own
draw API.

Animation (`engine/animation/`, 479 non-test lines). Tween-only, frame-delta
driven: `Animator.tween({from, to, duration=200ms, ease=standard, onUpdate, onComplete, owner})`
on a number or number array, returning
`{value, running, done, cancel(), retarget(to)}`. Ease is CSS-style
cubic-bezier or a function; retarget restarts from the current value with CSS
Transitions' reversing-shortening; owner unmount cancels; reduced motion
completes every tween on first tick; `settle()` runs everything to the end.
No timelines, springs, keyframes, delay, stagger or looping. A Clock in ms
that moves only via `advance(ms)`, and a lint banning setTimeout, setInterval,
rAF, Date.now and performance.now outside rendering/ and debug/.
LookTransition tweens one 13-value array (fill, border, text RGBA, offsetY)
over 120 ms. Worldsim has no equivalent of any of this.

Coordinates and snapping (`engine/coords/`, 184 non-test lines, plus
`rendering/CanvasViewport.ts`). Everything public is logical px, origin
top-left, y down; `ratio = dpr * uiScale`; the ratio is consumed only in the
viewport, snapping and per-draw device constants, never by layout, components,
the tree walk or the shader. Snapping at submission only, under translate-only
transforms: `snapToDevice(v, ratio) = round(v*ratio)/ratio`, hairline rects
snap each edge, border width `max(1, round(w*ratio))/ratio`, text origin
snapped once per line, layout positions stay fractional.

Component model (`Component.ts`, 2,410 lines): one base class for leaves and
widgets (R8.1); no Layer class and no separate Shape hierarchy; "layer" means
a named z-layer (base, raised, overlay, modal, popup, toast, tooltip, drag,
transition, ordinals 0..80 step 10) and the batcher sorts by layer ordinal
only, zIndex never reaches it. No singletons: every service comes through a
MountContext (draw, dispatcher, focus, viewport, frame, clock, animator, drag,
placement, overlays, popups, tooltips, clipboard, assets); constructors touch
no service. Layout is fixed/hug/fill with fillWeight, min and max size,
direction, gap, padding, distribution and crossAlign; dirtiness propagates to
the nearest relayout boundary. Style is a closed CSS-named property set with
per-state sub-objects and tokens generated from `theme/tokens.json`. Draw API:
drawRect (fill, radius, border with position inside/center/outside, shadow),
drawCircle, drawLine, drawPolyline, drawPolygon, drawImage (nine-slice),
drawText, measureText; one shader program for every primitive, with packed
104-byte instances and per-draw clip as data, no scissor, premultiplied alpha.
This differs from architecture.md's Layer/Component/Shapes trio and from
worldsim; which model `ui` follows is part of decision C.

What it does not have, so the engine design cannot lean on it: no gamepad, no
HTTP transport, no screenshot, no fixed-step loop (variable-step rAF, delta
clamped to 0.25 s, five sections per tick; the game is turn-based and has no
real-time tick), no char event, no action-map or rebinding layer, no drag used
by game code yet.

### What Job B needs from the engine

Job B is porting the game layer to C++, tracked in DDB's own project.
Prerequisites and facts the engine side must honour:

- PRNG parity. `dual-deck-builder/src/renderer/game/core/Rng.ts` (247 lines,
  `RNG_VERSION = 1`) is sfc32 with PractRand's step
  (`t = a + b + counter++; a = b ^ (b >>> 9); b = c + (c << 3); c = rotl(c, 21) + t`,
  output t). Seeding from a uint32: `a, b, c = fmix32(seed + i * 0x9e3779b9)`
  for i = 1..3, counter 1, 15 draws discarded; fmix32 is the MurmurHash3
  finalizer. `fork(name, attempt = 0)`: MurmurHash3_x86_32 seeded with the
  parent's uint32 seed over each UTF-16 code unit of `name` as a
  little-endian uint32 block, then `attempt` as one block, no tail, final
  length term `(name.length + 1) * 4`, fmix32; the result is the child's
  seed, and fork is pure (it reads the parent's seed and never advances it).
  Draws: `float() = next() / 2^32`; `int(min, max) = min + (draw % span)`
  redrawing while `draw - draw % span > 2^32 - span`; `pick = int(0, n-1)`;
  shuffle is Fisher-Yates from the last index down with `int(0, i)` per
  index > 0; weighted picks walk linearly from `float() * total`, zero
  weights never chosen. Goldens were cross-checked against PractRand's sfc32
  and SMHasher's MurmurHash3_x86_32 compiled from C. Bit-identical
  differential testing requires the C++ PRNG to be this generator with this
  fork; a different one (xoshiro, Pcg32) cannot pass. Decision A.
- The sim closure. `game/sim/` is Fights.ts 222, Gen.ts 316, SimConfig.ts
  146, Measures.ts 56 = 740 non-test lines (859 with tests), but its static
  import closure is 110 files / 32,947 lines: map 45 / 15,233; mechanics
  18 / 8,097; campaign 21 / 5,020; ai 14 / 2,993; core 6 / 814; sim 4 / 744;
  engine 2 / 46 (`errors/reportToPage.ts`, `ui/stepGrid.ts`); 9 JSON data
  files. No DOM. Outside the closure: screens, ui, art, Game.ts. "Port the
  sim first" means porting most of mechanics, campaign, map and ai, verified
  by differential JSON; presentation (screens 22,295 and game ui 14,749
  non-test) comes last.
- Seeding structure the port reproduces: fights
  `new Rng({seed}).fork('run', runIndex)`, each fight
  `run.fork('fight', stopIndex)`, deck shuffles
  `fight.fork('deck:${team}:${seatIndex}')`, AIs
  `fight.fork('ai').fork('player'|'enemy')`; gen `seed = config.seed + i`,
  `root.fork('map', m).fork(stage, attempt)`, stops
  `root.fork('stop:${id}', rollCount)`. `AREA_MAP_GENERATOR_VERSION = 7`.
- Float discipline. `map/Geometry.ts` says all geometry is adds, multiplies,
  divides, sqrt and compares, with a polynomial unitVector and no trig. One
  transcendental found: `Math.log` and `Math.sqrt` in `ai/MCTSAI.ts:171`,
  probably off the sim path. Watch items: `Battle.ts:584` reads `Date.now()`
  in log lines; Model ids are a per-session counter; AI controllers are
  async. For C++: `-ffp-contract=off`, no fast-math, JS `Math.round` (half
  up) against `std::round`, fmod, JSON number formatting, object key order.
- The golden JSON corpus does not exist. `node scripts/balance-sim.mjs <mode>`
  runs `fights` and `gen` only (the spec's runs, calibrate and campaign modes
  and `--check` do not exist); `--json` writes `{mode, config, rows}` or
  `{mode, config, summary, maps}`, tab-indented. No identical-output harness
  across Node and browser exists, no browser sim exists (the Balance Lab Web
  Worker is specified, not built), and balance-sim is invoked from no
  workflow. Anchors today: `Fights.test.ts` pins seed 7 (road_warrior plus
  interceptor, route [2]: home, turns 3, hpLost 5/65, structureLost 5/130,
  injuryDays 1, raidersKilled 4, routed true); `Gen.test.ts` pins seed 1 (55
  routes, 264 stops, 102 fights, 133 own stops); SeededFight.test; the Rng
  goldens; baseline tables in `docs/specs/Balance Simulation.md`
  (2026-10-17). First step before Job B: generate a corpus from `--json` and
  commit it in DDB (open decision J).
- The TypeScript implementation stays until parity; it is the oracle.
- The playtest deploy: on every push to main, `deploy-sftp.yml` builds
  `npm run build:web` (Node 24) and mirrors `dist/web/` over SFTP to
  `bearcavinteractive.com/playtest/dual-deckbuilder/` (no hyphen between deck
  and builder); PRs labelled `playtest` deploy to
  `.../playtest/dual-deckbuilder/<branch>/`. That TypeScript build stays the
  playtest URL until parity; the C++ build is playtested natively.

Its inputs are plain JSON (`SimConfigInput`: seed default 1, runs 150, crews,
archetypes, routes, playerAI and enemyAI default 'aggressive', turns 25,
raiders, encounters, tier 1..5, stage 0..6, maps 30, bands) and its outputs
plain JSON (FightRow per cell with Wilson rates at z = 1.6448536269514722;
GenMap per map seed with RouteAudit). The C++ port is held to byte-identical
output on these.

## Worldsim

The source of the code, and the consumer with the most to lose. Its
requirement is one sentence: after each extraction phase it must build and run
identically, consuming salal through FetchContent pinned to a tag, with each
phase its own branch and PR in worldsim.

What stays behind, and so must keep working against the extracted libraries
from the outside:

- The `assets` target (58 files, 14,175 lines) with its colony-sim schema
  (`worldsim/libs/engine/assets/AssetDefinition.h:23-110`), its Lua and sol2
  generators, pugixml, and the XML config under `assets/config/`. pugixml,
  lua and sol2 stay in worldsim's vcpkg.json and out of salal's.
- ECS systems, components, spatial and action; construction, nav, vision;
  engine's world/* (chunk, generation, rendering, camera); the `world`
  library (79 files, 21,460 lines, with `std::jthread` worldgen in
  `worldsim/libs/world/worldgen/pipeline/PlanetGenerator.cpp:168`);
  `geometry` (67 / 18,141); `planet-view` (16 / 1,942, with its own four
  `#version 330 core` shaders); the placeholder `game-systems`.
- The game-flavoured sandbox scenes (Planet, Grass, Tree, Salvage,
  SalvageDialog), which link planet-view, world and assets.
- The TileAtlasBuilder bootstrap and asset-system init, which left
  AppLauncher for the game ([worldsim#296](https://github.com/kcjonson/worldsim/pull/296)).
- Its threads: the `AssetRegistry` load thread, `std::async` in ChunkManager,
  NavigationSystem, AsyncChunkProcessor and PlanetColorizer, the foundation
  TaskPool. Worldsim keeps them, and nothing in the extracted libraries may
  assume one thread.
- Its three GUI apps and their dev-server ports (world-sim 8081, ui-sandbox
  8090, asset-manager 8070), and `apps/asset-cli`, which creates its own GL
  context (`worldsim/apps/asset-cli/Main.cpp:134-149`).

PRNG: `foundation/random/Pcg32.h` and `SplitMix64.h`
(`deriveSeed(worldSeed, streamId)`) are what worldgen is built on, with
"std::mt19937 is banned in worldgen" in the header. They move with foundation
and keep working unchanged; the DDB-compatible `Rng` sits beside them as an
addition (decision A). The `std::mt19937` residue in engine and apps is
worldsim's to clean up or leave.

Its loop is variable-step with a 120 FPS cap, and its scenes implement
`update(float dt)`. The tick split in phase 2 is mechanical, but the
fixed-step accumulator is opt-in for worldsim: it keeps variable-step
behaviour until it chooses otherwise.

Naming: worldsim's code spells namespaces `Foundation` 19 times and
`foundation` 13, `Renderer` 31 and `renderer` 11 (plus 14 `renderer::tess`),
`UI` 89 and `ui` 3; `m_` prefixes sit on 33 of 154 engine headers, 2 of 47 in
ui, 1 of 32 in renderer, 0 in foundation. Code moves without renaming, and is
formatted with the shared clang-format in its own commit (24 of foundation's 38
files needed it). The phase-1 call was not to rename on the way across, so
worldsim keeps its include lines and namespaces unchanged and the case split is
filed as a follow-up (extraction.md).
