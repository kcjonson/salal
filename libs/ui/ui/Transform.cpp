#include "ui/Transform.h"

#include <cmath>

namespace ui {

	bool Transform::isIdentity() const {
		return rotate == 0.0F && scale.x == 1.0F && scale.y == 1.0F && translate.x == 0.0F && translate.y == 0.0F;
	}

	Affine Affine::translation(Foundation::Vec2 offset) {
		return {.tx = offset.x, .ty = offset.y};
	}

	Affine Affine::local(Foundation::Vec2 position, Foundation::Vec2 size, const Transform& transform) {
		if (transform.isIdentity()) {
			return translation(position);
		}
		const float			   cosine = std::cos(transform.rotate);
		const float			   sine = std::sin(transform.rotate);
		const Foundation::Vec2 pivot = transform.origin * size;
		Affine				   out{
							.a = cosine * transform.scale.x,
							.b = sine * transform.scale.x,
							.c = -sine * transform.scale.y,
							.d = cosine * transform.scale.y,
		};
		// translate(position + pivot + translate) * linear * translate(-pivot)
		out.tx = position.x + pivot.x + transform.translate.x - (out.a * pivot.x + out.c * pivot.y);
		out.ty = position.y + pivot.y + transform.translate.y - (out.b * pivot.x + out.d * pivot.y);
		return out;
	}

	Foundation::Vec2 Affine::apply(Foundation::Vec2 point) const {
		return {a * point.x + c * point.y + tx, b * point.x + d * point.y + ty};
	}

	Affine Affine::operator*(const Affine& inner) const {
		return {
			.a = a * inner.a + c * inner.b,
			.b = b * inner.a + d * inner.b,
			.c = a * inner.c + c * inner.d,
			.d = b * inner.c + d * inner.d,
			.tx = a * inner.tx + c * inner.ty + tx,
			.ty = b * inner.tx + d * inner.ty + ty,
		};
	}

	Affine Affine::inverse() const {
		const float determinant = a * d - b * c;
		if (determinant == 0.0F) {
			return {};
		}
		const float inv = 1.0F / determinant;
		Affine		out{.a = d * inv, .b = -b * inv, .c = -c * inv, .d = a * inv};
		out.tx = -(out.a * tx + out.c * ty);
		out.ty = -(out.b * tx + out.d * ty);
		return out;
	}

	Foundation::Mat4 Affine::toMat4() const {
		Foundation::Mat4 out(1.0F);
		out[0][0] = a;
		out[0][1] = b;
		out[1][0] = c;
		out[1][1] = d;
		out[3][0] = tx;
		out[3][1] = ty;
		return out;
	}

} // namespace ui
