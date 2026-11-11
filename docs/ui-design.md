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

The drawing added since fits the same 104 bytes, each in fields its mode didn't
read (BatchRenderer.h has the table). A text shadow or halo
(`Primitives::drawGlyphShadow`, built 2026-11-02) is its glyphs' quads again in
the text mode, its spread and blur in the halves a rect keeps its border width
and outset in, which text never read; the shader widens the coverage ramp by
them, so a plain glyph pays a max, a min and a divide a pixel. A circle's shadow
is a rect's shadow quad with the radius at every corner, and a polygon's is its
fill's flat quads again with the feather ring widened to the blur; neither adds
a field or a line of shader. A rect's border on some sides takes four of the
flags byte's five free bits, its bare sides, leaving one; the corners beside a
bare side ride in the radii as 0. A rect whose border is on every side pays an
integer test a pixel for it, and only one with a bare side measures the second
and third rounded boxes its band then lies between. A dash pattern takes no
field and no flag: a rect's border carries its dash, its gap (negative for
round caps) and its offset in the three shape halves a rect left at 0, and a
stroke piece carries them in fill corners 1 and 2, its colour in corner 0's.
`base.frag` measures a border round its centre line and a stroke along its
piece's axis, so a dashed border is still one quad and a dashed line as many as
a solid one. Every rect and stroke pixel pays one float compare for it; only a
dashed one walks the outline (a dashed border, an `atan` on its corners).

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
  Files layer by path, so a game style that takes a name ui's styles already
  use (`styles.tab`) merges into ui's and restyles every tab. That is how a
  game restyles ui's components on purpose, so it isn't an error, but the
  load reports each ui style a game's layer redefines as an info naming the
  fields, and a game's own styles go under a group of their own
  (`styles.hud.tab`, not `styles.tab`).
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
styles. A style's `boxShadow` and `textShadow` each take the first outer layer
of their shadow token; `textShadow` is CSS's with the token's spread kept, which
is a halo's width. A style's `borderStyle`, a `strokeStyle` token, dashes a
Rectangle's border and a Line's stroke: `dashed` is three widths on and three
off, `dotted` round dots a width across and a width apart, and a `dashArray`
draws its first dash and gap (one length is both) with its `lineCap`. `double`,
`groove`, `ridge`, `outset` and `inset`, and a `dashArray` of more than two
lengths, load with a warning and draw solid or with their first two.

A font is one weight, so a role with several (a display face in semibold and
bold) is several families under one name: the game registers each with its
weight (`ThemeArgs::fontFamilies`), and a style's `fontWeight` picks the face,
the nearest by CSS's matching rule ([CSS Fonts 4, 5.2](https://www.w3.org/TR/css-fonts-4/#font-style-matching)),
when the theme resolves the style. So `title` at 600 draws the semibold, a
style extended to 700 the bold, and a control passes its weight to its label
with its family. A family registered under two names picks among the first
one's faces.

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

The sides a box's border is drawn on have no type either (a `border` token is
a colour, a width and a stroke style, all four sides), so they are salal's
extension too, `"borderSides": ["left", "top", "right"]` beside or instead of
`textTransform`, with the same rules: on the style's own group, a later file's
replacing an earlier one's whole, and anything but a list of `top`, `right`,
`bottom` and `left`, each once, an error. It sets `Style::borderSides`, which a
Rectangle (and every control drawn as one) hands the renderer's `BorderSides`.
A side left out has no border, and with a border width the corners beside it
are square, so one `borderRadius` gives a tab round top corners and a square
foot.

