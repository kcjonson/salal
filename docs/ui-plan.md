# ui plan

The order phase 3 lands in: the salal PRs that build `ui` to
[ui-design.md](ui-design.md), the renderer changes under it, and the worldsim
PRs that convert onto it. Written 2026-10-30 from salal main `9df4d85`
(`v0.19.0`) and worldsim main `a4226e17`. ui-design.md is the record of what
was decided and stays that way. This file is the order and the few decisions
the order needed that the design doesn't make, and it changes as PRs land.

## What exists

| where | what | lines |
|---|---|---|
| worldsim `libs/ui` | the tree (`Component` with a 64 KB arena per component, position handles, `Container`), `LayoutContainer`, `FocusManager`, 25 components, generated `Tokens.h` and `IconGlyphs.h`, the lint and tree serializer, the clipboard seam | 18,725 with tests; `LayoutContainer` 1,712 with 59 tests, `FocusManager` 253 with an 841-line suite |
| worldsim `apps/world-sim/scenes` | game ui (`GameUI`, views, dialogs, components) | about 12,000, plus 2,700 of models and adapters |
| | pre-game scenes (splash to world-creator) and `shared` | about 6,000 |
| worldsim `apps/ui-sandbox`, `apps/asset-manager` | 18 scenes using ui, 16 of them pure ui and two Salvage scenes; five asset-manager files | |
| worldsim `libs/engine/app` | phase 2's bridge: `UiBridge`, `UiScene`, `UiOverlay`, `UiClipboard` | |
| salal `libs/ui` | a placeholder: one translation unit and one test | |

Worldsim's `ui` links salal's `foundation` and `renderer` and nlohmann-json,
nothing else, so nothing in it rests on what ui-design.md takes out. Of the
spec's 32 worldsim gaps (its chapter 16), eleven are already closed in salal
or worldsim and one, golden screenshots, is dropped by ui-design.md. The rest
are placed in the PRs below, except the metrics items (the GPU timer's scope,
zeros for unknown memory and CPU values), which belong with the perf suite.

## Decisions this plan takes

### Built in salal, worldsim converted onto it

`ui` is built in salal to ui-design.md, the way phase 2 built `app`, and
worldsim converts onto it once the catalog is in. Its code is ported, not
copied: `LayoutContainer` and its tests, `FocusManager`'s tab, wrap and scope
mechanics with their suite, the lint and tree serializer, `ScrollContainer`'s
math, and each component's drawing, state machine and tests start from
worldsim's files at a commit the PR names, so blame stays reachable there.

Moving worldsim's ui first and reworking it in salal was rejected:

- The new base class and storage is one change for every component at once:
  `onMount` instead of constructors, `Ref` instead of arena handles, `Args`
  through `UI_COMMON_ARGS`, parent-local positions, look-only style. Salal
  can't carry two component models, so a moved copy would be rewritten whole
  in one later PR anyway, and every worldsim screen would convert twice.
- The copy would bring in what the design takes out (the thread-local z, the
  `FocusManager::Get()` singleton, a `Tokens.h` generated from worldsim's
  prototype) and worldsim's design language (Avatar, Panel's kicker and
  bracket ticks), which would then move back out.
- Phase 1 copied `foundation` and `renderer` because their APIs stood. Phase 2
  rebuilt `app` because its didn't, and ui's target is further from worldsim's
  than app's was.

### Renderer rework first

The integer key and the resource owners land before any ui code, and the
instance format and the texture slots as soon as the perf suite is in, before
the first component is ported and so before worldsim converts anything.

- ui's walk sets the integer key from its first PR.
- The owners depend on nothing, and the icon service needs them.
- The format and the slots are the largest renderer changes. Measured against
  `main` while only the renderer moves, over the perf scenes and worldsim's
  game, a regression can only be the renderer's. ui's catalog is then built
  and measured on the format it will keep.
- Until the slots, a list row with an icon, a body label and a mono value is
  three draws, and the ordering model's sort only adds splits.

The ui core (PRs 5 to 12) doesn't wait for the format or the slots; the two
tracks share no files, and the core's perf numbers are taken again when the
format lands.

### ui headers under `ui/`

