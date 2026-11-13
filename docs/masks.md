# Masks

A rect can carry a CSS mask: `mask-image` layers composited with `mask-composite`, the rect showing
only where the result covers. It follows CSS Masking Level 1 (css-masking-1) for what it builds and
says so where it stops. Overlapping, moving vector shapes cut each other with hard edges and no
polygon booleans on the CPU. Worldsim's player visibility cover is the first use; lighting is expected
to be the next.

## API

`primitives/MaskImage.h`, and `RectArgs::mask` in `primitives/Primitives.h`:

```cpp
namespace Renderer::Primitives {
	enum class MaskComposite : uint8_t { Add, Subtract };

	class MaskImage { // closed: built only through its factories
	  public:
		static MaskImage rect(const Foundation::Rect& bounds);
		static MaskImage polygon(std::span<const Foundation::Vec2> points);
		static MaskImage mesh(const InstancedMeshHandle& mesh, const WorldCamera& camera);
	};

	struct MaskLayer {
		MaskImage	  image;
		MaskComposite composite = MaskComposite::Add;
	};
}
// RectArgs, after `fill`:
std::span<const MaskLayer> mask; // top layer first; empty is no mask
```

The cover worldsim draws, the translucent fill first and the opaque one over it, each mask a rect
minus a union:

```cpp
std::vector<MaskLayer> dimMask = {{.image = MaskImage::rect(view), .composite = MaskComposite::Subtract}};
for (const SightShape& sight : sights) {
	dimMask.push_back({.image = MaskImage::polygon(sight.screenPoints)});
}
Primitives::drawRect({.bounds = view, .style = {.fill = {0.0F, 0.0F, 0.0F, kSeenDimAlpha}}, .mask = dimMask, .key = coverKey});

std::vector<MaskLayer> unseenMask = {
	{.image = MaskImage::rect(view), .composite = MaskComposite::Subtract},
	{.image = MaskImage::mesh(seenMesh, camera)},
};
for (const PendingShape& pending : pendingShapes) {
	unseenMask.push_back({.image = MaskImage::polygon(pending.screenPoints)});
}
Primitives::drawRect({.bounds = view, .style = {.fill = kUnseenColor}, .mask = unseenMask, .key = coverKey + 1});
```

`apps/sandbox`'s `mask` scene draws this over a busy background.

## Mapping to CSS

| CSS | salal | not built |
|---|---|---|
| `mask` on an element | `RectArgs::mask` | `mask` on any other draw's args |
| `mask-image` | `MaskImage::rect`, `polygon` (CSS `polygon()`, nonzero), `mesh` (an uploaded mesh, as an SVG `<mask>`'s vector children) | `circle()`, `ellipse()`, `url()`, gradients, images, a nested mask image |
| layers, listed top first | the span's order | |
| `mask-composite: add` | `MaskComposite::Add` | |
| `mask-composite: subtract` | `MaskComposite::Subtract` | |
| `mask-composite: intersect`, `exclude` | | both |
| `mask-mode` | luminance and alpha are the same for hard coverage, so there is no mode | `mask-mode` |
| `mask-position`, `mask-size`, `mask-repeat`, `mask-origin` | a layer is where its points are | all four |
| `mask-clip` | none: a layer is never clipped | `mask-clip` |

Each is "not built until a game asks", salal's rule; none is refused for a reason that would stop it
being added.

## Rules

- **Compositing is CSS's.** Layers composite bottom up, each with everything under it. `Add` is source
  over: the layer and what is under it. `Subtract` is the layer where it falls outside what is under
  it. The bottom layer's composite is ignored, as in CSS, since there is nothing under it. The rect
  shows only where the result covers.
- **Coverage is binary.** A layer covers a pixel or doesn't, by GL's rasterisation rules, so a mask's
  edges are hard and two layers that share an edge share no pixel. The masked rect itself keeps its own
  anti-aliased edge, rounded corners and border; its shadow is masked with it.
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
  would cover the whole view. This is decided at the flush, against the target bound then, since
  `RenderTarget::begin` flushes nothing.

## How it is built

Underneath, a mask is two bits of the bound target's stencil buffer, which no caller sees. At add time
a masked rect's layers are mapped to the screen and interned into the flush's mask table, deduplicated
by content, and the rect's draw group carries the entry's index. At flush, before a run whose mask
differs from the last one built, the mask is built:

1. The two bits are cleared across the target, so nothing carries over between builds and a game's GL
   that left values in the stencil changes nothing.
2. Layers are drawn bottom up, aliased, with no clip and no colour. `Add` sets the accumulator bit
   where the layer lands.
3. `Subtract` draws the layer into the scratch bit, then two cover passes over the box round the
   rects drawn under the mask: one zeroes the accumulator where the scratch bit is clear, the next
   inverts both bits where it is set, which leaves the accumulator as the layer outside what it held
   and clears the scratch bit.
4. The run draws where the accumulator bit is set.

A mesh layer draws under the instanced uniforms and the batch's uniforms are set again after it; the
uniform ring takes several writes in one flush and a draw keeps the values it was given.

The backend draws each call in its `StencilState` (test, function, reference, read and write masks,
operations) and `writeColour`, and puts the fixed state back, stencil test off and every colour channel
and stencil bit written, before `draw` returns. A game's GL that leaves stencil or colour-write state
changed calls `Primitives::invalidateState()`, as it already does for depth, culling and scissor.

## Where a stencil buffer lives

The window has one: the GLFW platform asks for 8 stencil bits, and the GL backend checks the default
framebuffer at creation and warns when it has fewer. A `RenderTarget` has one when made with
`RenderTarget::Attachments::ColourAndStencil` (a depth 24, stencil 8 renderbuffer; depth is unused),
and none with the default `Colour`.
