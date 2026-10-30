#include "primitives/BatchRenderer.h"
#include "BaseShader.h"
#include "CoordinateSystem/CoordinateSystem.h"
#include "backend/NullBackend.h"
#include "primitives/BoundaryLoops.h"
#include <array>
#include <bit>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/packing.hpp>
#include <gtest/gtest.h>
#include <numbers>
#include <vector>

// Everything here runs headless: the batch is made on the null backend, which
// touches no GPU, and adding geometry and planning the draw runs reach no backend
// at all.

using namespace Renderer;
using Primitives::BlendMode;

namespace {

	constexpr float kTolerance = 1e-4F;

	// Two atlases and an image on `gpu`, which the batch checks a draw's texture against.
	std::array<TextureHandle, 3> textures(backend::NullBackend& gpu) {
		return {
			gpu.createTexture({.width = 1, .height = 1}),
			gpu.createTexture({.width = 1, .height = 1}),
			gpu.createTexture({.width = 1, .height = 1})
		};
	}

	// A glyph and a rect with their left edge at x, so a plan's order reads off its quads.
	void addGlyph(BatchRenderer& batch, TextureHandle atlas, BlendMode blend = BlendMode::Alpha, int32_t key = 0, float x = 0.0F) {
		batch.addTextQuad(
			{x, 0.0F}, {8.0F, 8.0F}, {0.0F, 0.0F}, {0.1F, 0.1F}, Foundation::Color::white(), {0.0F, 8.0F}, atlas, 4.0F, key, blend
		);
	}

	void addRect(BatchRenderer& batch, BlendMode blend = BlendMode::Alpha, int32_t key = 0, float x = 0.0F) {
		batch.addQuad({x, 0.0F, 10.0F, 10.0F}, Foundation::Color::red(), std::nullopt, 0.0F, std::nullopt, key, blend);
	}

	// Each quad's top-left x, in order.
	std::vector<float> lefts(std::span<const BatchInstance> quads) {
		std::vector<float> out;
		for (const BatchInstance& quad : quads) {
			out.push_back(quad.corners[0].x);
		}
		return out;
	}

	std::vector<QuadMode> modes(std::span<const BatchInstance> quads) {
		std::vector<QuadMode> out;
		for (const BatchInstance& quad : quads) {
			out.push_back(static_cast<QuadMode>(quad.mode));
		}
		return out;
	}

	// The four halves of a quad's two half words.
	Foundation::Vec4 halves(const uint32_t (&words)[2]) { // NOLINT(modernize-avoid-c-arrays)
		return {glm::unpackHalf2x16(words[0]), glm::unpackHalf2x16(words[1])};
	}

	// A stroke quad's box-local position at one of its corners, as base.vert works it out.
	Foundation::Vec2 strokeLocal(const BatchInstance& quad, int corner) {
		const Foundation::Vec2 offset =
			quad.corners[corner] - Foundation::Vec2(std::bit_cast<float>(quad.radii[0]), std::bit_cast<float>(quad.radii[1]));
		return {
			glm::dot(Foundation::Vec2(quad.geometry.x, quad.geometry.y), offset),
			glm::dot(Foundation::Vec2(quad.geometry.z, quad.geometry.w), offset)
		};
	}

	void expectNear(Foundation::Vec2 actual, Foundation::Vec2 expected) {
		EXPECT_NEAR(actual.x, expected.x, kTolerance);
		EXPECT_NEAR(actual.y, expected.y, kTolerance);
	}

} // namespace

// ============================================================================
// Modes and flags
// ============================================================================