What ui's tokens define, beyond palette roles (surfaces, text, lines, scrim,
focus ring), spacing, radii and motion: font roles (display, body, mono), each
naming a game family, its faces picked by weight; a semantic tone set (default, accent, info, positive,
warning, danger, plus `Auto`, picked by value); and `size` (sm, md, lg)
setting font size and control height. State layers apply in R11.12's order
(base, hover, pressed, open and active, focus ring), their washes coming from
tokens. A disabled control draws whole at the disabled opacity, fill, border,
label and icon, so one with a clear fill still reads as disabled. A key or pad
activation has no release to end a press, so it shows the pressed layer for
`state.pressed.hold`, and a press looks the same however it comes. A control's
state (hovered, pressed, active, focused, focus-visible) is readable to its
subclasses, which hear it change as it happens (`Control::onInteractionChanged`),
for a look the layers don't draw, a lift on hover. Worldsim's design language (elevation by tint, its line
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

## The frame

Decided 2026-11-10, for dual-deck-builder's interface scaling. A context's
viewport was the window; it's now the frame, which is the window unless a
frame is set.

- `Context::setFrame(maxSize)` caps the window, in ui px, on each axis and
  centres what's left. `getViewport()` is the frame's size, `getFrame()` its
  rect, and `getCanvas()` the whole window in ui px. With no frame all three
  agree.
- Bounds stay in the window's space: a framed root's box starts at the frame's
  corner, so the tree snapshot, hit testing and injected input need no second
  coordinate system, and the pointer reaches framed content with nothing
  subtracted. The corner sits on a whole logical pixel of the window.
- Every root lays out against the frame except `background()`, a root made on
  first use that covers the window and paints under the others. A scene's art
  that should run to the window's edges goes there.
- Overlays lay out against the frame. An anchored entry flips and clamps inside
  it, an `Absolute` one with a margin is kept inside it, and a cover is the
  frame, so a toast stack's corner is the frame's. `Absolute::coverWindow`
  makes a cover the whole window instead: a Dialog's, so its scrim dims the
  background past the frame, its box centred in both.
- Outside the frame the pointer is over nothing unless the scene made
  `background()`: a press beside the content isn't the ui's, and goes on to
  the scene. `background()` sits under every other root, so a handler on it
  is reached only where no root covers it, which with no frame is nowhere.
  It's for art. An overlay opened from something in it is still placed in
  the frame.
- The lint is held to the window, and a framed root's children to their root
  by the child-outside-parent rule, so content that spills past the frame is a
  violation though it's still on screen.
- app sets it: a scene whose `framed()` is true gets `AppConfig::uiScale`'s
  frame ([app-api.md](app-api.md), "The frame").

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

## Drag and drop

Decided 2026-11-06, for a game's battle screen, which drags cards from a hand
onto targets on a road. Takes R9.12, R9.12b's ghost, R9.12d and R9.12e;
replaces R9.12a's start call and its threshold tokens, R9.12c's `accept()`, and
the bubbling drag events of R9.1 and R8.2; adds a keyboard and pad path, which
the spec doesn't have.

A component becomes a drag source or a drop target by declaring it, as it
declares a tooltip, and the context runs the whole gesture:

```cpp
card->setDragSource({
	.kind = "card",
	.value = cardId,
	.onDragEnd = [](const ui::Drag& drag) { /* drag.dropped, drag.target */ },
});
lane->setDropTarget({
	.accepts = [](const ui::Drag& drag) { return drag.kind == "card" && playable(std::any_cast<CardId>(drag.value)); },
	.onDrop = [this](const ui::Drag& drag) { play(std::any_cast<CardId>(drag.value)); },
});
```

- **Start.** A primary press on a source, or on anything under it, that no
  handler captured stays a candidate click until it travels past 6 px, the
  threshold that already stops a press clicking. Past it the drag starts: the
  press target hears a Cancel, nothing clicks, and the source captures the
  pointer. One number for both means a press is a click or a drag, never
  neither. It isn't a token, since it's behaviour rather than look, and there is
  no touch threshold because app has no touch input. A press consumed below the
  source starts nothing, so a control inside a card that shouldn't drag the
  card consumes its press.
- **The ghost.** The source is its own ghost unless its `ghost` builds one
  under it. Either way the ghost is lifted into the drag tier, the top of the
  ordering model, above the tooltips: it paints in that slot, its ancestors'
  clips stop cutting it, and the pointer never hits it, so a hit test during a
  drag finds what's under the ghost. It moves by a translation in screen space,
  the pointer's travel since the press, over its own transform, so a card turned
  in a fanned hand stays turned while it follows. A lifted source keeps its box
  in layout, so the hand doesn't close up behind it unless the game does that
  in `onDragStart`. At the end the ghost is set down where it started, at once;
  `onDragEnd` gets the last travel, for a game that tweens it home.
- **Targets.** The drag is over the innermost target at or above the hit, and
  that target decides. A refusal isn't passed up, so a card held over a vehicle
  that refuses it doesn't fall through to the road the vehicle sits on.
  Accepting is a predicate, `accepts`, asked whenever the context needs the
  answer (arriving, each move, each key step, the tree), so it should be cheap
  and change nothing; unset, a target takes anything. A disabled target
  refuses. A source and its subtree are never targets of its own drag, and
  nothing under a modal dialog of the drag's player is one.
- **Events.** The source hears `onDragStart` and exactly one `onDragEnd`. A
  target hears `onDragEnter`, `onDragOver` on each move (and once when a key
  drag arrives), and then `onDragLeave`, or `onDrop` in its place, after which
  the source's `onDragEnd` has `dropped` set; a target that goes mid-drag hears
  nothing more. None bubbles: the target was already found by
  walking up, so a bubble would only deliver the same answer twice. A source
  being dragged is `dragging`, and a target with a drag it accepts over it is
  `dropActive` (R11's state); a Control hears both in `onInteractionChanged`.
  Over a target that refuses, the cursor is NotAllowed.
- **Cancel.** The player's cancel action (Escape or a pad's B by default,
  rebindable like every navigation action), a second button pressed during the
  drag (a right click), the window losing focus, the scene being covered, and
  the source going all end the drag not dropped. A pointer drag's press is
  still held after a cancel, and its release clicks nothing.
- **Keys and pads.** Activate on a focused source, or on a component under
  one, picks it up when nothing on the way consumed the activate, so a source
  that's also a button clicks instead, and a game wanting both reads the
  activate itself. The drag then has that player's navigation actions before
  the focused component hears them: a direction moves it to the nearest
  accepting target that way, by the score directional focus uses; next and
  previous step through the accepting targets in tree order; activate drops;
  cancel puts it back. It starts on the accepting target nearest the source,
  or on none when nothing accepts, and an activate then ends it as a release
  over empty space does. Focus stays on the source and targets needn't be
  focusable, since a road lane isn't a Tab stop. The ghost sits centred on its
  target and follows it through layout. A pointer press during a key drag
  cancels it.
- **One drag at a time** in a scene. A second player's pick up while one is
  under way falls through; per-player drags wait on multi-device focus, an
  open decision.
- **Tooltips** stay shut during a drag (R9.12e).
- **Observability.** The tree document carries `drag`, null between drags:
  how it's driven (`pointer` or `keys`), the player, the kind, the source,
  ghost and target (each `{id, key}`, the target null over none), whether the
  target accepts, the point and the travel. Every node has `dragSource` (the
  kind it carries, null on anything else) and `dropTarget`, and its state has
  `dragging`, `dropAccepts` and `dropActive`, each null where it doesn't apply.
  `/api/input` drives a pointer drag with `down`, `move` and `up`, or one
  `drag,x0,y0,x1,y1`: a move to the start, the press, four moves along the
  line and the release. A key drag is `keydown` or `pad` events, as any
  navigation is.

Why declared: the keys have to know what can be picked up, and which targets
accept, before the drag gets anywhere, and neither a start call made from a
press handler nor an `accept()` made in a dragenter can tell them. Godot
(`_get_drag_data`, `_can_drop_data`) and Flutter (`Draggable`, `DragTarget`'s
`onWillAcceptWithDetails`) declare both sides the same way. The spec followed
HTML's event model, which has no keyboard path at all. Card games played on a
pad converge on the same shape for the keys: pick a card, move a cursor among
the legal targets, confirm or back out. Why the innermost target decides:
Godot passes a refusal up to the parent, and in a hand-over-road layout that
plays a card on the road when the player aimed it at a vehicle.

Not built until a game asks: touch, scrolling a scroller from its edge during
a drag, copy and move effects, drags between scenes or windows, `parkOffset`
(R8.30) and per-player drags.

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
    Flex, Grid, None), `flexDirection`, `flexWrap`, `gap`, `padding`,
    `boxSizing`, `alignItems`, `justifyItems`, `justifyContent`,
    `gridTemplateColumns`, `gridTemplateRows`, and as a child `flexGrow`,
    `flexShrink`, `alignSelf`, `justifySelf`, `gridColumn`, `gridRow`,
    `position` (Relative or Absolute), `inset` and `aspectRatio`. `display: Free`, the
    default, places each child at its own top-level `position`, its auto size
    hugging their boxes; Flex (and Grid) flow theirs, and `position:
    Absolute` takes a child out of a flowing parent to its `position` or
    `inset`, whose edges are px or a percentage of the parent's whole box
    (CSS's padding box, whatever the parent's padding), resolved in the same
    layout pass as everything else, so a plate's hotspots placed by fraction
    follow it through a resize in the frame it happens. The default isn't
    CSS's Block: in normal flow a child's `position` means nothing and
    children stack, so every hand-placed screen
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
- **Grid is CSS Grid Layout Level 1** in the same pass, its placement (8.5)
  and track sizing (11) as plain functions over the children's measured sizes.
  Tracks are taffy's `TrackSizingFunction`: a px float, `percent()` and
  `kAuto` as a size spells them, `fr()`, and `minmax()` of those; a percentage
  is auto while the grid's size hangs on its tracks. `gridColumn` and
  `gridRow` are taffy's `Line<GridPlacement>`, a start and an end each of
  `line(n)` (negative from the explicit grid's end), `span(n)` or auto, one
  edge alone being the start, as in CSS. Auto placement is row first and
  sparse, and implicit tracks are auto, CSS's initial `gridAutoRows`. `gap` is
  one value for both axes, and a negative one is 0 in a grid, where CSS allows
  none. The alignments are Flex's enum, Stretch (CSS's normal) filling an auto
  axis and starting a definite one, and one that doesn't fit starts, as in a
  flex row. Where Grid follows salal over CSS: a child's percentage size is of
  the grid's content box, as a flex child's is, not of its area; an `Absolute`
  child goes to its `position` or `inset` in the grid's box, as under Flex,
  not into an area its lines name; a grid with an auto width hugs its columns'
  max-content widths, sized again into the room there is when they don't fit
  it, as a text wraps; and a container child's min-content width is 0, the
  narrowest a flex row takes it to. Not built until a game asks: named lines
  and areas, `repeat()`, the min-content, max-content and fit-content() track
  sizes, `gridAutoColumns` and `gridAutoRows`, dense and column-first flow,
  subgrid, baselines, and `justifyContent` and `alignContent` on a grid (its
  tracks start at its content box's start, and auto tracks stretch into free
  space).
