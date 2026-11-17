# Masks

A rect can carry a CSS mask: `mask-image` layers composited with `mask-composite`, the rect showing
where the result covers and as much as it covers. It follows CSS Masking Level 1 (css-masking-1), with
SVG's `<mask>` for content, Compositing and Blending Level 1 for `mix-blend-mode` and `isolation`, and
says so where it stops. Overlapping, moving vector shapes cut each other with no polygon booleans on
the CPU. A rect, polygon or mesh layer covers a pixel or doesn't; a content layer covers it partly, by
values its shapes paint, which is how soft edges are drawn as vector geometry with no raster. Worldsim's
player visibility cover is the first use (its sight-softening design, 7.5 to 7.7); lighting is
expected to be the next.

## API

`primitives/MaskImage.h`, and `RectArgs::mask` in `primitives/Primitives.h`:

```cpp
namespace Renderer::Primitives {
	enum class MaskComposite : uint8_t { Add, Subtract };          // mask-composite
	enum class MaskMode : uint8_t { Alpha, Luminance };            // mask-mode
	enum class MaskBlend : uint8_t { Normal, Lighten, Darken };    // mix-blend-mode, on a content shape
	enum class MaskIsolation : uint8_t { Auto, Isolate };          // isolation, on a group of shapes

	class MaskPaint { // a content shape's fill
	  public:
		static MaskPaint flat(float value);
		static MaskPaint linear(Foundation::Vec2 from, Foundation::Vec2 to, float fromValue, float toValue);
	};

	class MaskMesh { // an uploaded mesh as a content shape; .mesh = {mesh, camera}
	  public:
		MaskMesh(const InstancedMeshHandle& mesh, const WorldCamera& camera);
	};

	class MaskImage { // closed: built only through its factories
	  public:
		static MaskImage rect(const Foundation::Rect& bounds);
		static MaskImage polygon(std::span<const Foundation::Vec2> points);
		static MaskImage mesh(const InstancedMeshHandle& mesh, const WorldCamera& camera);
		static MaskImage content(std::span<const MaskShape> shapes); // an SVG <mask> of shapes
	};

	struct MaskLayer {
		MaskImage	  image;
		MaskComposite composite = MaskComposite::Add;
		MaskMode	  mode = MaskMode::Alpha;
	};

	struct MaskShape { // an element inside the <mask>: a <polygon>, a mesh, or a <g>
		std::span<const Foundation::Vec2> points;
		MaskMesh						  mesh;
		std::vector<MaskShape>			  children;
		MaskPaint						  paint = MaskPaint::flat(0.0F); // black, as SVG fills
		std::span<const MaskLayer>		  mask;                          // its own mask, hard layers only
		MaskBlend						  blend = MaskBlend::Normal;
		MaskIsolation					  isolation = MaskIsolation::Auto;
	};
}
// RectArgs, after `fill`:
std::span<const MaskLayer> mask; // top layer first; empty is no mask
```

A cover with hard edges, the translucent fill first and the opaque one over it, each mask a rect minus
a union:

```cpp
std::vector<MaskLayer> dimMask = {{.image = MaskImage::rect(view), .composite = MaskComposite::Subtract}};
for (const SightShape& sight : sights) {
	dimMask.push_back({.image = MaskImage::polygon(sight.screenPoints)});
}
Primitives::drawRect({.bounds = view, .style = {.fill = {0.0F, 0.0F, 0.0F, kSeenDimAlpha}}, .mask = dimMask, .key = coverKey});
```

The same tint with soft edges: one content layer, the view at 1, then a group a colonist, isolated and
combined with what is under it by darken, holding his bright shape at 0 and over it his feather and
shadows by lighten, each clipped to his bright shape by a mask of its own. The tint's value is the
least, over the colonists, of each one's greatest darkness:

