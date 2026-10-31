# ui design

Phase 3's design of `ui`, decided rule by rule against dual-deck-builder's
`docs/ui-rendering-spec/`. The spec is the reference, not adopted wholesale:
each rule is revisited on native merits, and anything justified by WebGL, the
browser or sharing code with the TypeScript engine is re-decided or dropped.
This file records where salal departs from the spec and why. Until the review
is finished, a rule this file doesn't mention is the spec's, unreviewed. The
order it lands in is [ui-plan.md](ui-plan.md).

## Ordering

Decided 2026-10-30. Replaces R2.7, R3.5 to R3.8, R3.19, R4.8 and R8.9, drops
R3.6a, and amends R3.28 and R8.21.

What paints on top is answered by two questions: which slot, then where in
that slot's stacking contexts.

- **Stacking contexts, as in CSS but explicit.** A component's `zIndex` orders
  it among everything in its nearest stacking context, not only its siblings,
  so a hovered card with `zIndex` 1 paints over the resource bar beside its
  hand and targets paint over a targeting scrim. Only `isolation` makes a
  stacking context; nothing creates one implicitly (opacity, transform and
  layout never do), which is the part of CSS z-index that hurts. Every root
  is a context of its own: each of the scene's roots, each stack entry, and
  the tooltip and drag tiers. An ancestor's clip applies
  whatever the `zIndex`; escaping a clip is what the overlay stack is for.
- **The overlay stack.** Dialogs, menus, selects, context menus, toasts and
  tooltips are overlays, entries in one stack, ordered by when they opened, latest on top; a toast is
  brought to the front when it arrives. An entry stays logically owned by the
  component that opened it, for lifetime, dismissal and commands, and anything
  opened from an entry lands above it. The overlay service (R8.21) is this
  stack.
- **Anchored and absolute.** An overlay is placed one of two ways. Anchored,
  it opens from an origin in its opener (its box, or a point in it, for a
  context menu at the press), sits beside it, flipped and kept inside the
  viewport (R12.30), and moves with it as it scrolls, moves or turns, under
  its transform. Absolute, it sits at a point in screen coordinates, or over
  the whole viewport for a dialog's scrim, and its opener's position and
  transform play no part. Either way the opener owns it.
- **Fixed tiers above the stack.** Tooltip, then drag, for the two things that
  must win however recently they opened. A screen transition is app's
  (`SceneStack`), not a ui tier.
- **Slots.** The scene is slot 0 and each stack entry is its own slot, so a
  `zIndex` inside one dialog can't paint over another.
- **One key.** Each draw carries one integer, packed from (slot, stacking
  context order, `zIndex`, tree order).
  Painting, hit testing and pad focus navigation all read the same sorted list,
  so the hit winner at a point is always the topmost painted node, and a test
  holds that.
- **Modality per focus context.** Modal input blocking and menu exclusivity
  belong to a focus context, not the screen, so one player's confirm dialog
  doesn't close or block the other's menu.
- **Dismissal and focus.** A menu closes on a press outside it and when its
  player's focus leaves it; cancel closes the innermost menu, dialog or
  non-modal panel around the focus (R9.13, R9.14). Focus inside an overlay as
  it closes goes back to where it was when the overlay opened, and a modal
  dialog holds its player's focus in its subtree with a scope until then. An
  event that bubbles out of an overlay into the control that opened it, a
  click on a menu's padding or an activate the dialog's content didn't take,
  doesn't activate that control.

Renderer side, ui never forwards component z: the per-call key becomes an
integer ui sets from its walk, and world passes keep their own integer keys in
their own domain (R3.23). Transforms and clips are already baked into vertices
when a draw is submitted (`Primitives.cpp` `pushClip`, `PushTransform`), so
painting a subtree later is only a matter of its key, with no deferred walk.
Promotion needs one new operation, a clip push that resets instead of
intersecting.