TEST(BatchModeTest, EveryAddWritesItsModeAndTheAdditiveFlag) {
	backend::NullBackend gpu;
	BatchRenderer		 batch(gpu, base_shader::source());
	const auto [kAtlasA, kAtlasB, kImage] = textures(gpu);
	addRect(batch, BlendMode::Additive);
	addGlyph(batch, kAtlasA, BlendMode::Additive);
	batch.addShadowQuad({0.0F, 0.0F, 10.0F, 10.0F}, Foundation::BoxShadow{}, 0.0F, 0, BlendMode::Additive);
	const Foundation::Vec2 triangle[3] = {{0.0F, 0.0F}, {1.0F, 0.0F}, {0.0F, 1.0F}};
	const uint16_t		   triangleIndices[3] = {0, 1, 2};
	batch.addTriangles(
		{.vertices = triangle, .indices = triangleIndices, .color = Foundation::Color::white(), .blend = BlendMode::Additive}
	);
	batch.addArc({.center = {0.0F, 0.0F}, .innerRadius = 4.0F, .outerRadius = 8.0F, .blend = BlendMode::Additive});
	batch.addImageQuad({.texture = kImage, .bounds = {0.0F, 0.0F, 4.0F, 4.0F}, .blend = BlendMode::Additive});
	const StrokePiece piece{
		.center = {0.0F, 0.0F},
		.halfLength = 2.0F,
		.halfWidth = 1.0F,
		.corners = {{{-3.0F, -2.0F}, {3.0F, -2.0F}, {3.0F, 2.0F}, {-3.0F, 2.0F}}},
		.cornerCount = 4
	};
	batch.addStroke({&piece, 1}, Foundation::Color::white(), 0, BlendMode::Additive);

	const std::span<const BatchInstance> quads = batch.getPendingInstances();
	EXPECT_EQ(
		modes(quads),
		(std::vector{QuadMode::Rect, QuadMode::Text, QuadMode::Shadow, QuadMode::Flat, QuadMode::Arc, QuadMode::Image, QuadMode::Stroke})
	);
	for (const BatchInstance& quad : quads) {
		EXPECT_EQ(quad.flags & kQuadAdditive, kQuadAdditive);
	}
	EXPECT_EQ(quads[0].flags >> kQuadBorderShift, 1); // no border is a centred one
}

TEST(BatchModeTest, ARectCarriesItsBorderPremultipliedAndItsPosition) {
	backend::NullBackend gpu;
	BatchRenderer		 batch(gpu, base_shader::source());
	const auto			 rectWithBorder = [&](float alpha, Foundation::BorderPosition position, BlendMode blend) {
		  batch.addQuad(
			  {0.0F, 0.0F, 10.0F, 10.0F},
			  Foundation::Color::red(),
			  Foundation::BorderStyle{.color = {1.0F, 1.0F, 1.0F, alpha}, .width = 2.0F, .position = position},
			  3.0F,
			  std::nullopt,
			  0,
			  blend
		  );
	};
	rectWithBorder(1.0F, Foundation::BorderPosition::Center, BlendMode::Alpha);
	rectWithBorder(0.4F, Foundation::BorderPosition::Inside, BlendMode::Alpha);
	rectWithBorder(0.0F, Foundation::BorderPosition::Outside, BlendMode::Additive);
	addRect(batch);

	const std::span<const BatchInstance> quads = batch.getPendingInstances();
	ASSERT_EQ(quads.size(), 4U);
	// Half size, then the quad's half extent: grown by the part of the border outside the shape.
	EXPECT_EQ(quads[0].geometry, Foundation::Vec4(5.0F, 5.0F, 6.0F, 6.0F));
	EXPECT_EQ(quads[1].geometry, Foundation::Vec4(5.0F, 5.0F, 5.0F, 5.0F));
	EXPECT_EQ(quads[2].geometry, Foundation::Vec4(5.0F, 5.0F, 7.0F, 7.0F));
	expectNear(quads[0].corners[0], {-1.0F, -1.0F});
	EXPECT_EQ(halves(quads[0].radii), Foundation::Vec4(3.0F));
	EXPECT_EQ(halves(quads[0].shape).x, 2.0F);
	EXPECT_EQ(quads[0].flags, 1 << kQuadBorderShift);
	EXPECT_EQ(quads[1].flags, 0);
	EXPECT_EQ(quads[2].flags, kQuadAdditive | (2 << kQuadBorderShift));
	EXPECT_EQ(quads[0].border, glm::packUnorm4x8(Foundation::Vec4(1.0F)));
	EXPECT_EQ(quads[1].border, glm::packUnorm4x8(Foundation::Vec4(0.4F))); // white at 0.4, premultiplied
	EXPECT_EQ(quads[2].border, 0U);
	EXPECT_EQ(quads[3].border, 0U);
	EXPECT_EQ(halves(quads[3].shape).x, 0.0F);
	for (const uint32_t fill : quads[0].fills) {
		EXPECT_EQ(fill, glm::packUnorm4x8(Foundation::Vec4(1.0F, 0.0F, 0.0F, 1.0F)));
	}
}

TEST(BatchModeTest, AGradientsCornersArePremultipliedBeforeTheyInterpolate) {
	backend::NullBackend gpu;
	BatchRenderer		 batch(gpu, base_shader::source());
	batch.addQuad(
		{0.0F, 0.0F, 10.0F, 10.0F},
		Foundation::Color::white(),
		std::nullopt,
		0.0F,
		Foundation::LinearGradient{.from = {1.0F, 0.0F, 0.0F, 1.0F}, .to = {0.0F, 0.0F, 1.0F, 0.0F}, .horizontal = true}
	);
	// Left to right, red to transparent: transparent is zero whatever its colour, so
	// halfway the pixel is half red, not half purple.
	const BatchInstance& quad = batch.getPendingInstances()[0];
	EXPECT_EQ(quad.fills[0], glm::packUnorm4x8(Foundation::Vec4(1.0F, 0.0F, 0.0F, 1.0F)));
	EXPECT_EQ(quad.fills[1], 0U);
	EXPECT_EQ(quad.fills[2], 0U);
	EXPECT_EQ(quad.fills[3], glm::packUnorm4x8(Foundation::Vec4(1.0F, 0.0F, 0.0F, 1.0F)));
}

