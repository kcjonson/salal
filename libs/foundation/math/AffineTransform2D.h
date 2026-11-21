#pragma once

#include "math/Types.h"

#include <cmath>
#include <optional>

namespace Foundation { // NOLINT(readability-identifier-naming)

	// A 2D affine map in CSS and SVG's `matrix(a, b, c, d, e, f)` order, the members named as
	// DOMMatrix names them: a point (x, y) goes to (a * x + c * y + e, b * x + d * y + f). Default
	// constructed it is the identity. Plain + - * / in a fixed order, so with foundation's
	// -ffp-contract=off it gives the same bits on every platform.
	struct AffineTransform2D {
		float a = 1.0F;
		float b = 0.0F;
		float c = 0.0F;
		float d = 1.0F;
		float e = 0.0F;
		float f = 0.0F;

		[[nodiscard]] static constexpr AffineTransform2D identity() { return {}; }

		[[nodiscard]] constexpr Vec2 applyToPoint(Vec2 point) const {
			return {(a * point.x) + (c * point.y) + e, (b * point.x) + (d * point.y) + f};
		}

		// A direction or offset: the linear part alone, no translation.
		[[nodiscard]] constexpr Vec2 applyToVector(Vec2 vector) const {
			return {(a * vector.x) + (c * vector.y), (b * vector.x) + (d * vector.y)};
		}

		// DOMMatrix's multiply: `other` is applied first, then this, as `transform: this other`
		// reads in CSS. this.multiply(other).applyToPoint(p) == applyToPoint(other.applyToPoint(p)).
		[[nodiscard]] constexpr AffineTransform2D multiply(const AffineTransform2D& other) const {
			return {
				.a = (a * other.a) + (c * other.b),
				.b = (b * other.a) + (d * other.b),
				.c = (a * other.c) + (c * other.d),
				.d = (b * other.c) + (d * other.d),
				.e = (a * other.e) + (c * other.f) + e,
				.f = (b * other.e) + (d * other.f) + f,
			};
		}

		[[nodiscard]] constexpr float determinant() const { return (a * d) - (b * c); }

		// Empty when the map has no inverse in floats: a determinant of zero, a determinant that
		// itself overflows (a = d = 1e20), or one so small that the inverse overflows. A
		// near-singular map (a face seen almost edge-on) inverts to huge values; a caller that
		// cares tests the determinant first.
		[[nodiscard]] std::optional<AffineTransform2D> inverse() const {
			const float divisor = determinant();
			if (divisor == 0.0F || !std::isfinite(divisor)) {
				return std::nullopt;
			}
			const float				reciprocal = 1.0F / divisor;
			const AffineTransform2D result{
				.a = d * reciprocal,
				.b = -b * reciprocal,
				.c = -c * reciprocal,
				.d = a * reciprocal,
				.e = ((c * f) - (d * e)) * reciprocal,
				.f = ((b * e) - (a * f)) * reciprocal,
			};
			const bool finite = std::isfinite(result.a) && std::isfinite(result.b) && std::isfinite(result.c) && std::isfinite(result.d) &&
								std::isfinite(result.e) && std::isfinite(result.f);
			if (!finite) {
				return std::nullopt;
			}
			return result;
		}

		[[nodiscard]] constexpr bool operator==(const AffineTransform2D&) const = default;
	};

} // namespace Foundation