- **Flex wraps as CSS's `flex-wrap`** (decided 2026-11-02, for a game's
  resource strip that wraps when the window narrows): `flexWrap: Wrap` starts
  a new line where the next child's margin box, at its clamped basis, would
  pass the main size, and a row with an auto width breaks at the room there
  is and hugs its widest line, as a text wraps and an auto-width grid sizes
  its columns. Each line grows and shrinks its own children and is as big
  across as its biggest, a stretched child fills its line, and the lines are
  `gap` apart, the one gap for both axes Grid settled. Where wrap follows
  salal over CSS: the lines pack from the start, each its tallest child's
  height, where CSS's initial `align-content` stretches them into a fixed
  container's free space. Not built until a game asks: `wrap-reverse` and
  `alignContent`.
- **`aspectRatio` is CSS's `aspect-ratio`** (decided 2026-11-10, for boxes
  that hold art with a shape): width over height, and a box with one axis
  auto takes it from the other, whether that one is declared, stretched
  across a flex container or a grid area, or flexed along one. With both
  auto the width is the content's and the height follows. It's the box
  `boxSizing` names, `minSize` and `maxSize` win over it, and a box with
  both axes declared ignores it. One departure: CSS stretches the auto cross
  axis of a flex or grid child even when its other axis is declared, which
  breaks the shape the ratio was set for, so here a declared (or, in a grid,
  stretched) width keeps the height the ratio's. A child with both axes auto
  still stretches across a single-line flex container, and its main size
  follows; once it has grown, shrunk or wrapped onto a line, both its axes
  are given and the ratio is spent, as in CSS. Two cases to keep out of: a
  both-auto ratio child in a container whose cross size is its content's is
  measured unstretched and arranged stretched, so the container is sized for
  the smaller box, and a grid child that isn't stretched across its column
  takes its width from the stretched height after the columns were sized.
  Give the box one declared axis and neither happens. A ratio that isn't
  finite is none.
- **Box sizing is CSS's `box-sizing`** (decided 2026-11-02), border-box by
  default, which is what every node did before it: a px or percentage
  `size`, `minSize` and `maxSize` are the whole box, padding in. `boxSizing:
  ContentBox` makes them the content box with the padding added outside,
  which is CSS's initial value; salal keeps border-box as its default, as
  most stylesheets reset it. An auto size is the content and the padding
  either way. Layout has no border, so the padding is all the difference. An
  absolutely placed child places against its parent's whole box, CSS's
  padding box, whatever the parent's box sizing, and a flowing child's
  percentage is of the parent's content box either way.

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
  The App chooses it: the player's setting alone, or with a reference size
  the window's resolution times the setting ([app-api.md](app-api.md), "UI
  scale", 2026-11-10). A context takes 0.125 to 8.
- **The frame:** a context's roots and overlays can lay out in less than
  the window ("The frame", below, 2026-11-10).
- **Scroll extent (R4.13):** content less than a px past a scroller's box is
  layout rounding and counts as fitting: no bar, and the wheel and paging pass
  the scroller by (2026-11-10).
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
