#include "primitives/BatchRenderer.h"
#include <gtest/gtest.h>
#include <numbers>

// Everything here runs without a GL context: a BatchRenderer that was never
// init()ed holds no GL objects, and adding geometry and planning the draw runs
// touch none.

using namespace Renderer;
using Primitives::BlendMode;

namespace {

	constexpr float kTolerance = 1e-4F;

	const TextureHandle kAtlasA = TextureHandle::make(1, 0);
	const TextureHandle kAtlasB = TextureHandle::make(2, 0);
	const TextureHandle kImage = TextureHandle::make(3, 0);

	void addGlyph(BatchRenderer& batch, TextureHandle atlas, BlendMode blend = BlendMode::Alpha, float zIndex = 0.0F) {
		batch.addTextQuad(
			{0.0F, 0.0F}, {8.0F, 8.0F}, {0.0F, 0.0F}, {0.1F, 0.1F}, Foundation::Color::white(), {0.0F, 8.0F}, atlas, zIndex, blend
		);
	}

	void addRect(BatchRenderer& batch, BlendMode blend = BlendMode::Alpha, float zIndex = 0.0F) {
		batch.addQuad({0.0F, 0.0F, 10.0F, 10.0F}, Foundation::Color::red(), std::nullopt, 0.0F, std::nullopt, zIndex, blend);
	}

} // namespace

// ============================================================================
// UberVertex Structure Tests
// ============================================================================

TEST(UberVertexTest, SizeIsReasonable) {
	// position (8) + texCoord (8) + color (16) + data1 (16) + data2 (16) + clipBounds (16) = 80
	// However, Foundation types may vary - verify it's reasonable
	static_assert(sizeof(UberVertex) <= 96, "UberVertex should be <= 96 bytes");
}

// ============================================================================
// Render modes and the additive flag in data2.w
// ============================================================================

TEST(RenderModeTest, AlphaKeepsTheModeAndAdditiveMovesItOutOfEveryAlphaRange) {
	EXPECT_EQ(renderMode(0.0F, BlendMode::Alpha), 0.0F);
	EXPECT_EQ(renderMode(2.0F, BlendMode::Alpha), 2.0F);
	EXPECT_EQ(renderMode(kRenderModeText, BlendMode::Alpha), -1.0F);

	EXPECT_EQ(renderMode(0.0F, BlendMode::Additive), 4.0F);
	EXPECT_EQ(renderMode(1.0F, BlendMode::Additive), 5.0F);
	EXPECT_EQ(renderMode(2.0F, BlendMode::Additive), 6.0F);
	EXPECT_EQ(renderMode(kRenderModeText, BlendMode::Additive), -11.0F);
	EXPECT_EQ(renderMode(kRenderModeShadow, BlendMode::Additive), -13.0F);
	EXPECT_EQ(renderMode(kRenderModeArc, BlendMode::Additive), -14.0F);
	EXPECT_EQ(renderMode(kRenderModeImage, BlendMode::Additive), -15.0F);
}

TEST(RenderModeTest, AdditiveSharesAlphasGlBlendState) {
	EXPECT_EQ(glBlendState(BlendMode::Alpha), BlendMode::Alpha);
	EXPECT_EQ(glBlendState(BlendMode::Additive), BlendMode::Alpha);
}

