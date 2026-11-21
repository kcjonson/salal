#include "math/AffineTransform2D.h"

#include <gtest/gtest.h>

using Foundation::AffineTransform2D;
using Foundation::Vec2;

namespace {

	void expectNear(Vec2 actual, Vec2 expected, float tolerance = 1e-5F) {
		EXPECT_NEAR(actual.x, expected.x, tolerance);
		EXPECT_NEAR(actual.y, expected.y, tolerance);
	}

} // namespace

// matrix(a, b, c, d, e, f) maps (x, y) to (a x + c y + e, b x + d y + f), as CSS and SVG do; a
// vector takes the linear part only.
TEST(AffineTransform2D, AppliesInCssMatrixOrder) {
	const AffineTransform2D transform{.a = 2.0F, .b = 3.0F, .c = 5.0F, .d = 7.0F, .e = 11.0F, .f = 13.0F};
	EXPECT_EQ(transform.applyToPoint({1.0F, 0.0F}), Vec2(13.0F, 16.0F));
	EXPECT_EQ(transform.applyToPoint({0.0F, 1.0F}), Vec2(16.0F, 20.0F));
	EXPECT_EQ(transform.applyToVector({1.0F, 1.0F}), Vec2(7.0F, 10.0F));
	EXPECT_EQ(AffineTransform2D::identity().applyToPoint({-4.5F, 9.25F}), Vec2(-4.5F, 9.25F));
	EXPECT_EQ(AffineTransform2D{}, AffineTransform2D::identity());
}

// multiply is DOMMatrix's: the argument applies first.
TEST(AffineTransform2D, MultiplyAppliesTheArgumentFirst) {
	const AffineTransform2D skew{.a = 0.8F, .b = 0.6F, .c = 0.0F, .d = 0.35F, .e = 1.0F, .f = -2.0F};
	const AffineTransform2D translate{.e = 3.0F, .f = 4.0F};
	const AffineTransform2D scale{.a = 2.0F, .d = 0.5F};
	const Vec2				point{1.5F, -2.5F};
	expectNear(skew.multiply(translate).applyToPoint(point), skew.applyToPoint(translate.applyToPoint(point)));
	expectNear(translate.multiply(skew).applyToPoint(point), translate.applyToPoint(skew.applyToPoint(point)));
	expectNear(
		skew.multiply(translate).multiply(scale).applyToPoint(point), skew.applyToPoint(translate.applyToPoint(scale.applyToPoint(point)))
	);
}

TEST(AffineTransform2D, InverseUndoesTheMap) {
	const AffineTransform2D skew{.a = 0.8F, .b = 0.6F, .c = 0.0F, .d = 0.35F, .e = 1.0F, .f = -2.0F};
	const auto				inverse = skew.inverse();
	ASSERT_TRUE(inverse.has_value());
	const Vec2 point{1.5F, -2.5F};
	expectNear(inverse->applyToPoint(skew.applyToPoint(point)), point);
	const AffineTransform2D product = skew.multiply(*inverse);
	expectNear({product.a, product.b}, {1.0F, 0.0F});
	expectNear({product.c, product.d}, {0.0F, 1.0F});
	expectNear({product.e, product.f}, {0.0F, 0.0F});
	EXPECT_NEAR(skew.determinant(), 0.28F, 1e-6F);
}

// A singular map has no inverse; a near-singular one (a face almost edge-on) has a finite one.
TEST(AffineTransform2D, ASingularMapHasNoInverse) {
	EXPECT_FALSE((AffineTransform2D{.a = 0.0F, .b = 1.0F, .c = 0.0F, .d = 0.35F}).inverse().has_value());
	EXPECT_FALSE((AffineTransform2D{.a = 2.0F, .b = 4.0F, .c = 1.0F, .d = 2.0F}).inverse().has_value());
	EXPECT_FALSE((AffineTransform2D{.a = 1e-30F, .b = 0.0F, .c = 0.0F, .d = 1e-30F}).inverse().has_value());
	const AffineTransform2D edgeOn{.a = 3e-6F, .b = 1.0F, .c = 0.0F, .d = 0.35F};
	const auto				inverse = edgeOn.inverse();
	ASSERT_TRUE(inverse.has_value());
	expectNear(inverse->applyToPoint(edgeOn.applyToPoint({0.25F, 0.5F})), {0.25F, 0.5F}, 1e-2F);
}
