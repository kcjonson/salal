# Masks

A mask is the bound target's 8-bit stencil buffer. Some draws write a value into it with colour off;
later draws land only where the stored value compares to theirs. The GPU decides which pixels are
inside which shapes, so overlapping, moving vector shapes cut each other with hard edges: no
render-to-texture, no blur, no polygon booleans on the CPU. Worldsim's player visibility cover is the
first use; lighting is expected to be the next.

## API

In `Primitives` (`libs/renderer/primitives/Primitives.h`):

```cpp
using Mask = backend::MaskState; // {MaskMode mode; uint8_t value; MaskCompare compare;}
// MaskMode    Off, Write, Test
// MaskCompare Equal, NotEqual, Less, LessEqual, Greater, GreaterEqual (stored OP value)

void clearMask(uint8_t value = 0);
void setMask(const Mask& mask); // setMask({}) is off
Mask getMask();
```

A frame that fills the view where no shape is, dims where the first set of shapes is but the second
isn't, and leaves the second set untouched:

```cpp
namespace P = Renderer::Primitives;
P::clearMask(0);
P::setMask({.mode = P::MaskMode::Write, .value = 1});
P::drawInstanced(seenMesh, &instance, 1, camera); // an uploaded world mesh
P::drawTriangles(extraPolygon);
P::setMask({.mode = P::MaskMode::Write, .value = 2});
for (const Fan& sight : sights) {
	P::drawTriangles(sight.args); // replace: 2 wins over 1 where they overlap
}
P::setMask({.mode = P::MaskMode::Test, .value = 0});
P::drawRect({.bounds = view, .style = {.fill = unexplored}});
P::setMask({.mode = P::MaskMode::Test, .value = 1});
P::drawRect({.bounds = view, .style = {.fill = {0.0F, 0.0F, 0.0F, 0.6F}}});
P::setMask({});
```

`apps/sandbox`'s `mask` scene draws this over a busy background.

## Rules

- **Write** sets the mask to the value wherever a fragment lands, whatever its alpha, and writes no
  colour. Anti-aliased edges (a polygon's feather ring, an SDF shape's ramp) write their whole ramp,
  so draw a mask's shapes aliased: `drawTriangles` without `antiAlias`, or an uploaded mesh. A shape's
  edge then follows GL's rasterisation rules, so two shapes that share an edge share no pixel.
- **Test** draws only where the stored mask compares to the value, and never changes the mask.
- **Every renderer draw takes the mask in force**: batched draws, `drawInstanced`, and a baked pass,
  which takes the mask set when `beginBakedPass` ran. `setMask` inside a baked pass is refused.
- **A change flushes.** `setMask` with a different mask submits what is pending first, so draws added
  before it are drawn before draws added after it, whatever their keys. Within one mask keys sort as
  they do anywhere. `clearMask` flushes first when a mask is set, so pending masked draws see the mask
  as it was. A repeat of the mask in force costs nothing.
- **Clips apply** to Write and Test draws as to any batched draw: a write under `pushClip` writes
  only inside the clip. `pushClip` and `popClip` neither set nor end a mask.
- **The mask never outlives a submission on the GPU.** The backend sets the stencil and colour-write
  state a draw's mask needs and puts the fixed state back (stencil test off, every colour channel and
  stencil bit written) before it returns, and `endForeignDraws` does the same. A game's own GL between
  the renderer's draws is never masked by it. A game's GL that leaves stencil test or colour writes
  changed calls `Primitives::invalidateState()`, as it already does for depth, culling and scissor.
- **The mask goes off** at `beginFrame()` (warned once if one was left set), at the end of each
  scene's render, its ui's render and each debug layer's in an `app::App` (warned once), so nothing
  past the world/ui barrier is masked by the world pass, and around a `RenderTarget` (below).
- **Refused, logged once,** on a target with no mask.

## Where a mask lives

The window has one: the GLFW platform asks for 8 stencil bits, and the GL backend checks the default
framebuffer at creation and refuses masks (logged) when it has fewer.

A `RenderTarget` has one when made with `RenderTarget::Attachments::ColourAndMask` (a depth 24,
stencil 8 renderbuffer; depth is unused). One made with the default `Colour` has none, and `setMask`
is refused while it is bound. `begin()` turns the mask in force off, so its pending draws flush where
they were added and nothing drawn into the target is masked by the window's mask; `end()` turns off a
mask set inside the target and sets the one that was in force at `begin()` again. A target's mask is
its own, cleared with `clearMask` while it is bound, and undefined until then.

## Backend

`backend::DrawCall` carries a `MaskState`, `beginForeignDraws` takes one, and the backend has
`clearMask(value)` and `hasMask()` for the bound target; `createRenderTarget` takes whether to attach a
mask. `NullBackend` records each draw's mask, the clears and the foreign pass's mask, and
`setWindowMask(false)` stands in for a window made without stencil bits.