TEST(RenderModeTest, EveryAddWritesItsModeAndBlend) {
	BatchRenderer batch;
	addRect(batch, BlendMode::Additive);
	addGlyph(batch, kAtlasA, BlendMode::Additive);
	batch.addShadowQuad({0.0F, 0.0F, 10.0F, 10.0F}, Foundation::BoxShadow{}, 0.0F, 0.0F, BlendMode::Additive);
	const Foundation::Vec2 triangle[3] = {{0.0F, 0.0F}, {1.0F, 0.0F}, {0.0F, 1.0F}};
	const uint16_t		   triangleIndices[3] = {0, 1, 2};
	batch.addTriangles(triangle, triangleIndices, 3, 3, Foundation::Color::white(), nullptr, 0.0F, BlendMode::Additive);
	batch.addArc({.center = {0.0F, 0.0F}, .innerRadius = 4.0F, .outerRadius = 8.0F, .blend = BlendMode::Additive});
	batch.addImageQuad({.texture = kImage, .bounds = {0.0F, 0.0F, 4.0F, 4.0F}, .blend = BlendMode::Additive});
	const StrokePiece piece{.center = {0.0F, 0.0F}, .halfLength = 2.0F, .halfWidth = 1.0F};
	batch.addStroke({&piece, 1}, {}, Foundation::Color::white(), 0.0F, BlendMode::Additive);

	const std::span<const UberVertex> vertices = batch.getPendingVertices();
	ASSERT_EQ(vertices.size(), 4U + 4U + 4U + 3U + 4U + 4U + 4U);
	EXPECT_EQ(vertices[0].data2.w, 5.0F);	 // rect, center border position + 4
	EXPECT_EQ(vertices[4].data2.w, -11.0F);	 // text
	EXPECT_EQ(vertices[8].data2.w, -13.0F);	 // shadow
	EXPECT_EQ(vertices[12].data2.w, 5.0F);	 // triangles draw as shapes
	EXPECT_EQ(vertices[15].data2.w, -14.0F); // arc
	EXPECT_EQ(vertices[19].data2.w, -15.0F); // image
	EXPECT_EQ(vertices[23].data2.w, 5.0F);	 // stroke piece
}

// ============================================================================
// Draw plan: z sort and run splitting, headless
// ============================================================================

TEST(BatchPlanTest, EmptyBatchHasNoRuns) {
	BatchRenderer  batch;
	const DrawPlan plan = batch.plan();
	EXPECT_TRUE(plan.indices.empty());
	EXPECT_TRUE(plan.runs.empty());
}

TEST(BatchPlanTest, InterleavedAlphaAndAdditiveOnOneAtlasIsOneRun) {
	BatchRenderer batch;
	batch.setFontAtlas(kAtlasA);
	addRect(batch, BlendMode::Alpha);
	addGlyph(batch, kAtlasA, BlendMode::Additive);
	addRect(batch, BlendMode::Additive);
	addGlyph(batch, {}, BlendMode::Alpha); // the default atlas, which is A
	batch.addArc({.center = {0.0F, 0.0F}, .outerRadius = 8.0F, .blend = BlendMode::Additive});
	addGlyph(batch, kAtlasA, BlendMode::Additive);

	const DrawPlan plan = batch.plan();
	ASSERT_EQ(plan.runs.size(), 1U);
	EXPECT_EQ(plan.runs[0].indexStart, 0U);
	EXPECT_EQ(plan.runs[0].indexCount, 6U * 6U);
	EXPECT_EQ(plan.runs[0].texture, kAtlasA);
	EXPECT_EQ(plan.runs[0].blendState, BlendMode::Alpha);
	EXPECT_EQ(plan.indices.size(), 36U);
}

TEST(BatchPlanTest, AtlasChangeStillSplits) {
	BatchRenderer batch;
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
	EXPECT_EQ(plan.runs[0].indexStart, 0U);
	EXPECT_EQ(plan.runs[0].indexCount, 12U);
	EXPECT_EQ(plan.runs[1].texture, kAtlasB);
	EXPECT_EQ(plan.runs[1].indexStart, 12U);
	EXPECT_EQ(plan.runs[1].indexCount, 18U);
	EXPECT_EQ(plan.runs[2].texture, kAtlasA);
	EXPECT_EQ(plan.runs[2].indexStart, 30U);
	EXPECT_EQ(plan.runs[2].indexCount, 6U);
}

TEST(BatchPlanTest, ShapesOnlyAreOneRunOnTheDefaultAtlas) {
	BatchRenderer batch;
	batch.setFontAtlas(kAtlasA);
	addRect(batch);
	addRect(batch, BlendMode::Additive);

	const DrawPlan plan = batch.plan();
	ASSERT_EQ(plan.runs.size(), 1U);
	EXPECT_EQ(plan.runs[0].texture, kAtlasA);
	EXPECT_EQ(plan.runs[0].indexCount, 12U);
}