TEST(BatchModeTest, AdditiveSharesAlphasBlendState) {
	EXPECT_EQ(blendState(BlendMode::Alpha), backend::BlendState::Alpha);
	EXPECT_EQ(blendState(BlendMode::Additive), backend::BlendState::Alpha);
}

TEST(BatchModeTest, AGlyphCarriesItsUvRectFlippedAndItsRange) {
	backend::NullBackend gpu;
	BatchRenderer		 batch(gpu, base_shader::source());
	const auto [kAtlasA, kAtlasB, kImage] = textures(gpu);
	batch.addTextQuad({4.0F, 2.0F}, {8.0F, 6.0F}, {0.1F, 0.2F}, {0.3F, 0.4F}, Foundation::Color::white(), {4.0F, 8.0F}, kAtlasA, 6.0F);
	const BatchInstance& glyph = batch.getPendingInstances()[0];
	EXPECT_EQ(glyph.geometry, Foundation::Vec4(0.1F, 0.4F, 0.3F, 0.2F)); // the atlas is uploaded bottom row first
	EXPECT_EQ(halves(glyph.shape).z, 6.0F);
	expectNear(glyph.corners[0], {4.0F, 2.0F});
	expectNear(glyph.corners[2], {12.0F, 8.0F});
}

// ============================================================================
// Draw plan: key sort and run splitting, headless
// ============================================================================

TEST(BatchPlanTest, EmptyBatchHasNoRuns) {
	backend::NullBackend gpu;
	BatchRenderer		 batch(gpu, base_shader::source());
	const DrawPlan		 plan = batch.plan();
	EXPECT_TRUE(plan.instances.empty());
	EXPECT_TRUE(plan.runs.empty());
}

TEST(BatchPlanTest, InterleavedAlphaAndAdditiveOnOneAtlasIsOneRun) {
	backend::NullBackend gpu;
	BatchRenderer		 batch(gpu, base_shader::source());
	const auto [kAtlasA, kAtlasB, kImage] = textures(gpu);
	batch.setFontAtlas(kAtlasA);
	addRect(batch, BlendMode::Alpha);
	addGlyph(batch, kAtlasA, BlendMode::Additive);
	addRect(batch, BlendMode::Additive);
	addGlyph(batch, {}, BlendMode::Alpha); // the default atlas, which is A
	batch.addArc({.center = {0.0F, 0.0F}, .outerRadius = 8.0F, .blend = BlendMode::Additive});
	addGlyph(batch, kAtlasA, BlendMode::Additive);

	const DrawPlan plan = batch.plan();
	ASSERT_EQ(plan.runs.size(), 1U);
	EXPECT_EQ(plan.runs[0].instanceStart, 0U);
	EXPECT_EQ(plan.runs[0].instanceCount, 6U);
	EXPECT_EQ(plan.runs[0].texture, kAtlasA);
	EXPECT_EQ(plan.runs[0].blendState, backend::BlendState::Alpha);
	EXPECT_EQ(plan.instances.size(), 6U);
}

TEST(BatchPlanTest, AtlasChangeStillSplits) {
	backend::NullBackend gpu;
	BatchRenderer		 batch(gpu, base_shader::source());
	const auto [kAtlasA, kAtlasB, kImage] = textures(gpu);
	batch.setFontAtlas(kAtlasA);
	addGlyph(batch, kAtlasA);
	addRect(batch, BlendMode::Additive);
	addGlyph(batch, kAtlasB, BlendMode::Additive);
	addGlyph(batch, kAtlasB);
	addRect(batch);
	addGlyph(batch, kAtlasA, BlendMode::Additive);

	const DrawPlan plan = batch.plan();
	ASSERT_EQ(plan.runs.size(), 3U);
	EXPECT_EQ(plan.runs[0].texture, kAtlasA);
	EXPECT_EQ(plan.runs[0].instanceStart, 0U);
	EXPECT_EQ(plan.runs[0].instanceCount, 2U);
	EXPECT_EQ(plan.runs[1].texture, kAtlasB);
	EXPECT_EQ(plan.runs[1].instanceStart, 2U);
	EXPECT_EQ(plan.runs[1].instanceCount, 3U);
	EXPECT_EQ(plan.runs[2].texture, kAtlasA);
	EXPECT_EQ(plan.runs[2].instanceStart, 5U);
	EXPECT_EQ(plan.runs[2].instanceCount, 1U);
}

