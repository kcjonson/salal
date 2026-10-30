#include "primitives/BatchRenderer.h"
#include "BaseShader.h"
#include "CoordinateSystem/CoordinateSystem.h"
#include "backend/NullBackend.h"
#include "primitives/BoundaryLoops.h"
#include <array>
#include <glm/gtc/matrix_transform.hpp>
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

	const TextureHandle kAtlasA = TextureHandle::make(1, 0);
	const TextureHandle kAtlasB = TextureHandle::make(2, 0);
	const TextureHandle kImage = TextureHandle::make(3, 0);

	void addGlyph(BatchRenderer& batch, TextureHandle atlas, BlendMode blend = BlendMode::Alpha, float zIndex = 0.0F) {
		batch.addTextQuad(
			{0.0F, 0.0F}, {8.0F, 8.0F}, {0.0F, 0.0F}, {0.1F, 0.1F}, Foundation::Color::white(), {0.0F, 8.0F}, atlas, 4.0F, zIndex, blend
		);
	}

	void addRect(BatchRenderer& batch, BlendMode blend = BlendMode::Alpha, float zIndex = 0.0F) {
		batch.addQuad({0.0F, 0.0F, 10.0F, 10.0F}, Foundation::Color::red(), std::nullopt, 0.0F, std::nullopt, zIndex, blend);
	}

} // namespace

// ============================================================================
// BatchVertex Structure Tests
// ============================================================================