TEST(BatchPlanTest, AnImageSplitsOnItsTexture) {
	BatchRenderer batch;
	batch.setFontAtlas(kAtlasA);
	addGlyph(batch, kAtlasA);
	batch.addImageQuad({.texture = kImage, .bounds = {0.0F, 0.0F, 16.0F, 16.0F}});
	batch.addImageQuad({.texture = {}, .bounds = {0.0F, 0.0F, 16.0F, 16.0F}}); // no texture: nothing added
	addGlyph(batch, kAtlasA);

	const DrawPlan plan = batch.plan();
	ASSERT_EQ(plan.runs.size(), 3U);
	EXPECT_EQ(plan.runs[0].texture, kAtlasA);
	EXPECT_EQ(plan.runs[1].texture, kImage);
	EXPECT_EQ(plan.runs[1].indexStart, 6U);
	EXPECT_EQ(plan.runs[1].indexCount, 6U);
	EXPECT_EQ(plan.runs[2].texture, kAtlasA);
	EXPECT_EQ(batch.getPendingVertices().size(), 12U);
}

TEST(BatchPlanTest, ExplicitZSortsGroupsStablyBeforeSplitting) {
	BatchRenderer batch;
	batch.setFontAtlas(kAtlasA);
	addGlyph(batch, kAtlasA, BlendMode::Alpha, 1.0F);	 // vertices 0-3
	addGlyph(batch, kAtlasB, BlendMode::Additive, 0.0F); // vertices 4-7
	addRect(batch, BlendMode::Additive, 1.0F);			 // vertices 8-11
	addGlyph(batch, kAtlasB, BlendMode::Alpha, 0.0F);	 // vertices 12-15

	// z 0 first in submission order (glyph B, glyph B), then z 1 (glyph A, rect).
	const DrawPlan plan = batch.plan();
	ASSERT_EQ(plan.indices.size(), 24U);
	EXPECT_EQ(plan.indices[0], 4U);
	EXPECT_EQ(plan.indices[6], 12U);
	EXPECT_EQ(plan.indices[12], 0U);
	EXPECT_EQ(plan.indices[18], 8U);
	ASSERT_EQ(plan.runs.size(), 2U);
	EXPECT_EQ(plan.runs[0].texture, kAtlasB);
	EXPECT_EQ(plan.runs[0].indexCount, 12U);
	EXPECT_EQ(plan.runs[1].texture, kAtlasA);
	EXPECT_EQ(plan.runs[1].indexStart, 12U);
	EXPECT_EQ(plan.runs[1].indexCount, 12U);
}

// ============================================================================
// Arcs
// ============================================================================

TEST(BatchArcTest, LocalCoordinatesTurnTheArcsMiddleOntoPlusX) {
	BatchRenderer batch;
	const float	  quarter = std::numbers::pi_v<float> * 0.5F;
	batch.addArc({.center = {100.0F, 50.0F}, .innerRadius = 10.0F, .outerRadius = 20.0F, .startAngle = 0.0F, .sweep = quarter});

	const std::span<const UberVertex> vertices = batch.getPendingVertices();
	ASSERT_EQ(vertices.size(), 4U);
	EXPECT_NEAR(vertices[0].data2.x, 10.0F, kTolerance);
	EXPECT_NEAR(vertices[0].data2.y, 20.0F, kTolerance);
	EXPECT_NEAR(vertices[0].data2.z, quarter * 0.5F, kTolerance);
	EXPECT_EQ(vertices[0].data2.w, kRenderModeArc);
	// The quad reaches a pixel past the outer radius.
	EXPECT_NEAR(vertices[0].position.x, 79.0F, kTolerance);
	EXPECT_NEAR(vertices[0].position.y, 29.0F, kTolerance);
	// The bottom-right corner sits at 45 degrees (screen space, clockwise from +x),
	// the arc's middle, so it lands on +x.
	EXPECT_NEAR(vertices[2].texCoord.x, 21.0F * std::numbers::sqrt2_v<float>, kTolerance);
	EXPECT_NEAR(vertices[2].texCoord.y, 0.0F, kTolerance);
}