TEST(BatchPlanTest, ShapesOnlyAreOneRunOnTheDefaultAtlas) {
	backend::NullBackend gpu;
	BatchRenderer		 batch(gpu, base_shader::source());
	const auto [kAtlasA, kAtlasB, kImage] = textures(gpu);
	batch.setFontAtlas(kAtlasA);
	addRect(batch);
	addRect(batch, BlendMode::Additive);

	const DrawPlan plan = batch.plan();
	ASSERT_EQ(plan.runs.size(), 1U);
	EXPECT_EQ(plan.runs[0].texture, kAtlasA);
	EXPECT_EQ(plan.runs[0].instanceCount, 2U);
}

TEST(BatchPlanTest, AnImageSplitsOnItsTexture) {
	backend::NullBackend gpu;
	BatchRenderer		 batch(gpu, base_shader::source());
	const auto [kAtlasA, kAtlasB, kImage] = textures(gpu);
	batch.setFontAtlas(kAtlasA);
	addGlyph(batch, kAtlasA);
	batch.addImageQuad({.texture = kImage, .bounds = {0.0F, 0.0F, 16.0F, 16.0F}});
	batch.addImageQuad({.texture = {}, .bounds = {0.0F, 0.0F, 16.0F, 16.0F}}); // no texture: nothing added
	addGlyph(batch, kAtlasA);

	const DrawPlan plan = batch.plan();
	ASSERT_EQ(plan.runs.size(), 3U);
	EXPECT_EQ(plan.runs[0].texture, kAtlasA);
	EXPECT_EQ(plan.runs[1].texture, kImage);
	EXPECT_EQ(plan.runs[1].instanceStart, 1U);
	EXPECT_EQ(plan.runs[1].instanceCount, 1U);
	EXPECT_EQ(plan.runs[2].texture, kAtlasA);
	EXPECT_EQ(batch.getPendingInstances().size(), 3U);
}

TEST(BatchPlanTest, KeysSortGroupsStablyBeforeSplitting) {
	backend::NullBackend gpu;
	BatchRenderer		 batch(gpu, base_shader::source());
	const auto [kAtlasA, kAtlasB, kImage] = textures(gpu);
	batch.setFontAtlas(kAtlasA);
	addGlyph(batch, kAtlasA, BlendMode::Alpha, 1, 0.0F);
	addGlyph(batch, kAtlasB, BlendMode::Additive, 0, 1.0F);
	addRect(batch, BlendMode::Additive, 1, 2.0F);
	addGlyph(batch, kAtlasB, BlendMode::Alpha, 0, 3.0F);

	// Key 0 first in submission order (glyph B, glyph B), then key 1 (glyph A, rect).
	const DrawPlan plan = batch.plan();
	EXPECT_EQ(lefts(plan.instances), (std::vector{1.0F, 3.0F, 0.0F, 2.0F}));
	ASSERT_EQ(plan.runs.size(), 2U);
	EXPECT_EQ(plan.runs[0].texture, kAtlasB);
	EXPECT_EQ(plan.runs[0].instanceCount, 2U);
	EXPECT_EQ(plan.runs[1].texture, kAtlasA);
	EXPECT_EQ(plan.runs[1].instanceStart, 2U);
	EXPECT_EQ(plan.runs[1].instanceCount, 2U);
}

TEST(BatchPlanTest, ATextRunIsOneGroupHoweverManyGlyphs) {
	backend::NullBackend gpu;
	BatchRenderer		 batch(gpu, base_shader::source());
	const auto [kAtlasA, kAtlasB, kImage] = textures(gpu);
	batch.setFontAtlas(kAtlasA);
	addRect(batch, BlendMode::Alpha, 2, 1000.0F);
	for (int glyph = 0; glyph < 40; ++glyph) {
		addGlyph(batch, kAtlasA, BlendMode::Alpha, 1, static_cast<float>(glyph + 1));
	}
	EXPECT_EQ(batch.getDrawGroupCount(), 2U);

	// The next run at another blend, then one in a new sort domain, then a rect at
	// the second run's key: a group each.
	for (int glyph = 0; glyph < 3; ++glyph) {
		addGlyph(batch, kAtlasA, BlendMode::Additive, 1, 100.0F);
	}
	batch.sortBarrier();
	for (int glyph = 0; glyph < 3; ++glyph) {
		addGlyph(batch, kAtlasA, BlendMode::Additive, 1, 200.0F);
	}
	addRect(batch, BlendMode::Alpha, 1, 2000.0F);
	EXPECT_EQ(batch.getDrawGroupCount(), 5U);

	// The first run sorts as a whole, every glyph in order, then the additive run,
	// then the key 2 rect, all under the new domain's two groups.
	const DrawPlan			 plan = batch.plan();
	const std::vector<float> order = lefts(plan.instances);
	ASSERT_EQ(order.size(), 1U + 40U + 3U + 3U + 1U);
	EXPECT_EQ(order[0], 1.0F);
	EXPECT_EQ(order[39], 40.0F);
	EXPECT_EQ(order[40], 100.0F);
	EXPECT_EQ(order[43], 1000.0F);
	EXPECT_EQ(order[44], 200.0F);
	EXPECT_EQ(order[47], 2000.0F);
	EXPECT_EQ(plan.runs.size(), 1U);
}

