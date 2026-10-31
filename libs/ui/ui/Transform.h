#pragma once

// A component's transform (R8.26): CSS's, applied to render and hit testing and never to layout or
// the lint, which see the untransformed box. Affine is the 2D map it and a component's position make,
// which the context composes into each component's screen transform.

#include <math/Types.h>

namespace ui {

	struct Transform {
		float			 rotate = 0.0F; // radians, clockwise on screen (y is down)
		Foundation::Vec2 scale{1.0F, 1.0F};
		Foundation::Vec2 translate{0.0F, 0.0F}; // px
		Foundation::Vec2 origin{0.5F, 0.5F};	// what it turns and scales about, as a fraction of the box

		[[nodiscard]] bool isIdentity() const;
	};

	// x' = a x + c y + tx, y' = b x + d y + ty.
	struct Affine {
		float a = 1.0F;
		float b = 0.0F;
		float c = 0.0F;
		float d = 1.0F;
		float tx = 0.0F;
		float ty = 0.0F;

		[[nodiscard]] static Affine translation(Foundation::Vec2 offset);
		// The map from a box's local space, at `position` in its parent's space and `size` across, to the
		// parent's space: CSS's translate(origin) translate rotate scale translate(-origin).
		[[nodiscard]] static Affine local(Foundation::Vec2 position, Foundation::Vec2 size, const Transform& transform);

		[[nodiscard]] Foundation::Vec2 apply(Foundation::Vec2 point) const;
		// This after `inner`: (this * inner)(p) = this(inner(p)).
		[[nodiscard]] Affine operator*(const Affine& inner) const;
		// The inverse; the identity for a map that has none (a zero scale).
		[[nodiscard]] Affine		   inverse() const;
		[[nodiscard]] bool			   isTranslation() const { return a == 1.0F && b == 0.0F && c == 0.0F && d == 1.0F; }
		[[nodiscard]] Foundation::Mat4 toMat4() const;
	};

} // namespace ui