TEST(BatchArcTest, NegativeSweepRunsBackFromTheStart) {
	BatchRenderer batch;
	const float	  pi = std::numbers::pi_v<float>;
	// From 180 degrees back to 90: the middle is at 135, the bottom-left corner.
	batch.addArc({.center = {0.0F, 0.0F}, .outerRadius = 10.0F, .startAngle = pi, .sweep = -pi * 0.5F});

	const std::span<const UberVertex> vertices = batch.getPendingVertices();
	ASSERT_EQ(vertices.size(), 4U);
	EXPECT_NEAR(vertices[0].data2.z, pi * 0.25F, kTolerance);
	EXPECT_NEAR(vertices[3].texCoord.x, 11.0F * std::numbers::sqrt2_v<float>, kTolerance);
	EXPECT_NEAR(vertices[3].texCoord.y, 0.0F, kTolerance);
}

TEST(BatchArcTest, SweepClampsToTheFullRingAndEmptyArcsAddNothing) {
	BatchRenderer batch;
	batch.addArc({.center = {0.0F, 0.0F}, .outerRadius = 10.0F, .sweep = 20.0F});
	ASSERT_EQ(batch.getPendingVertices().size(), 4U);
	EXPECT_NEAR(batch.getPendingVertices()[0].data2.z, std::numbers::pi_v<float>, kTolerance);

	batch.addArc({.center = {0.0F, 0.0F}, .innerRadius = 10.0F, .outerRadius = 10.0F});
	batch.addArc({.center = {0.0F, 0.0F}, .outerRadius = 10.0F, .sweep = 0.0F});
	EXPECT_EQ(batch.getPendingVertices().size(), 4U);
}

// ============================================================================
// Stroke pieces
// ============================================================================

TEST(BatchStrokeTest, PiecesAreOrientedBoxesAndTrianglesFlatInOneGroup) {
	BatchRenderer	  batch;
	const StrokePiece pieces[2] = {
		{.center = {10.0F, 0.0F}, .axis = {0.0F, 1.0F}, .halfLength = 5.0F, .halfWidth = 2.0F},
		{.center = {0.0F, 0.0F}, .axis = {1.0F, 0.0F}, .halfLength = 2.0F, .halfWidth = 2.0F, .cornerRadius = 2.0F},
	};
	const Foundation::Vec2 wedge[3] = {{0.0F, 0.0F}, {4.0F, 0.0F}, {0.0F, 4.0F}};
	batch.addStroke(pieces, wedge, Foundation::Color::white(), 3.0F);
	addRect(batch); // z 0 sorts ahead of the whole stroke

	const std::span<const UberVertex> vertices = batch.getPendingVertices();
	ASSERT_EQ(vertices.size(), 8U + 3U + 4U);
	// Along the axis (down the screen) by halfLength + 1, across it by halfWidth + 1.
	EXPECT_NEAR(vertices[0].position.x, 13.0F, kTolerance);
	EXPECT_NEAR(vertices[0].position.y, -6.0F, kTolerance);
	EXPECT_NEAR(vertices[0].texCoord.x, -6.0F, kTolerance);
	EXPECT_NEAR(vertices[0].texCoord.y, -3.0F, kTolerance);
	EXPECT_EQ(vertices[0].data2, Foundation::Vec4(5.0F, 2.0F, 0.0F, 1.0F));
	EXPECT_EQ(vertices[4].data2, Foundation::Vec4(2.0F, 2.0F, 2.0F, 1.0F));
	// The wedge is a flat triangle: a zero-size shape, positions as given.
	EXPECT_EQ(vertices[8].data2, Foundation::Vec4(0.0F, 0.0F, 0.0F, 1.0F));
	EXPECT_EQ(vertices[9].position, Foundation::Vec2(4.0F, 0.0F));

	// One group: the rect sorts in front of it, and the stroke stays together after.
	const DrawPlan plan = batch.plan();
	ASSERT_EQ(plan.indices.size(), 6U + 12U + 3U);
	EXPECT_EQ(plan.indices[0], 11U);
	EXPECT_EQ(plan.indices[6], 0U);
	EXPECT_EQ(plan.indices[18], 8U);
	ASSERT_EQ(plan.runs.size(), 1U);
}