TEST(BatchPlanTest, ASortBarrierKeepsEarlierDrawsUnderLaterOnesWhateverTheirKeys) {
	backend::NullBackend gpu;
	BatchRenderer		 batch(gpu, base_shader::source());
	const auto [kAtlasA, kAtlasB, kImage] = textures(gpu);
	batch.setFontAtlas(kAtlasA);
	addRect(batch, BlendMode::Alpha, 5, 0.0F);
	addRect(batch, BlendMode::Alpha, 1, 1.0F);
	batch.sortBarrier();
	addRect(batch, BlendMode::Alpha, -3, 2.0F);
	addRect(batch, BlendMode::Alpha, -4, 3.0F);

	// Each side sorted by key, the first side wholly under the second; still one run.
	const DrawPlan plan = batch.plan();
	EXPECT_EQ(lefts(plan.instances), (std::vector{1.0F, 0.0F, 3.0F, 2.0F}));
	EXPECT_EQ(plan.runs.size(), 1U);
}

// ============================================================================
// Arcs
// ============================================================================

TEST(BatchArcTest, AnArcCarriesItsRadiiHalfSweepAndMiddle) {
	backend::NullBackend gpu;
	BatchRenderer		 batch(gpu, base_shader::source());
	const float			 quarter = std::numbers::pi_v<float> * 0.5F;
	batch.addArc({.center = {100.0F, 50.0F}, .innerRadius = 10.0F, .outerRadius = 20.0F, .startAngle = 0.0F, .sweep = quarter});

	const std::span<const BatchInstance> quads = batch.getPendingInstances();
	ASSERT_EQ(quads.size(), 1U);
	EXPECT_NEAR(quads[0].geometry.x, 10.0F, kTolerance);
	EXPECT_NEAR(quads[0].geometry.y, 20.0F, kTolerance);
	EXPECT_NEAR(quads[0].geometry.z, quarter * 0.5F, kTolerance);
	// The middle at 45 degrees (screen space, clockwise from +x), toward the bottom-right
	// corner, which base.vert turns onto +x.
	EXPECT_NEAR(quads[0].geometry.w, quarter * 0.5F, kTolerance);
	// The quad reaches a pixel past the outer radius.
	EXPECT_EQ(halves(quads[0].shape).y, 1.0F);
	expectNear(quads[0].corners[0], {79.0F, 29.0F});
	expectNear(quads[0].corners[2], {121.0F, 71.0F});
}

TEST(BatchArcTest, NegativeSweepRunsBackFromTheStart) {
	backend::NullBackend gpu;
	BatchRenderer		 batch(gpu, base_shader::source());
	const float			 pi = std::numbers::pi_v<float>;
	// From 180 degrees back to 90: the middle is at 135, the bottom-left corner.
	batch.addArc({.center = {0.0F, 0.0F}, .outerRadius = 10.0F, .startAngle = pi, .sweep = -pi * 0.5F});

	const std::span<const BatchInstance> quads = batch.getPendingInstances();
	ASSERT_EQ(quads.size(), 1U);
	EXPECT_NEAR(quads[0].geometry.z, pi * 0.25F, kTolerance);
	EXPECT_NEAR(quads[0].geometry.w, pi * 0.75F, kTolerance);
}

TEST(BatchArcTest, SweepClampsToTheFullRingAndEmptyArcsAddNothing) {
	backend::NullBackend gpu;
	BatchRenderer		 batch(gpu, base_shader::source());
	batch.addArc({.center = {0.0F, 0.0F}, .outerRadius = 10.0F, .sweep = 20.0F});
	ASSERT_EQ(batch.getPendingInstances().size(), 1U);
	EXPECT_NEAR(batch.getPendingInstances()[0].geometry.z, std::numbers::pi_v<float>, kTolerance);

	batch.addArc({.center = {0.0F, 0.0F}, .innerRadius = 10.0F, .outerRadius = 10.0F});
	batch.addArc({.center = {0.0F, 0.0F}, .outerRadius = 10.0F, .sweep = 0.0F});
	EXPECT_EQ(batch.getPendingInstances().size(), 1U);
}