```cpp
std::vector<MaskShape> tint = {{.points = viewPoints, .paint = MaskPaint::flat(1.0F)}};
for (const ColonistCover& colonist : colonistsInView) {
	const std::vector<MaskLayer> insideBrightShape = {{.image = MaskImage::polygon(colonist.brightShape)}};
	MaskShape group{.blend = MaskBlend::Darken, .isolation = MaskIsolation::Isolate};
	group.children.push_back({.points = colonist.brightShape, .paint = MaskPaint::flat(0.0F)});
	for (const DarknessPiece& piece : colonist.pieces) {
		group.children.push_back({.points = piece.screenPoints, .paint = piece.paint, .mask = insideBrightShape, .blend = MaskBlend::Lighten});
	}
	tint.push_back(std::move(group));
}
Primitives::drawRect({.bounds = view, .style = {.fill = kTintColor}, .mask = {{.image = MaskImage::content(tint), .mode = MaskMode::Luminance}}, .key = coverKey});
```

(`insideBrightShape` must live until the `drawRect` call, as every span a draw's args hold must.) The
unseen fill puts a content layer of the seen area's feather on top of hard layers, the view less the
record's meshes and the pending shapes; worldsim's sight-softening design, 7.5, has it in full.

`apps/sandbox`'s `mask` scene draws both, every case of that design's 7.7, over a busy background, and
`CoverageMask.test.cpp` draws the same cases and checks every value it reads back against the rules.

## Mapping to CSS

| CSS / SVG | salal | not built |
|---|---|---|
| `mask` on an element | `RectArgs::mask` | `mask` on any other draw's args |
| `mask-image` | `MaskImage::rect`, `polygon` (CSS `polygon()`, nonzero), `mesh` (an uploaded mesh, as an SVG `<mask>`'s vector children), `content` (an SVG `<mask>` of shapes) | `circle()`, `ellipse()`, `url()`, gradients, images |
| layers, listed top first | the span's order | |
| `mask-composite: add`, `subtract` | `MaskComposite::Add`, `Subtract`, source over and source out, on partial values too | `intersect`, `exclude` |
| `mask-mode: alpha`, `luminance` | `MaskMode::Alpha`, `Luminance`, per layer | `match-source` |
| SVG `<polygon>`, `<g>` in a `<mask>` | `MaskShape::points`, `children`; `mesh` for an uploaded mesh | every other element |
| SVG `fill` with a grey, or a `linearGradient` in user space | `MaskPaint::flat`, `MaskPaint::linear` (two stops, `spreadMethod="pad"`) | more stops, other spreads, `objectBoundingBox`, `radialGradient` |
| `mix-blend-mode: normal`, `lighten`, `darken` | `MaskBlend::Normal`, `Lighten`, `Darken`, per shape | every other mode |
| `isolation: isolate` | `MaskIsolation::Isolate`, on a group | |
| `mask` on an element inside a `<mask>` | `MaskShape::mask`, rect, polygon and mesh layers | a content layer there |
| `mask-position`, `mask-size`, `mask-repeat`, `mask-origin` | a layer is where its points are | all four |
| `mask-clip` | none: a layer is never clipped | `mask-clip` |

Each is "not built until a game asks", salal's rule; none is refused for a reason that would stop it
being added.

## Rules

- **Compositing is CSS's.** Layers composite bottom up, each with everything under it. `Add` is source
  over: the layer and what is under it. `Subtract` is the layer where it falls outside what is under
  it. The bottom layer's composite is ignored, as in CSS, since there is nothing under it. The rect
  shows where the result covers, as much as it covers.
- **A rect, polygon or mesh layer's coverage is binary.** It covers a pixel or doesn't, by GL's
  rasterisation rules, so its edges are hard and two layers that share an edge share no pixel. A content
  layer's is partial (below). The masked rect itself keeps its own anti-aliased edge, rounded corners
  and border; its shadow is masked with it.
- **Coordinates.** A `rect` or `polygon` layer is in the current transform's space, mapped at the draw
  like any batched draw. A `mesh` layer is in world space under its camera, one copy at the world
  origin, as `drawInstanced` would draw it with an identity instance.
- **Fill rule.** A polygon fills by the nonzero rule, tessellated as `drawPolygon`'s fill is and from
  the same cache, so a self-crossing outline covers what drawPolygon would fill.
- **Lifetime.** Everything a draw's args point at is read during the call: rect and polygon layers are
  copied into the batch then, as `PolygonArgs::vertices` is, so the caller's points can change or go
  as soon as `drawRect` returns. A `MaskImage::polygon` holds the span, so the points must live until
  that call. A mesh layer names the mesh, which must not be destroyed before the frame's flush;
  releasing an instanced mesh while draws are pending waits for the end of the next flush, as freeing
  a texture does, so a caller that releases or re-uploads the mesh mid-frame keeps the layer.
- **Clips.** `pushClip` clips the masked rect as any draw. It never clips a layer.
- **Keys.** A mask never enters the sort key. Masked draws sort with everything else and the batch
  cuts a run where the mask changes, as it does for the blend state, so key order stays exact. Equal
  masks, compared by content, share one entry, and consecutive runs under the same mask share one
  build.
- **A target with no stencil buffer** draws a masked rect as nothing, logged once: drawn unmasked it
  would cover the whole view. A target with no alpha or depth does the same under a content layer
  ("Where the stencil, alpha and depth live", below).

## Content layers: partial coverage

- **The canvas.** A content layer's shapes are painted in order on nothing, value 0. A shape lands where
  it covers (its own mask's coverage included) by its blend: `Normal` its value, `Lighten` the greater
  of its value and the canvas's, `Darken` the lesser. Values are greys from 0 to 1, clamped.
- **Mode.** `Luminance` reads the values. `Alpha` reads the shapes' alpha, and every shape is opaque, so
  an `Alpha` content layer covers where any shape lands, fully: its values and blends don't matter.
  A rect, polygon or mesh layer reads the same in either mode.
- **Groups.** A group whose `isolation` is `Isolate`, or whose blend isn't `Normal` (CSS isolates it
  then too), composites its shapes on nothing first and lands as the result, where any of its shapes
  does, by its own blend. A group that is neither is no group: its shapes land straight on the canvas.
- **Paint.** A polygon is filled by its paint, black unless set. A linear gradient runs from `fromValue`
  at `from` to `toValue` at `to`, in the shape's own space, padded past each end; with `from` equal to
  `to` it is `toValue` everywhere, as SVG paints it. A mesh shape's value at a vertex is the vertex
  colour's alpha, its colour read as white, linear over each triangle; its `paint` is ignored.
- **Where it sits.** One content layer, the top layer. Under it any rect, polygon and mesh layers
  composite as before, to 0 or 1, and the content layer's `composite` puts it on them: `Add`, source
  over, is the greater of the two (with a binary layer under it, nothing is darkened twice), `Subtract`,
  source out, is the content where they are 0.
- **The draw.** The rect's colour lands at its own alpha times the mask, as CSS composites a masked
  element, so a mask of 0.5 under an opaque fill blends it half over what is beneath.
- **Coordinates and lifetime** are a polygon layer's for polygon shapes and a mesh layer's for mesh
  shapes. Everything a content layer names is read during the `drawRect` call: shapes, their points
  and their masks are copied then; meshes must outlive the flush.

**What salal evaluates, and what it refuses.** Inside an isolated group, a shape after the first must
blend by `Lighten`: the group's value is then the greatest of its shapes', which is what the GPU
evaluates (below). The first shape may blend any way, since it lands on nothing. A drawRect whose mask
breaks one of these draws nothing, logged once for each reason:

- a content layer anywhere but on top, or more than one;
- an isolated group inside another, or a group shape after the first not blending by `Lighten`;
- a group carrying its own `mask` (give its shapes the mask), or a shape with both children and its own
  points or mesh;
- a content layer inside a shape's own mask.

**Where salal departs from CSS.**

- A `Darken` shape over canvas nothing has painted yet lands as 0, where SVG would show its value (a
  blend over a transparent backdrop shows the source). Paint a base first, as the tint's view at 1 does.
- A rect's own quads (its shadow, then its fill and border) each land at their alpha times the mask, one
  after the other, not composited together first, so where a masked rect's shadow lies under it the two
  differ from CSS's isolated element by the shadow's share of a partly covered pixel. An additive rect
  under a content mask draws nothing.
- After the rect, the target's alpha over the mask's cover box is 1. The window is opaque, so nothing
  changes there; a render target whose alpha mattered under the rect loses it.
- Each value is carried in 8 bits, at each shape's corners and in the target, so a value is within about
  2/255 of the rule's. Where two shapes of one group hold values within a 255th of each other, the
  group's greatest may be the lesser of the two.

## How it is built

Underneath, a mask's rect, polygon and mesh layers are two bits of the bound target's stencil buffer,
which no caller sees. At add time a masked rect's layers are mapped to the screen and interned into the
flush's mask table, deduplicated
by content, and the rect's draw group carries the entry's index. At flush, before a run whose mask
differs from the last one built, the mask is built.

The build evaluates the layers as a small expression, bottom up, over the box round every quad drawn
under the mask (its cover box, the only pixels its runs can reach). The accumulator bit holds the
layers composited so far, either as is or inverted, and the scratch bit holds a layer's coverage
while a `Subtract` composites it.

1. The two bits are cleared across the target, so nothing carries over between builds and a game's GL
   that left values in the stencil changes nothing.
2. Layers are drawn bottom up, aliased, with no clip and no colour. `Add`, and the bottom layer,
   sets the accumulator where the layer lands, or zeroes it there while the accumulator is
   inverted.
3. `Subtract` by a layer that holds the cover box is the complement of what is under it, so it
   inverts the accumulator by flipping how it is read, and draws nothing.
4. `Subtract` by any other layer draws it into the scratch bit, then two cover passes over the cover
   box: one zeroes the accumulator where the scratch bit is clear, the next, where it is set,
   inverts the accumulator (or, while it is inverted, leaves it) and clears the scratch bit. That
   leaves the accumulator, not inverted, as the layer outside what it held.
5. The run draws where the accumulator bit is set, or clear when it ended inverted.

A layer holds the cover box when its triangles on screen are exactly an axis-aligned box, two
triangles over its four corners split along a diagonal (a `rect` layer under a transform that only
scales and moves; compared exactly, so one turned by a hair is not), and that box contains the cover
box, edges included. The cover box is the bounds of the masked quads' corners as they were sent: a
rect's quad is its bounds, grown by a centred or outside border, and its shadow's quad is part of
it. A clip only removes pixels, so it doesn't enter. This is exact, not a near match, so the cover
the game draws, a rect layer with the masked rect's own bounds, depends on both making the same
corners from those bounds to the bit: a `rect` layer and every rect's quad take theirs from one
function (`rectCorners`), from the edges, `x` and `x + width`. A quad made from its centre plus and
minus half its size lands an ulp off for about a third of float rects, and would miss. The layer and
the masked quads go through the same vertex transform, which is monotonic, so the containment
holds on the GPU too. A pixel centre exactly on an edge is decided by a tie rule. OpenGL's own
guarantee, that two triangles sharing an edge never both draw a pixel on it, covers only an edge
with the same two endpoints, and the box's edge and a masked quad's lie on one line but can end at
different points. What this rests on is the top-left rule every desktop GPU uses, which decides by
the edge's line and the side its triangle lies on alone, so collinear edges are decided alike
whatever their endpoints. A pixel centre inside a
masked quad lies in the cover box, so in the box: inside it, the box draws it; on an edge of the box
the box leaves out, the quad's triangles lie on one side of that same line, so the rule leaves it
out of them too. Within the run's pixels the layer is all ones, so `Subtract` by it is exactly the
complement. When the condition fails, as with a box that stops short of a masked rect, a turned box
or a polygon layer, the layer is built by step 4, exactly as before.

Worldsim's two covers, the view minus a union, are each the clear, the union's draws and the fill:
two full-view passes a mask where the general build took five (the clear, the rect into the scratch
bit, two cover passes and the fill). In the sandbox's `perf_mask`, which draws them over a 3072 by
1728 window, the frame's GPU time went from 0.640 to 0.273 ms. Of what is left, the two clears are
about 0.04 ms; clearing all eight stencil bits instead of two was about 0.01 ms cheaper, and drawing
adjoining union layers in one call or clearing one bit instead of two measured nothing, so neither
is done. `Mask.test.cpp` draws scenes both ways, the second with a
transparent rect under the same mask far past the target so no layer holds the cover box, and
compares the pixels byte for byte.

A mesh layer draws under the instanced uniforms and the batch's uniforms are set again after it; the
uniform ring takes several writes in one flush and a draw keeps the values it was given.

The backend draws each call in its `StencilState` (test, function, reference, read and write masks,
operations), its channel writes and its depth state, and puts the fixed state back, stencil and depth
tests off and every colour channel, stencil bit and the depth buffer written, before `draw` returns. A
game's GL that leaves stencil, depth or channel-write state changed calls
`Primitives::invalidateState()`, as it already does for culling and scissor.

## How partial coverage is built

No caller sees any of this; the API names no GPU state. At add time a content layer's shapes are
flattened in paint order, each polygon tessellated by the nonzero rule (from the polygon cache) and
each triangle given a value at its corners; a gradient's triangle crossing either padded end is cut
there into pieces that are each linear, so the values between corners are exact. Each shape's own mask
is interned first as a mask of its own, its cover box the box round the shapes it masks (the view, for
a mesh). Isolated groups are numbered. The shapes' triangles upload as flat quads, their values as
their colour.

At flush, a run under a content layer is drawn a quad at a time, the mask evaluated before each into
the bound target's alpha (8 bits) over the mask's cover box, with its depth buffer (24 bits) as scratch:

1. The layers under the content layer are built in stencil bits 1 and 2, as above.
2. Alpha is set to 0 over the cover box, alpha alone written.
3. A shape outside a group draws its values into alpha alone, by replace, max or min for `Normal`,
   `Lighten` and `Darken`, under its own mask: that mask built in stencil bits 3 and 4 the same way,
   cleared over its own cover box, and kept while the next shapes share it.
4. An isolated group's greatest is found in depth. The vertex shader takes a value as depth: group k
   of a run keeps value v at `(k + v / 2) / 4096`, so a later group's depths lie above every earlier
   one's and never match them, and depth is cleared once a run, and again every 4095 groups. Its shapes
   draw with no colour and a greater-or-equal depth test that writes, each under its own mask; then
   the same shapes draw again with an equal test that writes nothing, no stencil, writing their values
   to alpha by the group's blend. At each pixel only a shape holding the group's greatest passes, or one
   holding the same value, so alpha takes the group's blend of its greatest. Positions are `invariant`,
   so the two passes' depths are equal to the bit.
5. `Add` sets alpha to 1 where the layers under it cover, `Subtract` to 0, alpha alone written under
   the stencil test.
6. The quad draws twice: its alpha multiplies the target's (alpha alone written), then its colour,
   straight rather than premultiplied, lands over the target by that alpha (colour alone written), so
   the quad lands at its alpha times the mask.
7. Alpha is set back to 1 over the cover box.

A group costs its shapes twice and its own masks once each; with the tint's, a colonist costs his
bright shape three times (as a shape twice, as his pieces' mask once) and his pieces twice, linear in the
colonists, with no pass per pair. Masks of 0 or 1 alone take none of this: they draw exactly as above.

The backend's states for it (`backend/Backend.h`): blend states `Replace`, `Max`, `Min`,
`MultiplyDestinationAlpha` and `OverByDestinationAlpha` beside `Alpha`; channel writes `All`, `None`,
`Alpha` and `Colour`; depth states `Off`, `GreaterOrEqual` and `Equal`; `clearDepth()`; and
`hasAlphaAndDepth()`. Each draw puts the fixed state back, depth test off, every channel and the depth
buffer written, the blend equation add, before `draw` returns.

## Where the stencil, alpha and depth live

The window has all three: the GLFW platform asks for 8 stencil, 8 alpha and 24 depth bits (GLFW's
defaults, stated), and the GL backend checks the default framebuffer at creation and warns when it has
fewer. A `RenderTarget` has a stencil buffer and depth when made with
`RenderTarget::Attachments::ColourAndStencil` (a depth 24, stencil 8 renderbuffer), and alpha when its
colour format carries it (every format but `Rgb8`). A masked rect on a target with no stencil draws
nothing, and one under a content layer on a target with no alpha or depth draws nothing, each logged
once, decided at the flush against the target bound then.