Salal's ui headers are included as `ui/...` (`#include <ui/Component.h>`),
unlike `foundation`, `renderer` and `app`. From PR 7 on app links ui PUBLIC,
and worldsim's `engine` links worldsim's `ui` PUBLIC, so both libraries'
include roots meet in every worldsim target from that pin until worldsim's
`libs/ui` is deleted. Without the prefix, worldsim's paths
(`layout/LayoutContainer.h`, `focus/FocusManager.h`,
`components/button/Button.h`) collide with salal's ports of the same files.
The prefix matches the namespace and keeps a game's own `layout/` or
`components/` folders apart too. One worldsim include already has the shape,
`<ui/input/InputEvent.h>` in `apps/ui-sandbox/scenes/PlanetScene.cpp` through a
`libs/` root, so salal's ui leaves that one path unused.

### app hosts ui

app links ui (the dependency rule allows it) and gives each `Scene` a ui
context, made on first use, holding the scene's services ("Services and input
routing"). App's default dispatch sends events to the overlays, then the top
scene's ui, then the scene, and app registers the tree and lint providers from
the top scene's context, so a game gets both with no code.

### Build steps are C++ host tools

`salal_tokens()` and `salal_icons()` run tools built in-tree, as
`salal_base_shader()`'s assembler is, so a consumer's build needs no Node.
`salal_icons()`'s tool links msdfgen, a host dependency through vcpkg as
glslang's tools are.

### Where the four named pieces land

Controller-first focus is PR 8, theme tokens with `salal_tokens()` PR 9, icons
with `salal_icons()` PR 10, and animation PR 11. Animation lands before the
catalog so components port onto motion tokens, not onto hard-coded durations
that would be reworked a PR later.

## Sequencing

Each salal PR is a minor tag. Worldsim pins after PRs 1 and 2 with small edits
and after 3 and 4 with none. It takes the tags from 5 to 14 with one CMake line
between them, at PR 7's pin, where it starts linking salal's ui because app
does; it uses salal's ui from its first conversion PR.

Every PR runs unit tests in CI on three OSes, and a ui PR adds its sandbox
scenes and their happy-path lines and runs the relevant scripts before
committing. The gate column names what else applies
([testing/README.md](testing/README.md)): GL readback tests, the scene diff
for drawing changes, and the perf suite against `main` for batching, the
shader, ui's walk or app's frame. "Gap n" is an item of the spec's chapter 16
list.