TEST(BatchVertexTest, SizeIsReasonable) {
	// position (8) + texCoord (8) + color (16) + data1 (16) + data2 (16) + clipBounds (16) = 80
	// However, Foundation types may vary - verify it's reasonable
	static_assert(sizeof(BatchVertex) <= 96, "BatchVertex should be <= 96 bytes");
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

TEST(RenderModeTest, AShapesBorderAlphaRidesInItsModesFractionAsTransparency) {
	backend::NullBackend gpu;
	BatchRenderer		 batch(gpu, base_shader::source());
	const auto			 rectWithBorder = [&](float alpha, Foundation::BorderPosition position, BlendMode blend) {
		  batch.addQuad(
			  {0.0F, 0.0F, 10.0F, 10.0F},
			  Foundation::Color::red(),
			  Foundation::BorderStyle{.color = {1.0F, 1.0F, 1.0F, alpha}, .width = 2.0F, .position = position},
			  0.0F,
			  std::nullopt,
			  0.0F,
			  blend
		  );
	};
	rectWithBorder(1.0F, Foundation::BorderPosition::Center, BlendMode::Alpha);
	rectWithBorder(0.4F, Foundation::BorderPosition::Center, BlendMode::Alpha);
	rectWithBorder(0.0F, Foundation::BorderPosition::Outside, BlendMode::Additive);
	addRect(batch);

	const std::span<const BatchVertex> vertices = batch.getPendingVertices();
	ASSERT_EQ(vertices.size(), 16U);
	EXPECT_EQ(vertices[0].data2.w, 1.0F); // opaque: the whole number
	EXPECT_FLOAT_EQ(vertices[4].data2.w, 1.0F + (kBorderTransparencySpan * 0.6F));
	EXPECT_EQ(vertices[8].data2.w, 6.0F + kBorderTransparencySpan); // still short of the next half unit
	EXPECT_EQ(vertices[12].data2.w, 1.0F);							// no border
	EXPECT_EQ(vertices[4].data1, Foundation::Vec4(1.0F, 1.0F, 1.0F, 2.0F));
}

TEST(RenderModeTest, AdditiveSharesAlphasBlendState) {
	EXPECT_EQ(blendState(BlendMode::Alpha), backend::BlendState::Alpha);
	EXPECT_EQ(blendState(BlendMode::Additive), backend::BlendState::Alpha);
}

TEST(RenderModeTest, EveryAddWritesItsModeAndBlend) {
	backend::NullBackend gpu;
	BatchRenderer		 batch(gpu, base_shader::source());
	addRect(batch, BlendMode::Additive);
	addGlyph(batch, kAtlasA, BlendMode::Additive);
	batch.addShadowQuad({0.0F, 0.0F, 10.0F, 10.0F}, Foundation::BoxShadow{}, 0.0F, 0.0F, BlendMode::Additive);
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
	batch.addStroke({&piece, 1}, Foundation::Color::white(), 0.0F, BlendMode::Additive);

	const std::span<const BatchVertex> vertices = batch.getPendingVertices();
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
	backend::NullBackend gpu;
	BatchRenderer		 batch(gpu, base_shader::source());
	const DrawPlan		 plan = batch.plan();
	EXPECT_TRUE(plan.indices.empty());
	EXPECT_TRUE(plan.runs.empty());
}

TEST(BatchPlanTest, InterleavedAlphaAndAdditiveOnOneAtlasIsOneRun) {
	backend::NullBackend gpu;
	BatchRenderer		 batch(gpu, base_shader::source());
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
	EXPECT_EQ(plan.runs[0].blendState, backend::BlendState::Alpha);
	EXPECT_EQ(plan.indices.size(), 36U);
}

TEST(BatchPlanTest, AtlasChangeStillSplits) {
	backend::NullBackend gpu;
	BatchRenderer		 batch(gpu, base_shader::source());
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
	backend::NullBackend gpu;
	BatchRenderer		 batch(gpu, base_shader::source());
	batch.setFontAtlas(kAtlasA);
	addRect(batch);
	addRect(batch, BlendMode::Additive);

	const DrawPlan plan = batch.plan();
	ASSERT_EQ(plan.runs.size(), 1U);
	EXPECT_EQ(plan.runs[0].texture, kAtlasA);
	EXPECT_EQ(plan.runs[0].indexCount, 12U);
}

TEST(BatchPlanTest, AnImageSplitsOnItsTexture) {
	backend::NullBackend gpu;
	BatchRenderer		 batch(gpu, base_shader::source());
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
	backend::NullBackend gpu;
	BatchRenderer		 batch(gpu, base_shader::source());
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

TEST(BatchPlanTest, ATextRunIsOneGroupHoweverManyGlyphs) {
	backend::NullBackend gpu;
	BatchRenderer		 batch(gpu, base_shader::source());
	batch.setFontAtlas(kAtlasA);
	addRect(batch, BlendMode::Alpha, 2.0F); // vertices 0-3
	for (int glyph = 0; glyph < 40; ++glyph) {
		addGlyph(batch, kAtlasA, BlendMode::Alpha, 1.0F); // vertices 4-163
	}
	EXPECT_EQ(batch.getDrawGroupCount(), 2U);

	// The next run at another blend, then one in a new sort domain, then a rect at
	// the second run's z: a group each.
	for (int glyph = 0; glyph < 3; ++glyph) {
		addGlyph(batch, kAtlasA, BlendMode::Additive, 1.0F);
	}
	batch.sortBarrier();
	for (int glyph = 0; glyph < 3; ++glyph) {
		addGlyph(batch, kAtlasA, BlendMode::Additive, 1.0F);
	}
	addRect(batch, BlendMode::Alpha, 1.0F);
	EXPECT_EQ(batch.getDrawGroupCount(), 5U);

	// The first run sorts as a whole, every glyph in order, then the additive run,
	// then the z 2 rect, all under the new domain's two groups.
	const DrawPlan plan = batch.plan();
	ASSERT_EQ(plan.indices.size(), (1U + 40U + 3U + 3U + 1U) * 6U);
	EXPECT_EQ(plan.indices[0], 4U);
	EXPECT_EQ(plan.indices[39 * 6], 160U);
	EXPECT_EQ(plan.indices[43 * 6], 0U);
	EXPECT_EQ(plan.runs.size(), 1U);
}

TEST(BatchPlanTest, ASortBarrierKeepsEarlierDrawsUnderLaterOnesWhateverTheirZ) {
	backend::NullBackend gpu;
	BatchRenderer		 batch(gpu, base_shader::source());
	batch.setFontAtlas(kAtlasA);
	addRect(batch, BlendMode::Alpha, 5.0F); // vertices 0-3
	addRect(batch, BlendMode::Alpha, 1.0F); // vertices 4-7
	batch.sortBarrier();
	addRect(batch, BlendMode::Alpha, -3.0F); // vertices 8-11
	addRect(batch, BlendMode::Alpha, -4.0F); // vertices 12-15

	// Each side z-sorted, the first side wholly under the second; still one run.
	const DrawPlan plan = batch.plan();
	ASSERT_EQ(plan.indices.size(), 24U);
	EXPECT_EQ(plan.indices[0], 4U);
	EXPECT_EQ(plan.indices[6], 0U);
	EXPECT_EQ(plan.indices[12], 12U);
	EXPECT_EQ(plan.indices[18], 8U);
	EXPECT_EQ(plan.runs.size(), 1U);
}

// ============================================================================
// Arcs
// ============================================================================

TEST(BatchArcTest, LocalCoordinatesTurnTheArcsMiddleOntoPlusX) {
	backend::NullBackend gpu;
	BatchRenderer		 batch(gpu, base_shader::source());
	const float			 quarter = std::numbers::pi_v<float> * 0.5F;
	batch.addArc({.center = {100.0F, 50.0F}, .innerRadius = 10.0F, .outerRadius = 20.0F, .startAngle = 0.0F, .sweep = quarter});

	const std::span<const BatchVertex> vertices = batch.getPendingVertices();
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
	backend::NullBackend gpu;
	BatchRenderer		 batch(gpu, base_shader::source());
	const float			 pi = std::numbers::pi_v<float>;
	// From 180 degrees back to 90: the middle is at 135, the bottom-left corner.
	batch.addArc({.center = {0.0F, 0.0F}, .outerRadius = 10.0F, .startAngle = pi, .sweep = -pi * 0.5F});

	const std::span<const BatchVertex> vertices = batch.getPendingVertices();
	ASSERT_EQ(vertices.size(), 4U);
	EXPECT_NEAR(vertices[0].data2.z, pi * 0.25F, kTolerance);
	EXPECT_NEAR(vertices[3].texCoord.x, 11.0F * std::numbers::sqrt2_v<float>, kTolerance);
	EXPECT_NEAR(vertices[3].texCoord.y, 0.0F, kTolerance);
}

TEST(BatchArcTest, SweepClampsToTheFullRingAndEmptyArcsAddNothing) {
	backend::NullBackend gpu;
	BatchRenderer		 batch(gpu, base_shader::source());
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

TEST(BatchStrokeTest, PiecesDrawTheirPolygonsAsFansInOneGroup) {
	backend::NullBackend gpu;
	BatchRenderer		 batch(gpu, base_shader::source());
	const StrokePiece	 pieces[2] = {
		   {.center = {10.0F, 0.0F},
			.axis = {0.0F, 1.0F},
			.halfLength = 5.0F,
			.halfWidth = 2.0F,
			.corners = {{{13.0F, -6.0F}, {13.0F, 6.0F}, {7.0F, 6.0F}, {6.0F, 0.0F}, {7.0F, -6.0F}}},
			.cornerCount = 5},
		   {.center = {0.0F, 0.0F}, .corners = {{{0.0F, 0.0F}, {4.0F, 0.0F}, {0.0F, 4.0F}}}, .cornerCount = 3},
	   };
	batch.addStroke(pieces, Foundation::Color::white(), 3.0F);
	addRect(batch); // z 0 sorts ahead of the whole stroke

	const std::span<const BatchVertex> vertices = batch.getPendingVertices();
	ASSERT_EQ(vertices.size(), 5U + 3U + 4U);
	// The corners as given; the SDF's coordinates run along the axis (down the screen)
	// and along its left normal, (-1, 0).
	EXPECT_EQ(vertices[0].position, Foundation::Vec2(13.0F, -6.0F));
	EXPECT_NEAR(vertices[0].texCoord.x, -6.0F, kTolerance);
	EXPECT_NEAR(vertices[0].texCoord.y, -3.0F, kTolerance);
	EXPECT_NEAR(vertices[3].texCoord.x, 0.0F, kTolerance);
	EXPECT_NEAR(vertices[3].texCoord.y, 4.0F, kTolerance);
	EXPECT_EQ(vertices[0].data2, Foundation::Vec4(5.0F, 2.0F, 0.0F, 1.0F));
	// A zero-size box is flat.
	EXPECT_EQ(vertices[5].data2, Foundation::Vec4(0.0F, 0.0F, 0.0F, 1.0F));
	EXPECT_EQ(vertices[6].position, Foundation::Vec2(4.0F, 0.0F));

	// One group, each polygon a fan from its first corner: the rect sorts in front of
	// it, and the stroke stays together after.
	const DrawPlan				plan = batch.plan();
	const std::vector<uint32_t> expected = {8, 9, 10, 8, 10, 11, 0, 1, 2, 0, 2, 3, 0, 3, 4, 5, 6, 7};
	EXPECT_EQ(std::vector<uint32_t>(plan.indices.begin(), plan.indices.end()), expected);
	ASSERT_EQ(plan.runs.size(), 1U);
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

	void expectPosition(const BatchVertex& vertex, Foundation::Vec2 position) {
		EXPECT_NEAR(vertex.position.x, position.x, kTolerance);
		EXPECT_NEAR(vertex.position.y, position.y, kTolerance);
	}

} // namespace

TEST(BatchFeatherTest, ARingOneDevicePixelWideRunsOutsideTheBoundaryFadingToTransparent) {
	backend::NullBackend gpu;
	BatchRenderer		 batch(gpu, base_shader::source());
	const BoundaryLoops	 loops = squareLoops();
	addSquare(batch, &loops);

	// The fill's four vertices, then one outer vertex per boundary vertex on its miter,
	// a pixel from both edges: the corners a pixel out diagonally.
	const std::span<const BatchVertex> vertices = batch.getPendingVertices();
	ASSERT_EQ(vertices.size(), 8U);
	expectPosition(vertices[4], {-1.0F, -1.0F});
	expectPosition(vertices[5], {11.0F, -1.0F});
	expectPosition(vertices[6], {11.0F, 11.0F});
	expectPosition(vertices[7], {-1.0F, 11.0F});
	for (size_t i = 0; i < 8; ++i) {
		const float alpha = i < 4 ? 0.8F : 0.0F;
		EXPECT_EQ(vertices[i].color, Foundation::Vec4(1.0F, 0.5F, 0.25F, alpha)) << "vertex " << i;
		EXPECT_EQ(vertices[i].data2, Foundation::Vec4(0.0F, 0.0F, 0.0F, 1.0F)) << "vertex " << i; // flat
	}

	// A quad per boundary edge, from the fill's own vertices out; one group, one run.
	const DrawPlan				plan = batch.plan();
	const std::vector<uint32_t> expected = {0, 1, 2, 0, 2, 3, 0, 1, 5, 0, 5, 4, 1, 2, 6, 1, 6, 5, 2, 3, 7, 2, 7, 6, 3, 0, 4, 3, 4, 7};
	EXPECT_EQ(std::vector<uint32_t>(plan.indices.begin(), plan.indices.end()), expected);
	EXPECT_EQ(plan.runs.size(), 1U);
}

TEST(BatchFeatherTest, TheRingIsOneDevicePixelAtTheCoordinateSystemsRatio) {
	backend::NullBackend gpu;
	BatchRenderer		 batch(gpu, base_shader::source());
	CoordinateSystem	 coordinates;
	coordinates.setViewport({.logicalWidth = 100, .logicalHeight = 100, .framebufferWidth = 200, .framebufferHeight = 200});
	batch.setCoordinateSystem(&coordinates);
	const BoundaryLoops loops = squareLoops();
	addSquare(batch, &loops);
	const std::span<const BatchVertex> vertices = batch.getPendingVertices();
	ASSERT_EQ(vertices.size(), 8U);
	expectPosition(vertices[4], {-0.5F, -0.5F});
	expectPosition(vertices[6], {10.5F, 10.5F});
}

TEST(BatchFeatherTest, TheRingRunsOutwardThroughAMirroringTransform) {
	backend::NullBackend gpu;
	BatchRenderer		 batch(gpu, base_shader::source());
	batch.setTransform(glm::scale(Foundation::Mat4(1.0F), Foundation::Vec3(-2.0F, 1.0F, 1.0F)));
	const BoundaryLoops loops = squareLoops();
	addSquare(batch, &loops);
	// The square lands on x from -20 to 0; a pixel is a pixel on screen.
	const std::span<const BatchVertex> vertices = batch.getPendingVertices();
	ASSERT_EQ(vertices.size(), 8U);
	expectPosition(vertices[4], {1.0F, -1.0F});
	expectPosition(vertices[5], {-21.0F, -1.0F});
	expectPosition(vertices[6], {-21.0F, 11.0F});
}

TEST(BatchFeatherTest, EachBoundaryVertexFadesItsOwnColour) {
	backend::NullBackend				   gpu;
	BatchRenderer						   batch(gpu, base_shader::source());
	const BoundaryLoops					   loops = squareLoops();
	const std::array<Foundation::Color, 4> colors = {
		Foundation::Color::red(), Foundation::Color::green(), Foundation::Color::blue(), Foundation::Color(1.0F, 1.0F, 1.0F, 0.5F)
	};
	addSquare(batch, &loops, colors.data());
	const std::span<const BatchVertex> vertices = batch.getPendingVertices();
	ASSERT_EQ(vertices.size(), 8U);
	for (size_t i = 0; i < 4; ++i) {
		const Foundation::Vec4 own = colors[i].toVec4();
		EXPECT_EQ(vertices[4 + i].color, Foundation::Vec4(own.r, own.g, own.b, 0.0F)) << "vertex " << i;
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
	const std::span<const BatchVertex> vertices = batch.getPendingVertices();
	ASSERT_EQ(vertices.size(), 6U);
	// The tip's miter would be 100 pixels long.
	EXPECT_NEAR(glm::length(vertices[4].position - spike[1]), 4.0F, 1e-4F);
	EXPECT_GT(vertices[4].position.x, 100.0F);
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
	EXPECT_EQ(batch.getPendingVertices().size(), 8U);
	EXPECT_EQ(batch.plan().indices.size(), 12U);
}