// ============================================================================
// Stroke pieces
// ============================================================================

namespace {

	// A piece down the screen around (10, 0), five corners, and a flat triangle.
	const StrokePiece kPieces[2] = {
		{.center = {10.0F, 0.0F},
		 .axis = {0.0F, 1.0F},
		 .halfLength = 5.0F,
		 .halfWidth = 2.0F,
		 .cornerRadius = 1.5F,
		 .corners = {{{13.0F, -6.0F}, {13.0F, 6.0F}, {7.0F, 6.0F}, {6.0F, 0.0F}, {7.0F, -6.0F}}},
		 .cornerCount = 5},
		{.center = {0.0F, 0.0F}, .corners = {{{0.0F, 0.0F}, {4.0F, 0.0F}, {0.0F, 4.0F}}}, .cornerCount = 3},
	};

} // namespace

TEST(BatchStrokeTest, PiecesDrawTheirPolygonsAsFansOfQuadsInOneGroup) {
	backend::NullBackend gpu;
	BatchRenderer		 batch(gpu, base_shader::source());
	batch.addStroke(kPieces, Foundation::Color::white(), 3);
	addRect(batch); // key 0 sorts ahead of the whole stroke

	// The five corners as two quads, (0, 1, 2, 3) and (0, 3, 4, 4); the zero-size box flat.
	const std::span<const BatchInstance> quads = batch.getPendingInstances();
	ASSERT_EQ(quads.size(), 4U);
	EXPECT_EQ(modes(quads), (std::vector{QuadMode::Stroke, QuadMode::Stroke, QuadMode::Flat, QuadMode::Rect}));
	EXPECT_EQ(quads[0].corners[3], Foundation::Vec2(6.0F, 0.0F));
	EXPECT_EQ(quads[1].corners[1], Foundation::Vec2(6.0F, 0.0F));
	EXPECT_EQ(quads[1].corners[2], Foundation::Vec2(7.0F, -6.0F));
	EXPECT_EQ(quads[1].corners[3], Foundation::Vec2(7.0F, -6.0F));
	EXPECT_EQ(quads[2].corners[3], Foundation::Vec2(0.0F, 4.0F));

	// The SDF's coordinates run along the axis (down the screen) and along its left
	// normal, (-1, 0), from its centre.
	expectNear(strokeLocal(quads[0], 0), {-6.0F, -3.0F});
	expectNear(strokeLocal(quads[0], 3), {0.0F, 4.0F});
	EXPECT_EQ(std::bit_cast<float>(quads[0].shape[0]), 5.0F);
	EXPECT_EQ(std::bit_cast<float>(quads[0].shape[1]), 2.0F);
	EXPECT_EQ(std::bit_cast<float>(quads[0].border), 1.5F);

	// One group: the rect sorts in front of it, and the stroke stays together after.
	const DrawPlan plan = batch.plan();
	EXPECT_EQ(modes(plan.instances), (std::vector{QuadMode::Rect, QuadMode::Stroke, QuadMode::Stroke, QuadMode::Flat}));
	ASSERT_EQ(plan.runs.size(), 1U);
}

TEST(BatchStrokeTest, ThePiecesFrameUndoesTheTransform) {
	backend::NullBackend gpu;
	BatchRenderer		 batch(gpu, base_shader::source());
	batch.setTransform(
		glm::scale(
			glm::rotate(
				glm::translate(Foundation::Mat4(1.0F), Foundation::Vec3(40.0F, 30.0F, 0.0F)), 0.6F, Foundation::Vec3(0.0F, 0.0F, 1.0F)
			),
			Foundation::Vec3(3.0F, 0.5F, 1.0F)
		)
	);
	batch.addStroke({kPieces, 1}, Foundation::Color::white());
	const std::span<const BatchInstance> quads = batch.getPendingInstances();
	ASSERT_EQ(quads.size(), 2U);
	// The same box coordinates as untransformed, from corners moved, turned and stretched.
	EXPECT_NEAR(strokeLocal(quads[0], 0).x, -6.0F, 1e-3F);
	EXPECT_NEAR(strokeLocal(quads[0], 0).y, -3.0F, 1e-3F);
	EXPECT_NEAR(strokeLocal(quads[1], 2).x, -6.0F, 1e-3F);
	EXPECT_NEAR(strokeLocal(quads[1], 2).y, 3.0F, 1e-3F);
}