| PR | salal | worldsim | gate | depends on |
|---|---|---|---|---|
| 1. Sort key and clip fixes | `Primitives`' per-call `zIndex` becomes `key`, a signed 32-bit integer on every draw, `drawGlyphs` and the batch's groups (float today) included; the batch sorts on the sort domain, then the key, as now. A clip rect passes through the current transform before it intersects (gap 5), and the clip test is half-open, so a clip's right and bottom pixel columns are outside it (gap 32) | renames `zIndex` to `key` at its `Primitives` call sites (under 100, world passes and its ui's forwarding) and drops the float casts on text; `ScreenClip` goes from `Container` and `TextInput`, and `Container` pushes its clip before its content offset, since the renderer maps clips now | null-backend tests: integer order with negative keys, a clip under a translate and a scale, the edge columns; a GL readback of the edge columns; scene diff, where only clip edges move; worldsim's clipped lists and text input through the dev server | none |
| 2. GPU resource owners | a move-only `Texture` owner that frees on destruction, as `InstancedMeshHandle` does, over the copyable generational `TextureHandle` draws carry; `RenderTarget` owns its colour texture, `FontRenderer` its atlases; `destroyTexture` goes; a stale handle draws nothing and logs once. `FontRenderer` warns at load on an atlas whose distance range is under a sixth of the em ("Remaining rules") | its two texture and target sites hold owners | null-backend tests: one free, moves, a stale handle; a GL test drawing after an atlas is freed | none |
| 3. Per-draw instance format | the 104-byte instance of "Per-draw data", one a quad, drawn with `glDrawArraysInstanced`, six vertices from `gl_VertexID` and no index buffer; polygon triangles and feather rings as quads repeating a corner; flat as a mode value (gap 7); `BatchVertex` gone; upload bytes and flush reasons in the metrics (R13.13, R13.14). The instanced-mesh and baked paths keep their per-vertex inputs at locations 0, 2, 6 and 7, and the batched instance packs into the rest of GL 3.3's sixteen. Both hook points are on the instanced-mesh path, which this leaves alone, so no hook's inputs change; shader-hooks.md changes only where it describes `base.vert` | none; its grass hook, its instanced meshes and `BakedChunkRenderer` (which feeds locations 0 and 2) are unchanged | perf suite: bytes a rect, batcher CPU time, frame p50 and p99, holding or better; GL readback of every mode and of the f16 and rgba8 packing; scene diff of every sandbox scene, identical to rounding; worldsim's game through the dev server | the perf suite |
| 4. Eight texture slots | up to eight textures bound per flush, the instance's slot byte, a constant-index `switch` in `base.frag` sampling with `textureLod(..., 0.0)`, the text mode's derivatives taken before it; a run splits only for a ninth texture, and the metrics count splits by reason | none | perf suite: draw calls fall on the text scenes; a GL test with nine atlases; scene diff | 3 |
| 5. ui core: tree, storage and order | `ui::Component`; the scene-owned slot map (`ComponentId`, `Ref<T>`); `onMount`, `addChild`, `removeChild` and `parent`; the deferred free that bumps the generation and clears every reference; `UI_COMMON_ARGS` with identity, geometry and ordering; `Style` and `extend`; the ui context with the registry, the viewport and roots sized from it; Rectangle, Circle, Line and Text as full nodes (R8.1, R12.1 to R12.4); parent-local coordinates with `screenBounds` and `localToScreen` (gap 28); the ordering model (stacking contexts, `isolation`, `zIndex`, slots) built into the one sorted list, each node's rank its draw key (gaps 1 and 14); `transform` in drawing; ink extent; the tree snapshot with shared field names and null for unknowns (gap 23); the lint's first four rules, checkable at a given viewport | none | unit tests: the slot map, stale refs, removal inside a dispatch, ui-design.md's ordering cases (a hovered card over the resource bar beside its hand, targets over a scrim, a `zIndex` in one root not crossing into another), keys in paint order through the null backend, the lint; sandbox `ui_order` and `ui_text`, from worldsim's layer, text-shapes and text-wrap scenes; a 10,000-node perf scene (R13.31), the walk's first numbers | 1 |
| 6. Layout | `LayoutContainer` reworked into the core with "Component API"'s CSS names: `display` Block, Flex and None; `flexDirection`, `gap`, `padding`, `alignItems`, `justifyContent`; `flexGrow`, `flexShrink`, `alignSelf`, `position` and `inset` on a child; `margin`, `minSize`, `maxSize`, `auto`. Its own pass between update and render (gap 16); dirtiness up to the relayout boundary, with an assigned size never freezing hug (gap 15); `overflow` Visible and Clip; `onLayout`; text measured with render metrics (R10.14); the lint's text-overflow rule | none | the 59 ported tests, with the spec's two freeze tests (10.9) written first; sandbox `ui_layout` and `ui_clip`, from worldsim's layout and clip scenes; perf suite | 5 |
| 7. Input, and app hosting ui | "app hosts ui", above: app links ui (`SALAL_BUILD_APP` implies `SALAL_BUILD_UI`, as the sandbox implies app), a ui context per scene, the default dispatch, the tree and lint providers; a layout section in app's metrics (R13.7); one UI scale through app's logical viewport (R7.5). ui's dispatch: the pointer to the topmost hit in the one list (gap 3); bubbling (R9.6); hover with enter and leave derived in one place and re-evaluated after layout under a still pointer (gap 12); per-pointer capture and cancel (R9.10, R9.30); click on the nearest common ancestor (R9.31); `pointerEvents` (R8.29); disabled components occluding (R9.5); hit tests on the content box, half-open, through the inverse transform (gap 13); wheel in logical px, latched (R9.3, R9.32); a cancel for anything mid-interaction when the scene is covered. The input callbacks in `UI_COMMON_ARGS`, stored on first set; `overflow: Scroll`, `ScrollContainer` folded in (wheel, thumb drag through capture, paging); the lint's unreachable-interactive rule | drops its `set(SALAL_BUILD_UI OFF)`; its `setDispatch` bridge replaces the default dispatch as it does today, so nothing else changes | dispatcher tests on the headless platform: the hit winner is the topmost painted node in each of PR 5's ordering cases, hover follows content scrolled under a still pointer, capture ends on removal, a covered scene gets its cancel; sandbox `ui_scroll` and `ui_input`; perf suite | 6 |
| 8. Focus, controller first | one focus per player in each scene, each player's devices (app's slot claiming) driving it; keys to the focused node, then bubbling (R9.15, R9.24); activate, cancel and the four directions as `ActionMap` actions (R9.27); directional focus scored by distance over the one list, with explicit neighbours (R9.26); tab order from the tree, positive `tabIndex` first (R9.18), wrapping (R9.19); scopes as subtree roots (gap 11); focus groups (R9.29); input mode and focus-visible (R9.23); fixup after layout (R9.28); scroll into view on focus (R9.22); removal clearing focus with no callbacks (R9.21); a covered scene keeping its focus; keyed `reconcileChildren`, so a rebuilt list keeps pad focus (R8.27). `FocusManager`'s suite ported where it still applies | none | headless tests with two players' snapshots, a split keyboard and a hot-plugged pad; sandbox `ui_focus`, a grid of controls for two players; happy-path lines driving it with pad events through `/api/input` | 7 |
| 9. Theme and tokens | the Design Tokens 2025.10 loader with the Resolver module; `Theme` in the context, on the root only; ui's default `.tokens.json` in three tiers; `salal_tokens(target tokens/)` generating `ui::styles::*`; state layers in R11.12's order, `size` (sm, md, lg), the tone set with `Auto`, font roles naming the game's `FontFamily` ids; an unknown path warns, a missing one falls back, a wrong `$type` fails the load; live reload through the dev server | none | loader tests: aliases across tiers, a mode, each failure; a build test where a misspelt style fails to compile; a test loading every tokens file in the repo; sandbox `ui_theme`, switching modes | 5 |
| 10. Icons | `salal_icons(target icons/)` with its tool on msdfgen, failing the build on an SVG that breaks "Icons"' rules and naming the file and element; the atlas, its JSON and the name table; an icon service in the context that owns the atlas and loads it as a font family; `Icon` as one tinted text-mode quad; Kenney's CC0 icons as the test set | none | tool tests on good and bad SVGs; a GL readback of a tinted icon; sandbox `ui_icons`, from worldsim's icon scene; perf suite | 2, 5 |
| 11. Animation | the clock and the animator in the context (R8.28): tweens retargeted from their current value, reduced motion completing on the first tick, cancelled with their owner; `requestUpdate` (R8.17); `duration` and `cubicBezier` tokens; state-layer transitions through it (R11.13, gap 18); timers as ids, dropped on removal | none | headless tests on a manual clock: curve values, retargeting, reduced motion, an owner removed mid-tween; sandbox `ui_motion` | 9 |
| 12. Popup stack, placement and modality | the popup service: entries in open order, each its own slot and stacking context, owned by its opener, anything opened from an entry landing above it, a toast brought to the front on arrival; the tooltip tier above the stack, and the drag tier's slot without its service; modality and popup exclusivity per focus context, content below a modal inert for that player (gap 11); dismissal on an outside press, cancel and focus loss (R9.13, R9.14); the placement service (R12.30); entries walked under their opener at their own slot's key, through a new `Primitives` clip push that resets instead of intersecting (gap 2) | none | headless tests: a dialog opened from a menu lands above it, a tooltip above both, two players' modals stay apart, hit testing and focus follow the stack; sandbox `ui_popups`; a popups-open perf scene | 8, 11 |
| 13. Controls | ported onto the core: Button and ListRow (the icon a child, gap 30; `size` setting the font, gap 19), Slider, ProgressBar, TextInput (app's clipboard through the context), TabBar, SegmentedControl (interactive now), TreeView, Badge, Stat, KeyCap, Divider, and Panel's structure, a title and an actions slot (gap 17); new: Checkbox and Toggle (R12.9), RadioGroup (R12.35). Each with its worldsim suite. The sandbox's picker rebuilt on ListRow and its `Primitives` picker deleted | none | the ported suites through the dispatcher; sandbox scenes from worldsim's button, tab bar, text input and tree view scenes; happy-path lines for each control; scene diff; perf suite | 4, 8, 9, 10, 11 |
| 14. Popups | Menu, Select (its caret an icon, gap 19), DropdownButton, ContextMenu, Dialog (its focus scope its subtree), Tooltip (any component as content, measured, gap 19), and Toast, ported onto the popup stack with their suites; `TooltipManager` becomes the tooltip tier and `ToastStack` the stack's toast entries (gap 30) | none | the ported suites; sandbox scenes from worldsim's context menu, dialog, dropdown, toast and tooltip scenes; happy-path lines; perf suite | 12, 13 |

Order and overlap: 5 to 8 go in order; 9 and 10 run beside 6 to 8; 3 and 4 run
beside 5 to 8 once the perf suite is in; 11 follows 9, 12 follows 8 and 11, 13
needs 4, and 14 closes the salal side. Wrap layout, Popover, NumberInput, the
standalone Scrollbar, Counter, the drag service and the rounded clip are not in
the sequence (Grid is the question under "Open"). None of worldsim's
screens or asteroids' screen designs uses one; each comes when a game asks,
dual-deck-builder's port most likely first.

## Worldsim's conversion

Worldsim converts after PR 14, in two PRs, each pinning the latest tag. Each
app moves whole, so no app runs both ui libraries; between the two PRs they
share worldsim's build, which the `ui/` prefix allows. Worldsim's only work in
flight is this extraction, whose other worldsim PRs (the renderer pins) land
before, so world-sim's one large PR races nothing on its main.

| PR | worldsim | gate |
|---|---|---|
| A. Theme, icons, ui-sandbox, asset-manager | `gen-cpp-theme.mjs` also emits a Design Tokens file for `salal_tokens()`; the 38 stroked glyphs of `IconGlyphs.h` outlined into filled SVGs, with `assets/ui/icons`' ten, in an `icons/` folder for `salal_icons()`; worldsim's own components on salal's ui (Avatar, Panel's kicker and bracket ticks); ui-sandbox's 16 pure-ui scenes deleted, since salal's sandbox has them, and its two Salvage scenes, its navigation menu and asset-manager converted | ctest; each converted scene through the dev server (tree, lint at 0, screenshot) against the pin before; asset-manager by hand |
| B. World-sim | the pre-game scenes (splash, landing, menu, settings, scenario-select, party-select, loading, world-creator, `scenes/shared`) and the game scene (`GameUI`, its views, dialogs and components); then `libs/ui`, `UiBridge`, `UiScene`, `UiOverlay`, `UiClipboard`, `Tokens.h` and `IconGlyphs.h` deleted | the happy-path flows from launch to a started game, pad and mouse; scripted `/api/input` runs before and after: a dialog from a menu, a tooltip over it, Tab and pad focus kept inside a dialog, a list in the game ui scrolled without zooming, a toast over a dialog; tree and lint; screenshots judged by a subagent for intended differences only |