Why not the spec's nine global bands (`base` to `transition`): fixed tiers
can't say "above whatever opened me". A confirm dialog opened from a context
menu would land under the menu, which is the web's typeahead-behind-its-modal
bug ([ng-bootstrap#2505](https://github.com/ng-bootstrap/ng-bootstrap/issues/2505)),
Unity's dropdown at sorting order 30000 and Godot's reserved canvas layer
1024. Global raises also leak across windows
([egui#4128](https://github.com/emilk/egui/issues/4128),
[egui#7451](https://github.com/emilk/egui/issues/7451)), and R3.6a's two
patches exist only because the ladder puts `popup` above `modal`.

Why not open order alone, the web's top layer: things that must win
regardless of timing can't, and the web is adding tiers back onto it
([whatwg/html#10370](https://github.com/whatwg/html/issues/10370),
`popover=hint`).

Why one list: the most frequent layering complaint in Unity and Godot is
clicks that don't match what's drawn, and every case found is a second
ordering key read by only one pass (Godot's `z_index` draws but doesn't
reorder input, [godot#85305](https://github.com/godotengine/godot/issues/85305);
Unity's raycaster re-sorts on its own; UIKit's `zPosition`; Dear ImGui's
display layer). egui hit-tests the list it paints and has no such complaints,
but its focus navigation leaked behind a menu
([egui#8327](https://github.com/emilk/egui/issues/8327)), so focus reads the
list too.

## Node model

Decided 2026-10-30. Takes R8.1 and R8.6 as written.

Every node, leaf or widget, implements one interface: render, event handling,
hit test, measure and assign-size, bounds, and the common properties.
`render()` draws the node's own content only and the framework visits the
children; no component renders its children itself, and nothing drawable
lives outside the tree. Containers hold children; a leaf is a full node, so a
Text can be clickable or an Image a drop target without a wrapper class.

Why: the ordering model's one sorted list needs one walk that reaches every
node, and anything outside it gets no key, no hit entry and no focus stop.
Worldsim shows both failures: shapes that can't be interactive without a
wrapper, seven design-system classes (Panel, Badge, Avatar, KeyCap, Divider,
Stat, SegmentedControl) that can't be laid out, linted or clicked, and Button,
Select and ToastStack rendering their icon, menu and toasts outside the walk.
Godot's `Control`, Unity UI Toolkit's `VisualElement`, Qt Quick's `QQuickItem`
and the DOM all use one node type. The base class is `ui::Component`, and
child storage is the scene's slot map (Component API, below).

## Texture residency

Decided 2026-10-30. Takes R5.20's mechanism and R6.4's goal; replaces the
renderer's split at every atlas change.

Each flush binds up to eight textures to units 0 to 7, and each draw carries a
slot index (an integer byte of the instance, under per-draw data below). The
fragment shader picks
its texture with a `switch` on constant indices (GLSL 3.30, like GLSL ES 3.00,
indexes a sampler array only with a constant) and samples with
`textureLod(..., 0.0)`, so no implicit derivative is taken inside a branch;
derivatives the text mode needs are computed before the switch. A run splits
only when a flush needs a ninth texture.

Why: a list row with an icon, a body label and a mono value is three draws
today, and the ordering model's sort interleaves slots and bands, which only
adds splits. The switch asks nothing of a game's atlases (any size, any
format; fonts, icons and images share it) and maps onto descriptor arrays in a
Vulkan backend. A `TEXTURE_2D_ARRAY` of equal-size pages was rejected: it
would make every game's font atlases share page size and format, and RGB MSDF
pages can't share an array with RGBA images, so images would still split.

## Per-draw data

Decided 2026-10-30. Takes R5.3 and R5.4's packing, R5.7a's per-corner radii
and R5.2's explicit flat mode; leaves room for R4.14's rounded clip, decided
separately.

Every batched draw is one instance per quad, drawn with
`glDrawArraysInstanced`, six vertices an instance built from `gl_VertexID`,
no index buffer. Dual-deck-builder's 104-byte layout is the starting point:

| field | type | bytes |
|---|---|---|
| the four corners, transformed on the CPU at submission | f32 x8 | 32 |
| geometry: half size and the quad's extent in local units, or the uv rect for text and images | f32 x4 | 16 |
| clip rect in logical px | f32 x4 | 16 |
| corner radii (the atlas range for text) | f16 x4 | 8 |
| premultiplied fill at each corner | rgba8 x4 | 16 |
| premultiplied border | rgba8 | 4 |
| border width, border outset, shadow sigma or screen distance range, opacity | f16 x4 | 8 |
| mode, texture slot, flags, rounded-clip entry | u8 x4 | 4 |

Lines, glyphs, images and shadows are quads. Polygon triangles and feather
rings go in as quads whose fourth corner repeats the third, so there is one
path and one format. The transform stays baked on the CPU, as the ordering
model assumes. Flat is a mode value. Instanced meshes (`InstancedMeshVertex`)
are a separate path and don't change. The vertex hooks run only on that path
and keep their points and arguments; what changes is the batched attributes a
hook can also see (1 and 3 to 5), which the instance's replace. A rect's fill
hook (shader-hooks.md, "Fill hooks", built 2026-10-31) adds no field: its index
is the texture-slot byte and its params corners 1 to 3 of the fill.

Why: the per-vertex layout writes everything about a rect (colour, border,
radius, clip, mode) into each of its four 80-byte vertices plus six indices,
344 bytes a rect against 104. On desktop the bus doesn't notice either, but
the batcher writes every byte on the CPU, which is where UI frame time goes,
and the float layout had run out of room: border alpha, border position, the
mode and the texture slot were all heading into fractions of one float. A
repacked per-vertex layout (about 296 bytes a rect) was the cheaper rewrite
and still copied the per-rect data four times. Zed's GPUI and Firefox's
WebRender draw UI the same way.

## Theme

Decided 2026-10-30. Replaces R11.1 to R11.3, R11.5 to R11.10 and R11.12's
values with the W3C Design Tokens format (2025.10); takes R11.12's state-layer
order; keeps salal's game-defined font families over R2.18 and R11.8.

A theme is design tokens in the
[Design Tokens Format Module 2025.10](https://www.designtokens.org/tr/2025.10/format/).

- **ui ships a default `.tokens.json`.** It is the contract: every token path a
  ui component reads, in three tiers. Component tokens (`button.background`)
  alias semantic tokens (`{color.surface.raised}`), which alias base tokens
  with plain default values, so a game draws without a theme of its own.
- **A game's tokens file overrides any tier.** Aliases are the mapping: a game
  keeps its own base names and points ui's semantic tokens at them, or
  overrides a component token directly. There is no separate mapping format.
- **Modes through the Resolver module.** Light and dark, per-screen themes and
  high contrast are contexts of a resolver file.
- **Only the root has a theme.** A scene's root carries the resolved theme.
  Components never name, carry, set or switch one: their type's defaults and
  the theme styles code extends (`ui::styles::pane`) come from it silently. A
  component that needs to look different (a player's colour, a one-off panel)
  extends a style with its own fields (Component API, below). Per-screen
  themes are each scene's root theme.
- **Loaded at runtime** into a typed `Theme` in the mount context. An unknown
  path is a warning, a missing one falls back to ui's default, and a wrong
  `$type` is an error; a test loads each game's theme and fails on errors, and
  the dev server can reload a tokens file live.

Types map onto the renderer: `color` as sRGB components and alpha, blended as
is (R5.24), with another colour space taking its `hex` fallback or failing
the load; `dimension` in px (rem times a root size); `duration` in ms;
`cubicBezier` for the animator; `fontFamily` and `typography` resolving to the
game's `FontFamily` ids by name; `shadow` and `border` to the renderer's
styles.

A text transform (CSS's `text-transform`) has no type in the format, so it is
salal's extension, in the place the format leaves for one: a style's group
carries it under `salal` in `$extensions`, and a file that sets it stays valid
for any other tool.

```json
{
  "styles": {
    "button": {
      "$extensions": { "salal": { "textTransform": "uppercase" } }
    }
  }
}
```

The values are `Foundation::TextTransform`'s, `none` and `uppercase`, and
`none` takes off one a lower layer set. It is read on the style's own group,
after `$extends`, and nothing inherits it down the groups; a later file's
`$extensions` on a group replaces an earlier one's whole, as the merge does
with every `$` property. An unknown value, anything else under `salal`, or the
extension on a token is an error like a wrong `$type`, failing
`salal_tokens()`'s build step and the load; on a group that isn't a style it
is a warning. A control that draws its label in its own style (Button and
DropdownButton, a tab, a segment, Select) passes the transform on with the
colour, face, size and tracking, and a transform set in code wins over the
theme's, as any field does. A label drawn in a text style (a list row's, a
checkbox's, a menu item's) takes that style's. Badge, KeyCap, Divider, Stat's
and ProgressBar's labels and Dialog's kicker set `uppercase` in code, so a
theme doesn't change their case.

What ui's tokens define, beyond palette roles (surfaces, text, lines, scrim,
focus ring), spacing, radii and motion: font roles (display, body, mono), each
naming a game family; a semantic tone set (default, accent, info, positive,
warning, danger, plus `Auto`, picked by value); and `size` (sm, md, lg)
setting font size and control height. State layers apply in R11.12's order
(base, hover, pressed, open and active, disabled, focus ring), their washes
coming from tokens. Worldsim's design language (elevation by tint, its line
alphas, two accents, its radius scale and control heights, Panel's kicker and
bracket ticks) is worldsim's tokens and components, not engine rules.

Why: a standard format any design tool can export, with aliases doing the
token-to-use mapping a theme needs, so a tokens file is the theme. A runtime
load gives per-screen and per-player themes and live tuning, which a
compile-time header can't. Salal ships a loader, not a generator; worldsim's
CSS-derived pipeline emits this format instead of a C++ header.

## Icons

Decided 2026-10-30. Replaces R12.6's icon-font atlas.

Icons are SVG and only SVG; there is no raster icon support. A game copies the
raw SVGs it needs into an `icons/` folder, a build step converts everything in
that folder into the engine's form, and code names an icon by its filename
(`icons/home.svg` is `Icon{.name = "home"}`).

Every icon is built the way Material Symbols are, and the build fails on a
file that isn't, naming the file and the offending element:

- a square viewBox
- filled `path`, `rect`, `circle` and `polygon` only, with no strokes (outline
  them before copying)
- one colour, tinted at runtime; both fill rules honoured; transforms
  flattened by the tool
- no gradients, masks, clip paths, filters, text, `image` or external
  references

Material Symbols are the model the rules are written against, not a source:
nothing that needs attribution lands in salal. The pipeline is developed and
tested against Kenney's CC0 game icon packs (filled, with SVG sources), which
need none. System UIcons (Unlicense) was passed over because it is drawn with
strokes, and Health Icons because its licence is published two ways.

The build step produces an MSDF atlas. `salal_icons(target icons/)` runs
msdfgen over the folder at build time, a host dependency like glslang's
validator, and writes an atlas PNG, its msdf-atlas-gen JSON and a name table
from filename to private-use codepoint. ui's icon service owns the atlas and
builds glyph quads from its JSON, drawn through `drawGlyphs` as text is (a
quad carries its atlas and range), so an `Icon` is one tinted text-mode quad in
the same resident set as text and `FontRenderer`'s families stay the game's.

Why MSDF: one instance an icon, three texture samples and a median a
fragment. Tessellated meshes cost 100 to 300 instances an icon with the
feather ring and go soft at 16 px; curve data evaluated in the shader is one
quad too but loops over every segment of the icon per fragment. The atlas
costs texture memory (about 3 MB for 250 icons at 48 px cells) and one of the
eight texture slots; features thinner than the field's resolution soften and
corners round at extreme magnification, neither of which happens at icon
sizes.

## Testing and gates

Decided 2026-10-30. Replaces R13.37's golden images and R14.4, R14.5 and
R14.7's baselines; reshapes R14.8. The strategy is
[testing/README.md](testing/README.md).

No golden images and no pixel comparisons in CI. The gate is tests and a
manual regression pass from the happy-path scripts by a cheap subagent fleet;
the layout lint is a developer helper, not a gate (below). Salal's sandbox scenes are its rendering test pages: when
drawing or rendering code changes, the affected scenes are diffed against the
same scenes rendered from `main`, locally, before committing, with nothing
committed as a baseline. Performance is strict and the engine's own: perf
scenes and a capture runner, run against `main` for changes to batching, the
shader, ui's walk or app's frame, so a game can tell an engine regression
from its own. The renderer's GL tests keep asserting computed values read
back from a `RenderTarget`.

Why: every game's visuals are prototype, so a stored image only ever reports
an intentional change; a pass with judgement finds the real regressions, and
the sandbox scenes are the one place an unexpected pixel is one. Rendering
performance dropping is the regression the games have actually hit.

## GPU resource ownership

Decided 2026-10-30. Replaces R5.30's reference counts.

The renderer's resources have explicit owners: a move-only owner frees its
texture when it goes away, the way `InstancedMeshHandle` already works, and a
copyable generational `TextureHandle` is what draws carry, so a stale handle is
caught rather than drawing garbage. `FontRenderer` owns its atlases, ui's icon
service owns the icon atlas, and a game owns its images.

Why: every engine's GPU layer is explicit (bgfx, sokol, Godot's
RenderingServer, Vulkan and D3D12 renderers); reference counting belongs to an
asset layer above it, where content is shared and streamed (Godot's
`Ref<Texture2D>`, Bevy's strong handles, Unity's Addressables). Salal has no
asset layer yet; whether one counts references is decided when
dual-deck-builder's card art needs streaming (R5.31). Explicit owners are also
the shape Rust's ownership gives by default.

## Layout lint

Decided 2026-10-30. Reframes R13.29; drops R13.25's rule 7 (target size).

The lint and the tree snapshot are helpers the engine ships for developers
building game UI; salal itself has no UI beyond its sandbox test scenes. They
run on demand through the dev server (`/api/ui/lint`, `/api/ui/tree`), which
agents use to inspect and check their work while building a game's screens,
and never in CI. The lint is one walk over rectangles layout already computed,
with a pairwise sibling-overlap check, so a call costs well under a
millisecond on a typical screen.

The rules are R13.25's first six: `sibling-overlap`, `child-outside-parent`,
`outside-viewport`, `zero-or-negative-size`, `text-overflow` and
`unreachable-interactive`. Target size is dropped: pad-first menus move focus
in steps and a dense mouse HUD is small on purpose, so a minimum hit-target
size is a design call per screen, not a lint.

Decoration is exempt from the first two and from `zero-or-negative-size`
(decided 2026-10-30). A component whose own `pointerEvents` is None, R8.29's
decorative overlay, may overlap its siblings, leave its parent's box and be
empty (a fill at 0). Examples: a caret's strokes, a checkbox tick, the
hairline under a tab bar, a list row's selection bar, a slider's thumb, a
progress fill under its label, a focus ring. R13.25.1 exempts only a
differing zIndex or layer, and R3.18 rules out zIndex inside a composite
widget, so without this every stock widget that layers decoration over its
content would fail, and a stock component has to lint at 0 in any game. The
pointer can't reach a decoration, so a None that hides a real overlap can't
also hide a blocked control. Its children are judged by their own value, so a
tooltip's content (the tooltip is None) is still checked.

## Services and input routing

Decided 2026-10-30. Takes R1.6 and R8.14, scoped per scene.

Every ui service (focus, the overlay stack, the theme, input dispatch,
viewport, clock and animator, placement, clipboard) reaches a component
through the context of the scene it was mounted in. There are no singletons,
and constructors touch no service. The draw API stays process-wide.

- **One context per scene,** shared by all of its roots.
- **Only the top scene gets input.** app already does this: covered scenes
  render but receive no events (app-api.md). When a scene is covered, its ui
  gets a cancel for anything mid-interaction (a held press, a drag, a hover)
  and keeps its focused node, so focus comes back when it is uncovered.
- **One focus per player.** Inside a scene, each player has one focused node,
  and a player's devices route to that player's focus. Keys go to the focused
  node and bubble up; the pointer goes to the topmost hit in the ordering
  model's one list.
- **Modals make content below them inert,** for the player who opened them:
  no new events, focus trapped in the modal, and a cancel for anything
  mid-interaction. Inert content still updates and renders.
- **A subtree can keep focus out.** `descendantsFocusable` false on a node
  means nothing under it takes focus, by a press, Tab, a direction or code,
  while the overlays opened from inside it are their own and take focus as
  ever. A game's HUD sets it on its root: it's driven by the pointer and
  hotkeys, so a click leaves no focus behind for Space or Enter to press
  again, and a dialog that closes finds no HUD control to hand focus to. Its
  hotkeys then read only the keys the ui didn't take, Godot's
  `_unhandled_input`. Engines that let a click focus a HUD button all carry
  this bug and the same per-widget fix (Unreal's `IsFocusable`, Godot's
  `focus_mode`, Unity deselecting after a click); one flag on the HUD's root
  covers every control in it, including ones added later.

Why: one focus owner per user is the pattern everywhere (the web's
`activeElement` with `inert` under a modal, Godot's focus owner per viewport,
Unity's one selected object, Unreal Slate's focus per local user for
split-screen). A context per scene lets a pause scene have its own focus,
lets two players shop at once, and lets tests build fresh services.
Worldsim's `FocusManager::Get()` singleton, registered from constructors,
allowed none of that and threw when built too early.

## Component API

Decided 2026-10-30. Names the node model's one base class, settles the
storage model (open-decisions F), and takes R8.2, R8.5, R8.10 and R12.20 in
the forms below. Worked through in a scratch doc with code; this is the
record.

```cpp
class HealthBar : public ui::Component {
  public:
	struct Args {
		UI_COMMON_ARGS
		float value = 1.0F;
	};

	void setValue(float newValue) {
		value = std::clamp(newValue, 0.0F, 1.0F);
		if (auto bar = fill.get()) {
			bar->setSize({size.x * value, size.y});   // setters mark layout dirty
		}
	}

  protected:
	void onMount() override {
		track = addChild<ui::Rectangle>({.size = size, .style = ui::styles::pane});
		fill = addChild<ui::Rectangle>({.size = {size.x * value, size.y}, .style = styles::healthBar::fill, .zIndex = 1});
	}

  private:
	ui::Ref<ui::Rectangle> track;
	ui::Ref<ui::Rectangle> fill;
};

// in a scene
row = root().addChild<ui::Component>({.layout = {.display = ui::Display::Flex, .flexDirection = ui::FlexDirection::Row, .gap = 8.0F}});
health = row.addChild<HealthBar>({.value = 1.0F});
row.addChild<ui::Rectangle>({.size = {100.0F, 16.0F}, .style = ui::styles::pane.extend({.backgroundColor = player.color})});

if (auto bar = health.get()) {
	bar->setValue(player.hp / player.maxHp);
}
root().removeChild(row);   // health.get() is null from here on
```

- **One base class, `ui::Component`,** for leaves and widgets alike, with
  `addChild<T>(args)`, `removeChild(ref)` and `parent()`. Children are added
  in `onMount()`, never the constructor. Implicit `this`, as in the rest of
  the codebase.
- **`ui::Ref<T>`** is how anything holds a component past one statement: an
  id, cheap to copy. `get()` returns `T*` for the current statement, or null
  once the component is gone. `Ref` forwards `addChild` so a held ref reads
  as a relationship (`row.addChild(...)`).
- **Storage is a slot map the scene owns.** Each component is allocated once
  and filed in a slot; a `Ref` is a 64-bit (index, generation) pair. A lookup
  is an index and a compare, no hashing. Parent and children are stored as
  ids, and so are focus, capture, hover, overlay-stack entries and timers.
  `removeChild` unlinks at once and frees after the current dispatch through
  one path, which bumps the slot's generation and clears every reference to
  the component (focus falls back, capture gets a cancel, timers drop). Game
  code never names the slot map.
- **Common attributes**, on every component's `Args` through
  `UI_COMMON_ARGS` (C++20 designated initializers can't name a base struct's
  fields, and P2287, which would allow it, is aimed at C++29):
  - identity: `id`, `visible` (state, not style: false takes the subtree out
    of layout, drawing, input and focus), `enabled`
  - geometry: `position`, `size` (px, percent or auto), `minSize`, `maxSize`,
    `margin`, `transform` (render and hit test only)
  - layout: `layout`, with CSS names as taffy uses them: `display` (Free,
    Flex, Grid, None), `flexDirection`, `gap`, `padding`, `alignItems`,
    `justifyContent`, and as a child `flexGrow`, `flexShrink`, `alignSelf`,
    `position` (Relative or Absolute) and `inset`. `display: Free`, the
    default, places each child at its own top-level `position`, its auto size
    hugging their boxes; Flex (and Grid) flow theirs, and `position:
    Absolute` takes a child out of a flowing parent to its `position` or
    `inset`. The default isn't CSS's Block: in normal flow a child's
    `position` means nothing and children stack, so every hand-placed screen
    and every overlay inside a component (a bar's fill over its track) would
    need `Absolute` on each child. Block keeps its CSS meaning and comes when
    a game asks, since a Flex column already stacks
  - look: `style`, `overflow` (Visible, Clip or Scroll; scrolling is a
    property of any component, not a separate container)
  - ordering: `zIndex` and `isolation` (Ordering, above)
  - input: `pointerEvents`, `focusable`, `descendantsFocusable`, `tabIndex`,
    `cursor`, and the callbacks (`onClick`, `onPointerDown`/`Up`/`Move`/`Enter`/`Leave`,
    `onWheel`, `onKeyDown`/`Up`, `onFocus`, `onBlur`, `onLayout`), whose
    storage is allocated the first time one is set
- **`style` is look only**, with CSS-like names (`backgroundColor`,
  `borderColor`, `borderWidth`, `borderRadius`, `opacity`, and the text
  properties). Geometry and layout are never style, so the theme can change
  how things look but never where they are. A component never names the
  theme: it takes a style value, either a theme style (`ui::styles::pane`, a
  typed constant generated from the tokens files by `salal_tokens()`, so a
  typo is a compile error) or one extended from it,
  `ui::styles::pane.extend({.backgroundColor = player.color})`, the way JS
  spreads `{...pane, backgroundColor}`. Unset fields fall back to the
  component type's defaults.
- **The layout engine is worldsim's `LayoutContainer`** (1,712 lines, 59
  tests), reworked to the CSS names: its hug is `auto`, fill with a weight is
  `flexGrow`, distribution is `justifyContent`, cross align is `alignItems`.

Why the slot map over a parent-owned tree: worldsim's three ownership fixes
(a pointer taken before arena placement, a focus registration gone stale on a
move, handles remapped by an in-place sort) are all addresses or positions
changing under a reference, which ids end. Rust UI frameworks moved to the
same shape (Masonry, Bevy, taffy) because parent-owned children made focus,
capture and targeted events route through the hierarchy, and a Rust port
would need it anyway. No benchmark shows tree storage mattering at UI node
counts; hashed id lookups did, so ids are an index and a generation.

### Declarations

```cpp
namespace ui {

	class Component {
	  public:
		virtual ~Component() = default;

		template <typename T>
		Ref<T> addChild(const typename T::Args& args);   // builds T under this component, mounts it
		void		   removeChild(Ref<Component> child);  // unlinks now, frees after this dispatch
		Ref<Component> parent() const;

	  protected:
		virtual void onMount() {}   // add children here, never in the constructor
		virtual void render() {}    // own draws only; the framework walks the children

	  private:
		ComponentId self;      // (index, gen) in the scene's slot map
		Registry*   registry;  // set when mounted
	};

	template <typename T>
	class Ref {
	  public:
		T* get() const;   // null if the component is gone

		template <typename C>
		Ref<C> addChild(const typename C::Args& args) const;   // forwards to Component::addChild

	  private:
		ComponentId id;
		Registry*   registry;
	};

	struct Style {   // look only; geometry and layout are the component's own properties
		std::optional<Color> backgroundColor;
		std::optional<Color> borderColor;
		std::optional<float> borderWidth;
		std::optional<float> borderRadius;
		std::optional<float> opacity;
		// ...the text properties

		Style extend(const Style& overrides) const;   // a copy with the overrides' set fields on top
	};

}
```

### The slot map

```cpp
struct ComponentId {
	uint32_t index;
	uint32_t gen;
};

Component* Registry::find(ComponentId id) {
	if (id.index >= slots.size()) return nullptr;
	Slot& slot = slots[id.index];
	return slot.gen == id.gen ? slot.component.get() : nullptr;
}

void Registry::free(ComponentId id) {      // after the dispatch that removed it
	Slot& slot = slots[id.index];
	slot.component.reset();
	slot.gen += 1;                         // every old id for this slot is now stale
	freeList.push_back(id.index);
}
```

The slot array may grow, but components never move, because each is
allocated once; nothing keeps a pointer into the array, only ids.

### Theme styles

A game's tokens file defines its styles under the top-level `styles` group,
with the same names `Style` uses:

```json
{
  "styles": {
    "pane": {
      "backgroundColor": { "$type": "color", "$value": "{color.surface.raised}" },
      "borderColor": { "$type": "color", "$value": "{color.line.edge}" },
      "borderWidth": { "$type": "dimension", "$value": { "value": 1, "unit": "px" } },
      "borderRadius": { "$type": "dimension", "$value": "{radius.md}" }
    }
  }
}
```

A style's text transform, which the format has no type for, is salal's
extension on its group (Theme, above).

`salal_tokens(my_game tokens/)` generates a typed constant per style at build
time, an index into the loaded theme, so code names `ui::styles::pane` and a
typo is a compile error. The build loads the folder in every mode its resolver
has and fails on a token some modes lack. A style a game's table names that a
loaded file lacks fails the load in a dev build, naming the style and the file;
a release build warns and the style's fields fall back to the type's defaults.

### In Rust

A port maps across directly except for one thing: Rust needs a context
parameter to add children, because the component is already borrowed from the
slot map inside `on_mount`. Each `addChild(...)` becomes `cx.add_child(...)`,
`Ref` becomes a `Copy` id, and `get()` becomes an `Option`.

```rust
impl Component for HealthBar {
    fn on_mount(&mut self, cx: &mut MountCx) {
        self.track = cx.add_child(Rectangle { size: self.size, style: ui::styles::PANE, ..Default::default() });
    }
}
```

## Remaining rules

Decided 2026-10-30, closing the review of the spec.

- **No raster images.** None of the games has raster art (dual-deck-builder
  has one UI art file besides its font atlases), so there is no Image
  component (R12.5), no nine-slice (R2.12, R5.19) and no art residency,
  decoding or context-loss handling (R5.31 to R5.33). Textures are MSDF
  atlases (fonts, icons) and render targets; `drawImage` stays only to
  composite a render target. Surface texture that would have been raster art
  is a game's fill hook instead, computed in the base shader per rect ([shader-hooks.md](shader-hooks.md), "Fill hooks",
  2026-10-31), and the rest of a game's art is vector through
  `renderer/vector`.
- **Rounded clip (R4.14):** deferred until a game needs one. Until then,
  inset content by the radius. The instance format already has a byte for a
  rounded-clip entry.
- **Small text (R6.4a):** no raster fallback. Loading an atlas whose distance
  range is under a sixth of the em warns, and text stays at 11 px and up.
- **UI scale (R7.5):** one scale for the whole UI, applied by redefining
  app's logical viewport, which also covers asteroids' text-scale setting.
- **Ink extent (R8.8):** taken. A shadow or glow drawn past a component's
  box is ink, not overflow, so the lint doesn't flag it.
- **Snapshot fields (R13.11):** the tree snapshot's field names are the same
  in every game, so one agent toolkit drives them all, and a missing value is
  null, never zero.
- **Devices to players:** a device belongs to a player through app's existing
  slot claiming, and each player's devices drive that player's focus
  (Services and input routing, above).
- **Activate and cancel (R9.27):** ui reads activate, cancel and the
  navigation directions as actions from foundation's `ActionMap`, rebindable
  per device, never from hard-coded keys.
- **Names:** each of ui's dialogs, menus, selects, context menus, toasts and
  tooltips is an overlay, an entry in the overlay stack, which the overlay
  service runs; the spec's "popup" is not used. app's layers above every scene
  (dev menus, debug HUDs) are `app::DebugLayer`, so the two don't collide.
- **Screen transitions (R8.22, R12.38):** app's `SceneStack` owns them; ui
  mounts a scene's roots in `onEnter` and unmounts them in `onExit`.
- **The lint's required rules** are R13.25's first six; the spec's chapter 13
  checklist count (four) and rule 7 don't apply.
- **Debug drawing (R13.45):** world debug lines draw in a world domain below
  the UI; the diagnostic domain (app's overlays) draws after the UI, as R3.21
  says.