// ============================================================================
// Feather rings
// ============================================================================

namespace {

	// A 10 x 10 square as two triangles.
	const std::array<Foundation::Vec2, 4> kSquare = {{{0.0F, 0.0F}, {10.0F, 0.0F}, {10.0F, 10.0F}, {0.0F, 10.0F}}};
	const std::array<uint16_t, 6>		  kSquareIndices = {0, 1, 2, 0, 2, 3};

	BoundaryLoops squareLoops() {
		BoundaryLoops loops;
		EXPECT_TRUE(findBoundaryLoops(kSquare, kSquareIndices, loops));
		return loops;
	}

	void addSquare(BatchRenderer& batch, const BoundaryLoops* feather, const Foundation::Color* colors = nullptr) {
		batch.addTriangles(
			{.vertices = kSquare, .indices = kSquareIndices, .color = {1.0F, 0.5F, 0.25F, 0.8F}, .colors = colors, .feather = feather}
		);
	}

} // namespace

TEST(BatchFeatherTest, ARingOneDevicePixelWideRunsOutsideTheBoundaryFadingToTransparent) {
	backend::NullBackend gpu;
	BatchRenderer		 batch(gpu, base_shader::source());
	const BoundaryLoops	 loops = squareLoops();
	addSquare(batch, &loops);

	// The fill's two triangles, then a quad per boundary edge from the fill's own
	// corners out to points on their miters, a pixel from both edges: the corners a
	// pixel out diagonally.
	const std::span<const BatchInstance> quads = batch.getPendingInstances();
	ASSERT_EQ(quads.size(), 6U);
	EXPECT_EQ(quads[0].corners[3], quads[0].corners[2]); // a triangle repeats its last corner
	expectNear(quads[2].corners[0], {0.0F, 0.0F});
	expectNear(quads[2].corners[1], {10.0F, 0.0F});
	expectNear(quads[2].corners[2], {11.0F, -1.0F});
	expectNear(quads[2].corners[3], {-1.0F, -1.0F});
	expectNear(quads[3].corners[2], {11.0F, 11.0F});
	expectNear(quads[4].corners[2], {-1.0F, 11.0F});
	const uint32_t fill = glm::packUnorm4x8(Foundation::Vec4(1.0F * 0.8F, 0.5F * 0.8F, 0.25F * 0.8F, 0.8F));
	for (size_t i = 0; i < quads.size(); ++i) {
		EXPECT_EQ(static_cast<QuadMode>(quads[i].mode), QuadMode::Flat) << "quad " << i;
		EXPECT_EQ(quads[i].fills[0], fill) << "quad " << i;
		EXPECT_EQ(quads[i].fills[1], fill) << "quad " << i;
	}
	EXPECT_EQ(quads[2].fills[2], 0U);
	EXPECT_EQ(quads[2].fills[3], 0U);

	// One group, one run.
	EXPECT_EQ(batch.getDrawGroupCount(), 1U);
	EXPECT_EQ(batch.plan().runs.size(), 1U);
}

TEST(BatchFeatherTest, TheRingIsOneDevicePixelAtTheCoordinateSystemsRatio) {
	backend::NullBackend gpu;
	BatchRenderer		 batch(gpu, base_shader::source());
	CoordinateSystem	 coordinates;
	coordinates.setViewport({.logicalWidth = 100, .logicalHeight = 100, .framebufferWidth = 200, .framebufferHeight = 200});
	batch.setCoordinateSystem(&coordinates);
	const BoundaryLoops loops = squareLoops();
	addSquare(batch, &loops);
	const std::span<const BatchInstance> quads = batch.getPendingInstances();
	ASSERT_EQ(quads.size(), 6U);
	expectNear(quads[2].corners[3], {-0.5F, -0.5F});
	expectNear(quads[3].corners[2], {10.5F, 10.5F});
}

TEST(BatchFeatherTest, TheRingRunsOutwardThroughAMirroringTransform) {
	backend::NullBackend gpu;
	BatchRenderer		 batch(gpu, base_shader::source());
	batch.setTransform(glm::scale(Foundation::Mat4(1.0F), Foundation::Vec3(-2.0F, 1.0F, 1.0F)));
	const BoundaryLoops loops = squareLoops();
	addSquare(batch, &loops);
	// The square lands on x from -20 to 0; a pixel is a pixel on screen.
	const std::span<const BatchInstance> quads = batch.getPendingInstances();
	ASSERT_EQ(quads.size(), 6U);
	expectNear(quads[2].corners[3], {1.0F, -1.0F});
	expectNear(quads[2].corners[2], {-21.0F, -1.0F});
	expectNear(quads[3].corners[2], {-21.0F, 11.0F});
}