What changes where a player or worldsim's tooling can see, known now; each
conversion PR owns its part in its description:

- Tooltips paint over menus, a dialog opened from a menu lands above it, and a
  toast comes to the front when it arrives, instead of literal z values.
- Tab stays inside an open dialog, and tab order follows the tree, not
  registration order.
- A pad and the arrow keys move focus between controls; Enter, Space and
  Escape arrive as `ActionMap` actions, rebindable.
- A click lands on the topmost painted control in every case, so the scenes'
  hand-ordered dispatch goes.
- Hover follows content moving under a still cursor, with no move synthesised
  each frame.
- Icons are filled MSDF quads at a fixed weight per icon;
  `GearTabView.cpp:266`'s stroke width 3 becomes its own outlined SVG.
- Transitions run on worldsim's duration tokens, unused today.
- `/api/ui/tree` has the shared schema (local and screen bounds, state, text,
  null for unknowns); a script reading `zIndex` or absolute `bounds` changes.
- The select caret is an icon, and tooltip width is measured, not estimated.

## Open

One decision the sequence needs that ui-design.md leaves open:
`display: Grid`. ui-design.md lists it with the taffy names; the spec puts
wrap and grid out of scope, worldsim's `LayoutContainer` has neither, and no
screen in the three games uses one. Recommended: PR 6 lands Block, Flex and
None, and Grid comes with the first game that asks. Changes PR 6 only.