TEST(BatchFeatherTest, EachBoundaryVertexFadesItsOwnColour) {
	backend::NullBackend				   gpu;
	BatchRenderer						   batch(gpu, base_shader::source());
	const BoundaryLoops					   loops = squareLoops();
	const std::array<Foundation::Color, 4> colors = {
		Foundation::Color::red(), Foundation::Color::green(), Foundation::Color::blue(), Foundation::Color(1.0F, 1.0F, 1.0F, 0.5F)
	};
	addSquare(batch, &loops, colors.data());
	const std::span<const BatchInstance> quads = batch.getPendingInstances();
	ASSERT_EQ(quads.size(), 6U);
	for (size_t edge = 0; edge < 4; ++edge) {
		const Foundation::Vec4 from = colors[edge].toVec4();
		const Foundation::Vec4 to = colors[(edge + 1) % 4].toVec4();
		EXPECT_EQ(quads[2 + edge].fills[0], glm::packUnorm4x8(Foundation::Vec4(Foundation::Vec3(from) * from.a, from.a)))
			<< "edge " << edge;
		EXPECT_EQ(quads[2 + edge].fills[1], glm::packUnorm4x8(Foundation::Vec4(Foundation::Vec3(to) * to.a, to.a))) << "edge " << edge;
		EXPECT_EQ(quads[2 + edge].fills[2], 0U) << "edge " << edge;
	}
}

TEST(BatchFeatherTest, ASharpCornersRingStopsFourPixelsOut) {
	backend::NullBackend				  gpu;
	BatchRenderer						  batch(gpu, base_shader::source());
	const std::array<Foundation::Vec2, 3> spike = {{{0.0F, 0.0F}, {100.0F, 1.0F}, {0.0F, 2.0F}}};
	const std::array<uint16_t, 3>		  triangle = {0, 1, 2};
	BoundaryLoops						  loops;
	ASSERT_TRUE(findBoundaryLoops(spike, triangle, loops));
	batch.addTriangles({.vertices = spike, .indices = triangle, .color = Foundation::Color::white(), .feather = &loops});
	const std::span<const BatchInstance> quads = batch.getPendingInstances();
	ASSERT_EQ(quads.size(), 4U);
	// The tip's miter would be 100 pixels long; the first edge's quad reaches the tip's outer point.
	const Foundation::Vec2 tipOuter = quads[1].corners[2];
	EXPECT_NEAR(glm::length(tipOuter - spike[1]), 4.0F, 1e-4F);
	EXPECT_GT(tipOuter.x, 100.0F);
}

TEST(BatchRendererTest, ALocalDevicePixelSpansAtMostADevicePixelInAnyDirection) {
	backend::NullBackend gpu;
	BatchRenderer		 batch(gpu, base_shader::source());
	EXPECT_FLOAT_EQ(batch.getLocalDevicePixel(), 1.0F);
	// Stretched 4 along x and squashed to a quarter along y: a unit along x is 4 pixels,
	// so a device pixel is a quarter unit, though the area is unscaled.
	batch.setTransform(glm::scale(Foundation::Mat4(1.0F), Foundation::Vec3(4.0F, 0.25F, 1.0F)));
	EXPECT_FLOAT_EQ(batch.getLocalPixel(), 1.0F);
	EXPECT_FLOAT_EQ(batch.getLocalDevicePixel(), 0.25F);
	// Turned and scaled 2, at a pixel ratio of 2.
	CoordinateSystem coordinates;
	coordinates.setViewport({.logicalWidth = 100, .logicalHeight = 100, .framebufferWidth = 200, .framebufferHeight = 200});
	batch.setCoordinateSystem(&coordinates);
	batch.setTransform(
		glm::scale(glm::rotate(Foundation::Mat4(1.0F), 0.7F, Foundation::Vec3(0.0F, 0.0F, 1.0F)), Foundation::Vec3(2.0F, 2.0F, 1.0F))
	);
	EXPECT_NEAR(batch.getLocalDevicePixel(), 0.25F, 1e-6F);
	batch.setCoordinateSystem(nullptr);
}

TEST(BatchFeatherTest, WithoutLoopsTrianglesAddNoRing) {
	backend::NullBackend gpu;
	BatchRenderer		 batch(gpu, base_shader::source());
	const BoundaryLoops	 none;
	addSquare(batch, nullptr);
	addSquare(batch, &none);
	EXPECT_EQ(batch.getPendingInstances().size(), 4U);
	EXPECT_EQ(batch.plan().instances.size(), 4U);
}
