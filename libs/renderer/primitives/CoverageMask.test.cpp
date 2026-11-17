// Masks with partial coverage (docs/masks.md): a content layer's shapes, their values combined by
// mix-blend-mode, groups isolated by it, over a mask's rect, polygon and mesh layers. What reaches the
// backend, headless on the null backend; and the value test of the player visibility cover's cases
// (worldsim's sight-softening design, 7.7), drawn through Primitives into a RenderTarget on a hidden
// GL 3.3 core context and read back against the values its rules give (skipped where no context can
// be made). No stored image: every expected value is computed.

#include "primitives/Primitives.h"

#include "BaseShader.h"
#include "backend/NullBackend.h"
#include "backend/gl/GLBackend.h"
#include "primitives/BatchRenderer.h"
#include "random/Rng.h"
#include "resources/RenderTarget.h"
#include "utils/Log.h"
#include "vector/Types.h"

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <numbers>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace Renderer {

	using Foundation::Vec2;
	using Primitives::MaskBlend;
	using Primitives::MaskComposite;
	using Primitives::MaskImage;
	using Primitives::MaskLayer;
	using Primitives::MaskMesh;
	using Primitives::MaskPaint;
	using Primitives::MaskShape;

	namespace {
		constexpr float kFeather = 12.0F; // sightFeatherMm, in pixels

		// The fade of 7.1: 1 at an area's edge, 0 a feather in.
		float fade(float depth) {
			return std::max(0.0F, 1.0F - (depth / kFeather));
		}

		float cross(Vec2 first, Vec2 second) {
			return (first.x * second.y) - (first.y * second.x);
		}

		float distanceToSegment(Vec2 point, Vec2 start, Vec2 end) {
			const Vec2	along = end - start;
			const float lengthSquared = glm::dot(along, along);
			const float share = lengthSquared > 0.0F ? std::clamp(glm::dot(point - start, along) / lengthSquared, 0.0F, 1.0F) : 0.0F;
			return glm::length(point - (start + (along * share)));
		}

		// Nonzero winding, as the mask fills a polygon.
		bool insidePolygon(std::span<const Vec2> outline, Vec2 point) {
			int winding = 0;
			for (size_t index = 0; index < outline.size(); ++index) {
				const Vec2	start = outline[index];
				const Vec2	end = outline[(index + 1) % outline.size()];
				const float side = cross(end - start, point - start);
				if (start.y <= point.y && end.y > point.y && side > 0.0F) {
					++winding;
				} else if (start.y > point.y && end.y <= point.y && side < 0.0F) {
					--winding;
				}
			}
			return winding != 0;
		}

		// A piece of darkness or of the unseen fade: a triangle and a value at each corner, linear
		// between them.
		struct Piece {
			std::array<Vec2, 3>	 corners;
			std::array<float, 3> values;

			// The piece's value at `point`, or nothing outside it.
			std::optional<float> at(Vec2 point) const {
				const float area = cross(corners[1] - corners[0], corners[2] - corners[0]);
				if (area == 0.0F) {
					return std::nullopt;
				}
				const float first = cross(corners[1] - point, corners[2] - point) / area;
				const float second = cross(corners[2] - point, corners[0] - point) / area;
				const float third = 1.0F - first - second;
				// A point on an edge counts as inside, though rounding puts it a hair out.
				constexpr float kOnEdge = -1e-5F;
				if (first < kOnEdge || second < kOnEdge || third < kOnEdge) {
					return std::nullopt;
				}
				return (first * values[0]) + (second * values[1]) + (third * values[2]);
			}
		};

		// The two-stop gradient whose value is the piece's linear field: from its least corner to
		// its greatest along the field's slope, so the padding past each end is never reached inside
		// the triangle (7.6: a field linear over a triangle is one two-stop linear gradient).
		MaskPaint pieceGradient(const Piece& piece) {
			const Vec2	first = piece.corners[1] - piece.corners[0];
			const Vec2	second = piece.corners[2] - piece.corners[0];
			const float rise1 = piece.values[1] - piece.values[0];
			const float rise2 = piece.values[2] - piece.values[0];
			const float determinant = cross(first, second);
			const Vec2	slope = Vec2((rise1 * second.y) - (rise2 * first.y), (rise2 * first.x) - (rise1 * second.x)) / determinant;
			const float slopeSquared = glm::dot(slope, slope);
			if (slopeSquared < 1e-12F) {
				return MaskPaint::flat(piece.values[0]);
			}
			const auto	least = static_cast<size_t>(std::min_element(piece.values.begin(), piece.values.end()) - piece.values.begin());
			const float lowest = piece.values[least];
			const float highest = *std::max_element(piece.values.begin(), piece.values.end());
			const Vec2	from = piece.corners[least];
			return MaskPaint::linear(from, from + (slope * ((highest - lowest) / slopeSquared)), lowest, highest);
		}

		// The rectangle a feather wide inward from `start` to `end`, 1 on the edge and 0 on its far
		// side (7.2), as two pieces. `inward` is the unit normal into the area.
		void edgePieces(Vec2 start, Vec2 end, Vec2 inward, std::vector<Piece>& pieces) {
			const Vec2 farStart = start + (inward * kFeather);
			const Vec2 farEnd = end + (inward * kFeather);
			pieces.push_back({{start, end, farEnd}, {1.0F, 1.0F, 0.0F}});
			pieces.push_back({{start, farEnd, farStart}, {1.0F, 0.0F, 0.0F}});
		}

		// A reflex corner's fan, from direction `fromAngle` to `toAngle` (radians, the short way
		// through the area), 1 at the corner and 0 on the arc, in triangles of at most 15 degrees.
		void fanPieces(Vec2 corner, float fromAngle, float toAngle, std::vector<Piece>& pieces) {
			constexpr float kStep = 15.0F * std::numbers::pi_v<float> / 180.0F;
			const int		steps = std::max(1, static_cast<int>(std::ceil(std::abs(toAngle - fromAngle) / kStep)));
			for (int step = 0; step < steps; ++step) {
				const float first = fromAngle + ((toAngle - fromAngle) * static_cast<float>(step) / static_cast<float>(steps));
				const float second = fromAngle + ((toAngle - fromAngle) * static_cast<float>(step + 1) / static_cast<float>(steps));
				pieces.push_back(
					{{corner,
					  corner + (Vec2(std::cos(first), std::sin(first)) * kFeather),
					  corner + (Vec2(std::cos(second), std::sin(second)) * kFeather)},
					 {1.0F, 0.0F, 0.0F}}
				);
			}
		}

		// The unit normal into an outline wound either way, of its edge from `start` to `end`.
		Vec2 inwardNormal(std::span<const Vec2> outline, Vec2 start, Vec2 end) {
			float area = 0.0F;
			for (size_t index = 0; index < outline.size(); ++index) {
				area += cross(outline[index], outline[(index + 1) % outline.size()]);
			}
			const Vec2 along = glm::normalize(end - start);
			return area > 0.0F ? Vec2(-along.y, along.x) : Vec2(along.y, -along.x);
		}

		// A colonist's cover (7.1, 7.4): his bright shape, and the pieces of his darkness inside it,
		// his feather on the sides that are rim and his drawn shadows.
		struct Colonist {
			std::vector<Vec2>  bright;
			std::vector<Piece> pieces;

			// 7.1: 1 outside his bright shape, inside it the greatest of his pieces over 0.
			float darkness(Vec2 point) const {
				if (!insidePolygon(bright, point)) {
					return 1.0F;
				}
				float greatest = 0.0F;
				for (const Piece& piece : pieces) {
					greatest = std::max(greatest, piece.at(point).value_or(0.0F));
				}
				return greatest;
			}

			// Feather pieces on every side of the bright shape for which `rim` holds.
			template <typename Rim>
			void featherRim(Rim rim) {
				for (size_t index = 0; index < bright.size(); ++index) {
					const Vec2 start = bright[index];
					const Vec2 end = bright[(index + 1) % bright.size()];
					if (rim(index)) {
						edgePieces(start, end, inwardNormal(bright, start, end), pieces);
					}
				}
			}
		};

		std::vector<Vec2> circle(Vec2 centre, float radius, int sides) {
			std::vector<Vec2> outline;
			for (int side = 0; side < sides; ++side) {
				const float angle = 2.0F * std::numbers::pi_v<float> * static_cast<float>(side) / static_cast<float>(sides);
				outline.push_back(centre + (Vec2(std::cos(angle), std::sin(angle)) * radius));
			}
			return outline;
		}

		// A soft body's triangle behind a trunk `away` from the eye, `strength` at its foot and 0 at its
		// tip, and a fin each side falling from the triangle's value to 0 outward (3.2).
		void softShadow(Vec2 foot, Vec2 away, float width, float length, float strength, std::vector<Piece>& pieces) {
			const Vec2 across(-away.y, away.x);
			const Vec2 left = foot + (across * (width * 0.5F));
			const Vec2 right = foot - (across * (width * 0.5F));
			const Vec2 tip = foot + (away * length);
			pieces.push_back({{left, right, tip}, {strength, strength, 0.0F}});
			pieces.push_back({{left, tip, left + (across * 6.0F) + (away * length * 0.6F)}, {strength, 0.0F, 0.0F}});
			pieces.push_back({{right, right - (across * 6.0F) + (away * length * 0.6F), tip}, {strength, 0.0F, 0.0F}});
		}

		// An opaque body's shadow edge from its corner along `ray` to the rim, on its lit side
		// `litSide` (a unit normal to the ray): a band at full darkness, then a fin falling to 0
		// (3.1). The band and fin reach past the rim; the colonist's own mask clips them.
		void hardShadowEdge(Vec2 corner, Vec2 ray, Vec2 litSide, float reach, std::vector<Piece>& pieces) {
			const Vec2 rim = corner + (ray * reach);
			const Vec2 bandEdge = rim + (litSide * reach * 0.07F);
			const Vec2 finEdge = rim + (litSide * reach * 0.21F);
			pieces.push_back({{corner, rim, bandEdge}, {1.0F, 1.0F, 1.0F}});
			pieces.push_back({{corner, bandEdge, finEdge}, {1.0F, 1.0F, 0.0F}});
		}

		// The pieces of `pieces` as content shapes under `ownMask`, combined by lighten, their outlines
		// kept in `outlines`, which must hold room for them all (the shapes point into it).
		void appendShapes(
			std::span<const Piece>			  pieces,
			std::span<const MaskLayer>		  ownMask,
			std::vector<std::array<Vec2, 3>>& outlines,
			std::vector<MaskShape>&			  shapes
		) {
			for (const Piece& piece : pieces) {
				outlines.push_back(piece.corners);
				shapes.push_back({.points = outlines.back(), .paint = pieceGradient(piece), .mask = ownMask, .blend = MaskBlend::Lighten});
			}
		}
	} // namespace

	// --- On the null backend ---

	class CoverageBatchTest : public ::testing::Test {
	  protected:
		void SetUp() override {
			auto owned = std::make_unique<backend::NullBackend>();
			gpu = owned.get();
			Primitives::init(std::move(owned), base_shader::source());
			Primitives::setCoordinateSystem(nullptr);
			Primitives::setViewport(64, 64);
			Primitives::beginFrame();
			gpu->clearRecord();
		}

		void TearDown() override { Primitives::shutdown(); }

		static void fill(std::span<const MaskLayer> mask) {
			Primitives::drawRect({.bounds = {0.0F, 0.0F, 64.0F, 64.0F}, .style = {.fill = Foundation::Color::white()}, .mask = mask});
		}

		// The errors naming `reason` since the sink was set.
		struct RefusalLog {
			const char* reason = nullptr;
			int			count = 0;
		};

		static void countRefusals(void* context, const foundation::LogRecord& record) {
			auto* log = static_cast<RefusalLog*>(context);
			if (record.level == foundation::LogLevel::Error && std::strstr(record.message, log->reason) != nullptr) {
				++log->count;
			}
		}

		// `draw` twice, refused both times: nothing drawn, and one error naming `reason`.
		template <typename Draw>
		void expectRefusedOnce(Draw draw, const char* reason) {
			RefusalLog log{.reason = reason};
			foundation::Logger::setSink(countRefusals, &log);
			draw();
			draw();
			Primitives::endFrame();
			foundation::Logger::setSink(nullptr, nullptr);
			EXPECT_TRUE(gpu->draws().empty());
			EXPECT_EQ(log.count, 1) << reason;
		}

		size_t drawsWith(backend::BlendState blend, backend::ChannelWrites writes) const {
			return static_cast<size_t>(std::count_if(gpu->draws().begin(), gpu->draws().end(), [&](const auto& draw) {
				return draw.call.blend == blend && draw.call.writes == writes;
			}));
		}

		backend::NullBackend* gpu = nullptr;
	};

	TEST_F(CoverageBatchTest, AGroupKeepsItsGreatestInDepthThenWritesItWhereItMatches) {
		const std::array<Vec2, 4>	   view = {{{0.0F, 0.0F}, {64.0F, 0.0F}, {64.0F, 64.0F}, {0.0F, 64.0F}}};
		const std::array<Vec2, 3>	   shape = {{{4.0F, 4.0F}, {40.0F, 4.0F}, {4.0F, 40.0F}}};
		const std::array<Vec2, 3>	   piece = {{{4.0F, 4.0F}, {20.0F, 4.0F}, {4.0F, 20.0F}}};
		const std::array<MaskLayer, 1> insideShape = {{{.image = MaskImage::polygon(shape)}}};
		MaskShape					   group{.blend = MaskBlend::Darken};
		group.children.push_back({.points = shape, .paint = MaskPaint::flat(0.0F)});
		group.children.push_back({.points = piece, .paint = MaskPaint::flat(0.5F), .mask = insideShape, .blend = MaskBlend::Lighten});
		const std::vector<MaskShape> content = {
			{.points = view, .paint = MaskPaint::flat(1.0F)},
			group,
			group,
			{.points = piece, .paint = MaskPaint::flat(0.25F), .blend = MaskBlend::Lighten},
		};
		const std::array<MaskLayer, 1> mask = {{{.image = MaskImage::content(content)}}};
		fill(mask);
		Primitives::endFrame();

		EXPECT_EQ(gpu->depthClears().size(), 1U); // once for both groups
		size_t greaterOrEqual = 0;
		size_t equal = 0;
		size_t lighten = 0;
		for (const auto& draw : gpu->draws()) {
			greaterOrEqual += draw.call.depth == backend::DepthState::GreaterOrEqual ? 1 : 0;
			equal += draw.call.depth == backend::DepthState::Equal ? 1 : 0;
			if (draw.call.depth == backend::DepthState::Equal) {
				EXPECT_EQ(draw.call.blend, backend::BlendState::Min); // the group's darken
			}
			if (draw.call.depth == backend::DepthState::Off && draw.call.blend == backend::BlendState::Max) {
				++lighten; // the shape outside a group, by its lighten
				EXPECT_EQ(draw.call.writes, backend::ChannelWrites::Alpha);
			}
			if (draw.call.depth != backend::DepthState::Off) {
				EXPECT_GT(draw.uniforms.coverageDepth.y, 0.0F);
			}
			// Every shape draws only over the cover box.
			const bool shape = draw.call.depth != backend::DepthState::Off || draw.call.blend == backend::BlendState::Max ||
							   (draw.call.blend == backend::BlendState::Replace && draw.call.stencil.writeMask == 0 &&
								draw.call.writes == backend::ChannelWrites::Alpha && draw.call.stencil.readMask != kMaskAccumulatorBit);
			if (shape) {
				EXPECT_TRUE(draw.call.stencil.test);
				EXPECT_EQ(draw.call.stencil.function, backend::StencilFunction::Equal);
				EXPECT_EQ(draw.call.stencil.readMask & kCoverBoxBit, kCoverBoxBit);
				EXPECT_EQ(draw.call.stencil.reference & kCoverBoxBit, kCoverBoxBit);
			}
		}
		EXPECT_EQ(greaterOrEqual, 4U); // each group's two shapes, keeping the greatest
		EXPECT_EQ(equal, 4U);		   // and again, writing it
		EXPECT_EQ(lighten, 1U);
		// The fill: alpha weighted, then the colour over by it, then the alpha set back.
		EXPECT_EQ(drawsWith(backend::BlendState::MultiplyDestinationAlpha, backend::ChannelWrites::Alpha), 1U);
		EXPECT_EQ(drawsWith(backend::BlendState::OverByDestinationAlpha, backend::ChannelWrites::Colour), 1U);
		// A shape's own mask is built in its own bits, cleared over its box, not across the target.
		for (const auto& clear : gpu->stencilClears()) {
			EXPECT_EQ(clear.bits & (kShapeMaskAccumulatorBit | kShapeMaskScratchBit), 0);
		}
	}

	TEST_F(CoverageBatchTest, MasksSalalDoesNotEvaluateDrawNothing) {
		const std::array<Vec2, 3>	 triangle = {{{4.0F, 4.0F}, {40.0F, 4.0F}, {4.0F, 40.0F}}};
		const std::vector<MaskShape> content = {{.points = triangle, .paint = MaskPaint::flat(1.0F)}};
		// A content layer under another layer.
		const std::array<MaskLayer, 2> underneath = {
			{{.image = MaskImage::rect({0.0F, 0.0F, 8.0F, 8.0F})}, {.image = MaskImage::content(content)}}
		};
		fill(underneath);
		// A group's shapes after its first combined by darken.
		MaskShape group{.blend = MaskBlend::Lighten};
		group.children.push_back({.points = triangle});
		group.children.push_back({.points = triangle, .blend = MaskBlend::Darken});
		const std::vector<MaskShape>   darkened = {group};
		const std::array<MaskLayer, 1> darkenedMask = {{{.image = MaskImage::content(darkened)}}};
		fill(darkenedMask);
		Primitives::endFrame();
		EXPECT_TRUE(gpu->draws().empty());
	}

	TEST_F(CoverageBatchTest, AShapeWithBothPointsAndAMeshIsRefusedAndLoggedOnce) {
		const std::array<Vec2, 3> triangle = {{{4.0F, 4.0F}, {40.0F, 4.0F}, {4.0F, 40.0F}}};
		renderer::TessellatedMesh triangleMesh;
		triangleMesh.vertices = {triangle.begin(), triangle.end()};
		triangleMesh.indices = {0, 1, 2};
		triangleMesh.colors.assign(3, Foundation::Color::white());
		InstancedMeshHandle			   uploaded = Primitives::uploadInstancedMesh(triangleMesh, 1);
		const std::vector<MaskShape>   both = {{.points = triangle, .mesh = {uploaded, Primitives::WorldCamera{}}}};
		const std::array<MaskLayer, 1> mask = {{{.image = MaskImage::content(both)}}};
		expectRefusedOnce([&] { fill(mask); }, "a shape has both points and a mesh");
		Primitives::releaseInstancedMesh(uploaded);
	}

	TEST_F(CoverageBatchTest, AGroupCarryingAPaintIsRefusedAndLoggedOnce) {
		const std::array<Vec2, 3> triangle = {{{4.0F, 4.0F}, {40.0F, 4.0F}, {4.0F, 40.0F}}};
		MaskShape				  painted{.paint = MaskPaint::flat(0.5F)};
		painted.children.push_back({.points = triangle, .paint = MaskPaint::flat(1.0F)});
		const std::vector<MaskShape>   content = {painted};
		const std::array<MaskLayer, 1> mask = {{{.image = MaskImage::content(content)}}};
		expectRefusedOnce([&] { fill(mask); }, "a group carries a paint");
	}

	TEST_F(CoverageBatchTest, AnAdditiveRectUnderAContentLayerIsRefusedAndLoggedOnce) {
		const std::array<Vec2, 3>	   triangle = {{{4.0F, 4.0F}, {40.0F, 4.0F}, {4.0F, 40.0F}}};
		const std::vector<MaskShape>   content = {{.points = triangle, .paint = MaskPaint::flat(1.0F)}};
		const std::array<MaskLayer, 1> mask = {{{.image = MaskImage::content(content)}}};
		expectRefusedOnce(
			[&] {
				Primitives::drawRect(
					{.bounds = {0.0F, 0.0F, 64.0F, 64.0F},
					 .style = {.fill = Foundation::Color::white()},
					 .mask = mask,
					 .blend = Primitives::BlendMode::Additive}
				);
			},
			"an additive rect under a content layer"
		);
	}

	TEST_F(CoverageBatchTest, ATargetWithNoAlphaOrDepthDrawsAContentMaskAsNothing) {
		const std::array<Vec2, 3>	   triangle = {{{4.0F, 4.0F}, {40.0F, 4.0F}, {4.0F, 40.0F}}};
		const std::vector<MaskShape>   content = {{.points = triangle, .paint = MaskPaint::flat(1.0F)}};
		const std::array<MaskLayer, 1> mask = {{{.image = MaskImage::content(content)}}};
		gpu->setWindowAlphaAndDepth(false);
		fill(mask);
		Primitives::drawRect({.bounds = {0.0F, 0.0F, 4.0F, 4.0F}, .style = {.fill = Foundation::Color::red()}});
		Primitives::endFrame();
		size_t colour = 0;
		for (const auto& draw : gpu->draws()) {
			colour += draw.call.writes == backend::ChannelWrites::All ? 1 : 0;
			EXPECT_NE(draw.call.blend, backend::BlendState::OverByDestinationAlpha);
		}
		EXPECT_EQ(colour, 1U); // the unmasked rect alone
	}

	// --- Values on GL ---

	class CoverageDrawTest : public ::testing::Test {
	  protected:
		static constexpr int kSize = 512;

		void SetUp() override {
			if (glfwInit() != GLFW_TRUE) {
				GTEST_SKIP() << "GLFW initialization failed (no display available in CI)";
			}
			glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
			glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
			glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
			glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
			glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
			window = glfwCreateWindow(32, 32, "offscreen", nullptr, nullptr);
			if (window == nullptr) {
				glfwTerminate();
				GTEST_SKIP() << "Could not create GLFW window (no OpenGL context available)";
			}
			glfwMakeContextCurrent(window);
			std::unique_ptr<backend::gl::GLBackend> created = backend::gl::GLBackend::create();
			if (created == nullptr) {
				glfwDestroyWindow(window);
				window = nullptr;
				glfwTerminate();
				GTEST_SKIP() << "GLEW initialization failed";
			}
			Primitives::init(std::move(created), base_shader::source());
			Primitives::setCoordinateSystem(nullptr);
			Primitives::setViewport(kSize, kSize);
			target = RenderTarget::create(kSize, kSize, TextureFormat::Rgba8, RenderTarget::Attachments::ColourAndStencil);
			ASSERT_TRUE(target.isValid());
		}

		void TearDown() override {
			if (window != nullptr) {
				target = RenderTarget();
				Primitives::shutdown();
				glfwDestroyWindow(window);
				glfwTerminate();
			}
		}

		// The target cleared black, a white rect over all of it under `mask`, read back: each
		// pixel's red is the mask's value there, times 255.
		void render(std::span<const MaskLayer> mask) {
			target.begin();
			Primitives::setViewport(kSize, kSize);
			Primitives::clear(Foundation::Color(0.0F, 0.0F, 0.0F, 1.0F));
			Primitives::beginFrame();
			Primitives::drawRect({.bounds = kView, .style = {.fill = Foundation::Color::white()}, .mask = mask});
			Primitives::endFrame();
			target.end();
			pixels.assign(static_cast<size_t>(kSize) * kSize * 4, 0);
			target.readPixels(pixels);
		}

		// The mask's value at the pixel holding `point`, y down; readPixels rows run bottom up.
		float value(Vec2 point) const {
			const int	 x = static_cast<int>(std::floor(point.x));
			const int	 y = static_cast<int>(std::floor(point.y));
			const size_t index = ((static_cast<size_t>(kSize - 1 - y) * kSize) + static_cast<size_t>(x)) * 4;
			return static_cast<float>(pixels[index]) / 255.0F;
		}

		std::array<int, 4> rgba(int x, int y) const {
			const size_t index = ((static_cast<size_t>(kSize - 1 - y) * kSize) + static_cast<size_t>(x)) * 4;
			return {pixels[index], pixels[index + 1], pixels[index + 2], pixels[index + 3]};
		}

		// The alpha is opaque at every pixel: the rect covers the whole target, so its cover box does.
		void expectOpaque(const char* what) const {
			int failures = 0;
			for (int y = 0; y < kSize; ++y) {
				for (int x = 0; x < kSize; ++x) {
					const int alpha = rgba(x, y)[3];
					if (alpha != 255 && ++failures <= 8) {
						ADD_FAILURE() << what << ": alpha " << alpha << " at (" << x << ", " << y << ")";
					}
				}
			}
			EXPECT_EQ(failures, 0) << what;
		}

		// Pixel centres one pixel apart from `from` along `step`, `count` of them, each against `expected`,
		// and each step between neighbours against the step the rules give: a seam is a step they don't.
		template <typename Expected>
		void expectProbe(Expected expected, Vec2 from, Vec2 step, int count, const char* what) const {
			for (int index = 0; index < count; ++index) {
				const Vec2 point = from + (step * static_cast<float>(index));
				EXPECT_NEAR(value(point), expected(point), kTolerance) << what << " at (" << point.x << ", " << point.y << ")";
				if (index > 0) {
					const Vec2 previous = point - step;
					EXPECT_NEAR(value(point) - value(previous), expected(point) - expected(previous), kStepTolerance)
						<< what << ": a step between (" << previous.x << ", " << previous.y << ") and (" << point.x << ", " << point.y
						<< ")";
				}
			}
		}

		// Segments in cells of 16 px, so a pixel tests only the few near it for being within 0.6 px
		// of one, where the drawn and the computed sides of an edge may differ by a pixel.
		class EdgeGrid {
		  public:
			explicit EdgeGrid(std::span<const std::pair<Vec2, Vec2>> edges)
				: edges(edges),
				  cells(static_cast<size_t>(kCells) * kCells) {
				for (size_t index = 0; index < edges.size(); ++index) {
					const auto& [start, end] = edges[index];
					const int left = cell(std::min(start.x, end.x) - 1.0F);
					const int right = cell(std::max(start.x, end.x) + 1.0F);
					const int top = cell(std::min(start.y, end.y) - 1.0F);
					const int bottom = cell(std::max(start.y, end.y) + 1.0F);
					for (int row = top; row <= bottom; ++row) {
						for (int column = left; column <= right; ++column) {
							cells[(static_cast<size_t>(row) * kCells) + column].push_back(index);
						}
					}
				}
			}

			bool nearEdge(Vec2 point) const {
				return nearEdgeWhere(point, [](Vec2, Vec2) { return true; });
			}

			// Whether `point` is within 0.6 px of an edge from start to end for which `accept(start, end)` holds.
			template <typename Accept>
			bool nearEdgeWhere(Vec2 point, Accept accept) const {
				const std::vector<size_t>& near = cells[(static_cast<size_t>(cell(point.y)) * kCells) + cell(point.x)];
				return std::any_of(near.begin(), near.end(), [&](size_t edge) {
					const auto& [start, end] = edges[edge];
					return distanceToSegment(point, start, end) < 0.6F && accept(start, end);
				});
			}

		  private:
			static constexpr int kCell = 16;
			static constexpr int kCells = kSize / kCell;

			static int cell(float coordinate) { return std::clamp(static_cast<int>(std::floor(coordinate / kCell)), 0, kCells - 1); }

			std::span<const std::pair<Vec2, Vec2>> edges;
			std::vector<std::vector<size_t>>	   cells;
		};

		// Every pixel centre against `expected`, but those near an edge of `grid`.
		template <typename Expected>
		void expectEveryPixel(Expected expected, const EdgeGrid& grid, const char* what) {
			int checked = 0;
			int failures = 0;
			for (int y = 0; y < kSize; ++y) {
				for (int x = 0; x < kSize; ++x) {
					const Vec2 point(static_cast<float>(x) + 0.5F, static_cast<float>(y) + 0.5F);
					if (grid.nearEdge(point)) {
						continue;
					}
					++checked;
					const float want = expected(point);
					const float got = value(point);
					if (std::abs(got - want) > kTolerance && ++failures <= 8) {
						ADD_FAILURE() << what << ": pixel (" << x << ", " << y << ") holds " << got << ", the rules give " << want;
					}
				}
			}
			EXPECT_EQ(failures, 0) << what << ": " << failures << " of " << checked << " pixels off";
			EXPECT_GT(checked, 1000) << what;
		}

		// 8-bit values at the pieces' corners and in the target, and interpolation between.
		static constexpr float kTolerance = 3.0F / 255.0F;

		// A step between neighbouring pixels against the rules' step: a seam a pixel wide is more.
		static constexpr float kStepTolerance = 1.5F / 255.0F;

		// Whether the rules' value jumps across the line through `point` along `normal`, a unit vector,
		// as at a hard shadow side, a wall, or a piece's edge where nothing beside it carries its value.
		template <typename Expected>
		static bool stepsByTheRules(Expected expected, Vec2 point, Vec2 normal) {
			return std::abs(expected(point + (normal * 0.01F)) - expected(point - (normal * 0.01F))) > kStepTolerance;
		}

		// Across the edge from `start` to `end` at `samples` points along it, the pixels two either side of
		// each along the axis nearest the edge's normal: each at the rules' value, but one within 0.6 px of
		// an edge the rules step across, which may land either side; and each step between neighbours at
		// the rules' step, unless `stepsHere`, the rules stepping across this edge. Counts what fails in
		// `failures`, reporting the first few.
		template <typename Expected>
		void probeAcross(Expected expected, const EdgeGrid& grid, Vec2 start, Vec2 end, int samples, int& failures) const {
			const Vec2 along = end - start;
			if (glm::dot(along, along) < 1e-6F) {
				return;
			}
			const Vec2 normal = glm::normalize(Vec2(-along.y, along.x));
			const Vec2 axis = std::abs(normal.x) >= std::abs(normal.y) ? Vec2(1.0F, 0.0F) : Vec2(0.0F, 1.0F);
			const auto undecided = [&](Vec2 centre) {
				return grid.nearEdgeWhere(centre, [&](Vec2 edgeStart, Vec2 edgeEnd) {
					const Vec2	edge = edgeEnd - edgeStart;
					const float share = std::clamp(glm::dot(centre - edgeStart, edge) / glm::dot(edge, edge), 0.0F, 1.0F);
					return stepsByTheRules(expected, edgeStart + (edge * share), glm::normalize(Vec2(-edge.y, edge.x)));
				});
			};
			const auto fail = [&](const std::string& what) {
				if (++failures <= 8) {
					ADD_FAILURE() << what;
				}
			};
			for (int sample = 0; sample < samples; ++sample) {
				const Vec2 onEdge = start + (along * ((static_cast<float>(sample) + 0.5F) / static_cast<float>(samples)));
				const bool stepsHere = stepsByTheRules(expected, onEdge, normal);
				const Vec2 middle(std::floor(onEdge.x) + 0.5F, std::floor(onEdge.y) + 0.5F);
				std::optional<std::pair<Vec2, bool>> previous;
				for (int offset = -2; offset <= 2; ++offset) {
					const Vec2 centre = middle + (axis * static_cast<float>(offset));
					if (centre.x < 0.0F || centre.y < 0.0F || centre.x > kSize || centre.y > kSize) {
						previous.reset();
						continue;
					}
					const bool decided = !undecided(centre);
					if (decided && std::abs(value(centre) - expected(centre)) > kTolerance) {
						fail(
							"(" + std::to_string(centre.x) + ", " + std::to_string(centre.y) + ") holds " + std::to_string(value(centre)) +
							", the rules give " + std::to_string(expected(centre))
						);
					}
					if (previous.has_value() && previous->second && decided && !stepsHere) {
						const Vec2	before = previous->first;
						const float step = value(centre) - value(before);
						const float want = expected(centre) - expected(before);
						if (std::abs(step - want) > kStepTolerance) {
							fail(
								"a step of " + std::to_string(step) + " from (" + std::to_string(before.x) + ", " +
								std::to_string(before.y) + ") to (" + std::to_string(centre.x) + ", " + std::to_string(centre.y) +
								"), the rules give " + std::to_string(want)
							);
						}
					}
					previous = std::pair{centre, decided};
				}
			}
		}

		static constexpr Foundation::Rect kView{-4.0F, -4.0F, kSize + 8.0F, kSize + 8.0F};

		GLFWwindow*			 window = nullptr;
		RenderTarget		 target;
		std::vector<uint8_t> pixels;
	};

	// A content shape reaching far past the masked rect writes nothing outside the rect's cover box:
	// on a target cleared transparent, the pixels between the rect and the shape's edge keep alpha 0,
	// and under the rect the alpha is opaque.
	TEST_F(CoverageDrawTest, AContentShapeWritesNothingOutsideTheCoverBox) {
		const std::array<Vec2, 4>	   shape = {{{100.0F, 100.0F}, {400.0F, 100.0F}, {400.0F, 400.0F}, {100.0F, 400.0F}}};
		const std::vector<MaskShape>   content = {{.points = shape, .paint = MaskPaint::flat(0.75F)}};
		const std::array<MaskLayer, 1> mask = {{{.image = MaskImage::content(content)}}};
		const Foundation::Rect		   bounds{200.0F, 200.0F, 100.0F, 100.0F};
		target.begin();
		Primitives::setViewport(kSize, kSize);
		Primitives::clear(Foundation::Color(0.0F, 0.0F, 0.0F, 0.0F));
		Primitives::beginFrame();
		Primitives::drawRect({.bounds = bounds, .style = {.fill = Foundation::Color::white()}, .mask = mask});
		Primitives::endFrame();
		target.end();
		pixels.assign(static_cast<size_t>(kSize) * kSize * 4, 0);
		target.readPixels(pixels);

		int outside = 0;
		int inside = 0;
		for (int y = 0; y < kSize; ++y) {
			for (int x = 0; x < kSize; ++x) {
				const bool				 farOutside = x < 198 || x >= 302 || y < 198 || y >= 302;
				const bool				 wellInside = x >= 202 && x < 298 && y >= 202 && y < 298;
				const std::array<int, 4> pixel = rgba(x, y);
				if (farOutside && pixel[3] != 0 && ++outside <= 8) {
					ADD_FAILURE() << "alpha " << pixel[3] << " outside the cover box at (" << x << ", " << y << ")";
				}
				if (wellInside && pixel[3] != 255 && ++inside <= 8) {
					ADD_FAILURE() << "alpha " << pixel[3] << " under the rect at (" << x << ", " << y << ")";
				}
			}
		}
		EXPECT_EQ(outside, 0);
		EXPECT_EQ(inside, 0);
		EXPECT_NEAR(rgba(250, 250)[0], 191, 2); // white at three quarters over transparent black
	}

	// The unseen fill's mask of 7.5: the view less the record's meshes and the pending shapes, and over
	// them the feather of the seen area's edge, the record's pieces kept in two cells (two meshes
	// sharing an edge), a pending shape abutting the record along its edge and a pending leg
	// overlapping it, each with the record's pieces cut against it. Against 7.1 and 7.2: 1 outside the
	// seen area, the fade of the distance to its edge inside.
	TEST_F(CoverageDrawTest, TheUnseenFillFadesFromTheSeenAreasEdgeWithNoSeam) {
		struct Box {
			float left, top, right, bottom;
			bool  holds(Vec2 point) const { return point.x >= left && point.x <= right && point.y >= top && point.y <= bottom; }
		};
		const Box				 firstCell{60.0F, 60.0F, 180.0F, 200.0F};
		const Box				 secondCell{180.0F, 60.0F, 300.0F, 160.0F};
		const Box				 abutting{300.0F, 60.0F, 380.0F, 160.0F};
		const Box				 overlapping{100.0F, 180.0F, 150.0F, 250.0F};
		const std::array<Box, 4> seen = {firstCell, secondCell, abutting, overlapping};

		// The record's pieces, cell by cell, for the record's own edge (the shared edge draws none).
		std::vector<Piece> firstCellPieces;
		std::vector<Piece> secondCellPieces;
		edgePieces({60.0F, 60.0F}, {180.0F, 60.0F}, {0.0F, 1.0F}, firstCellPieces);
		edgePieces({60.0F, 200.0F}, {60.0F, 60.0F}, {1.0F, 0.0F}, firstCellPieces);
		edgePieces({180.0F, 200.0F}, {60.0F, 200.0F}, {0.0F, -1.0F}, firstCellPieces);
		edgePieces({180.0F, 160.0F}, {180.0F, 200.0F}, {-1.0F, 0.0F}, firstCellPieces);
		const float pi = std::numbers::pi_v<float>;
		fanPieces({180.0F, 160.0F}, pi, 1.5F * pi, firstCellPieces); // the reflex corner where the cells' heights differ
		edgePieces({180.0F, 60.0F}, {300.0F, 60.0F}, {0.0F, 1.0F}, secondCellPieces);
		edgePieces({300.0F, 160.0F}, {180.0F, 160.0F}, {0.0F, -1.0F}, secondCellPieces);
		edgePieces({300.0F, 60.0F}, {300.0F, 160.0F}, {-1.0F, 0.0F}, secondCellPieces);
		const auto upload = [](std::span<const Piece> pieces) {
			renderer::TessellatedMesh mesh;
			for (const Piece& piece : pieces) {
				for (size_t corner = 0; corner < 3; ++corner) {
					mesh.indices.push_back(static_cast<uint16_t>(mesh.vertices.size()));
					mesh.vertices.push_back(piece.corners[corner]);
					mesh.colors.emplace_back(1.0F, 1.0F, 1.0F, piece.values[corner]);
				}
			}
			return Primitives::uploadInstancedMesh(mesh, 1);
		};
		const auto boxMesh = [](const Box& box) {
			renderer::TessellatedMesh mesh;
			mesh.vertices = {{box.left, box.top}, {box.right, box.top}, {box.right, box.bottom}, {box.left, box.bottom}};
			mesh.indices = {0, 1, 2, 0, 2, 3};
			return Primitives::uploadInstancedMesh(mesh, 1);
		};
		InstancedMeshHandle			  firstFeather = upload(firstCellPieces);
		InstancedMeshHandle			  secondFeather = upload(secondCellPieces);
		InstancedMeshHandle			  firstSeen = boxMesh(firstCell);
		InstancedMeshHandle			  secondSeen = boxMesh(secondCell);
		const Primitives::WorldCamera camera{.position = {kSize / 2.0F, kSize / 2.0F}}; // world is screen

		// Each pending shape grown by the feather, its convex corners round in the fan's steps.
		const auto grown = [](const Box& box) {
			std::vector<Vec2>							outline;
			const std::array<std::pair<Vec2, float>, 4> corners = {{
				{{box.right, box.top}, -0.5F},
				{{box.right, box.bottom}, 0.0F},
				{{box.left, box.bottom}, 0.5F},
				{{box.left, box.top}, 1.0F},
			}};
			for (const auto& [corner, start] : corners) {
				for (int step = 0; step <= 6; ++step) {
					const float angle = std::numbers::pi_v<float> * (start + (0.5F * static_cast<float>(step) / 6.0F));
					outline.push_back(corner + (Vec2(std::cos(angle), std::sin(angle)) * kFeather));
				}
			}
			return outline;
		};
		const std::vector<Vec2>		   abuttingGrown = grown(abutting);
		const std::vector<Vec2>		   overlappingGrown = grown(overlapping);
		const std::array<MaskLayer, 3> outsidePendingFeather = {{
			{.image = MaskImage::rect(kView), .composite = MaskComposite::Subtract},
			{.image = MaskImage::polygon(abuttingGrown)},
			{.image = MaskImage::polygon(overlappingGrown)},
		}};

		// Inside the pending shapes' feather: the record's pieces cut against them, and theirs.
		std::vector<Piece> pendingPieces;
		edgePieces({288.0F, 60.0F}, {300.0F, 60.0F}, {0.0F, 1.0F}, pendingPieces);
		edgePieces({300.0F, 160.0F}, {288.0F, 160.0F}, {0.0F, -1.0F}, pendingPieces);
		edgePieces({300.0F, 60.0F}, {380.0F, 60.0F}, {0.0F, 1.0F}, pendingPieces);
		edgePieces({380.0F, 160.0F}, {300.0F, 160.0F}, {0.0F, -1.0F}, pendingPieces);
		edgePieces({380.0F, 60.0F}, {380.0F, 160.0F}, {-1.0F, 0.0F}, pendingPieces);
		edgePieces({100.0F, 200.0F}, {88.0F, 200.0F}, {0.0F, -1.0F}, pendingPieces);
		edgePieces({162.0F, 200.0F}, {150.0F, 200.0F}, {0.0F, -1.0F}, pendingPieces);
		edgePieces({100.0F, 250.0F}, {100.0F, 200.0F}, {1.0F, 0.0F}, pendingPieces);
		edgePieces({150.0F, 200.0F}, {150.0F, 250.0F}, {-1.0F, 0.0F}, pendingPieces);
		edgePieces({150.0F, 250.0F}, {100.0F, 250.0F}, {0.0F, -1.0F}, pendingPieces);
		fanPieces({100.0F, 200.0F}, 1.5F * pi, 2.0F * pi, pendingPieces);
		fanPieces({150.0F, 200.0F}, pi, 1.5F * pi, pendingPieces);

		std::vector<MaskShape> unseenFeather = {
			{.mesh = {firstFeather, camera}, .mask = outsidePendingFeather, .blend = MaskBlend::Lighten},
			{.mesh = {secondFeather, camera}, .mask = outsidePendingFeather, .blend = MaskBlend::Lighten},
		};
		std::vector<std::array<Vec2, 3>> outlines;
		outlines.reserve(pendingPieces.size());
		appendShapes(pendingPieces, {}, outlines, unseenFeather);
		const std::array<Vec2, 4> abuttingOutline = {
			{{abutting.left, abutting.top},
			 {abutting.right, abutting.top},
			 {abutting.right, abutting.bottom},
			 {abutting.left, abutting.bottom}}
		};
		const std::array<Vec2, 4> overlappingOutline = {
			{{overlapping.left, overlapping.top},
			 {overlapping.right, overlapping.top},
			 {overlapping.right, overlapping.bottom},
			 {overlapping.left, overlapping.bottom}}
		};
		const std::array<MaskLayer, 6> unseenMask = {{
			{.image = MaskImage::content(unseenFeather)},
			{.image = MaskImage::rect(kView), .composite = MaskComposite::Subtract},
			{.image = MaskImage::mesh(firstSeen, camera)},
			{.image = MaskImage::mesh(secondSeen, camera)},
			{.image = MaskImage::polygon(abuttingOutline)},
			{.image = MaskImage::polygon(overlappingOutline)},
		}};
		render(unseenMask);
		expectOpaque("unseen fill"); // the alpha set back to opaque after the fill

		// The seen area's edge exactly: each box's sides, cut where another box holds them.
		std::vector<std::pair<Vec2, Vec2>> boundary;
		for (const Box& box : seen) {
			const std::array<std::pair<Vec2, Vec2>, 4> sides = {{
				{{box.left, box.top}, {box.right, box.top}},
				{{box.right, box.top}, {box.right, box.bottom}},
				{{box.right, box.bottom}, {box.left, box.bottom}},
				{{box.left, box.bottom}, {box.left, box.top}},
			}};
			for (const auto& [start, end] : sides) {
				std::vector<float> cuts = {0.0F, 1.0F};
				for (const Box& other : seen) {
					for (const float coordinate : {other.left, other.right, other.top, other.bottom}) {
						const float share =
							start.x == end.x ? (coordinate - start.y) / (end.y - start.y) : (coordinate - start.x) / (end.x - start.x);
						if (share > 0.0F && share < 1.0F) {
							cuts.push_back(share);
						}
					}
				}
				std::sort(cuts.begin(), cuts.end());
				const Vec2 along = end - start;
				const Vec2 outward = -inwardNormal(
					std::array<Vec2, 4>{
						Vec2(box.left, box.top), Vec2(box.right, box.top), Vec2(box.right, box.bottom), Vec2(box.left, box.bottom)
					},
					start,
					end
				);
				for (size_t index = 0; index + 1 < cuts.size(); ++index) {
					const Vec2 middle = start + (along * ((cuts[index] + cuts[index + 1]) * 0.5F)) + (outward * 0.01F);
					if (std::none_of(seen.begin(), seen.end(), [&](const Box& other) { return other.holds(middle); })) {
						boundary.emplace_back(start + (along * cuts[index]), start + (along * cuts[index + 1]));
					}
				}
			}
		}
		const auto expected = [&](Vec2 point) {
			if (std::none_of(seen.begin(), seen.end(), [&](const Box& box) { return box.holds(point); })) {
				return 1.0F;
			}
			float depth = std::numeric_limits<float>::max();
			for (const auto& [start, end] : boundary) {
				depth = std::min(depth, distanceToSegment(point, start, end));
			}
			return fade(depth);
		};
		expectEveryPixel(expected, EdgeGrid({}), "unseen fill");

		// No seam where the two cells share an edge, or where the pending shape abuts the record.
		for (float y = 75.5F; y < 145.0F; y += 1.0F) {
			EXPECT_LE(value({180.5F, y}), kTolerance) << "the cells' shared edge at y " << y;
			EXPECT_LE(value({179.5F, y}), kTolerance) << "the cells' shared edge at y " << y;
			EXPECT_LE(value({300.5F, y}), kTolerance) << "the abutting edge at y " << y;
			EXPECT_LE(value({299.5F, y}), kTolerance) << "the abutting edge at y " << y;
		}
		// The field the pieces draw, which the probes below hold a step to: the fans are 15-degree
		// chords of 7.2's round fade, off it by up to 0.1 px of distance, so a pixel's step against the
		// round fade can miss by more than a seam's tolerance where nothing is wrong. 1 outside the seen
		// area; inside, the greatest of the pending pieces and of the record's outside the pending
		// shapes' feather.
		const auto drawnField = [&](Vec2 point) {
			if (std::none_of(seen.begin(), seen.end(), [&](const Box& box) { return box.holds(point); })) {
				return 1.0F;
			}
			float greatest = 0.0F;
			for (const Piece& piece : pendingPieces) {
				greatest = std::max(greatest, piece.at(point).value_or(0.0F));
			}
			if (!insidePolygon(abuttingGrown, point) && !insidePolygon(overlappingGrown, point)) {
				for (const std::vector<Piece>* cell : {&firstCellPieces, &secondCellPieces}) {
					for (const Piece& piece : *cell) {
						greatest = std::max(greatest, piece.at(point).value_or(0.0F));
					}
				}
			}
			return greatest;
		};
		// No seam across the pending leg's overlap with the record: down through the record's bottom
		// edge inside the leg, beside the leg's sides and down its middle, a pixel at a time.
		for (const float x : {101.5F, 105.5F, 125.5F, 144.5F, 148.5F}) {
			expectProbe(drawnField, {x, 170.5F}, {0.0F, 1.0F}, 80, "the pending leg's overlap");
		}
		// And across it, along the record's old bottom edge and just inside the leg's.
		for (const float y : {199.5F, 200.5F, 238.5F, 248.5F}) {
			expectProbe(drawnField, {90.5F, y}, {1.0F, 0.0F}, 70, "across the pending leg");
		}
		// Nothing darkened twice: inside a convex corner, the nearer edge's fade alone.
		EXPECT_NEAR(value({374.5F, 66.5F}), fade(5.5F), kTolerance);
		// The reflex corner's fan: the fade of the distance to the corner.
		EXPECT_NEAR(value({176.5F, 156.5F}), fade(glm::length(Vec2(3.5F, 3.5F))), 0.02F);
	}

	// The tint's mask of 7.5, the least darkness over the colonists, each an isolated group of his
	// bright shape at 0 and his pieces over it by lighten, each piece clipped to his bright shape.
	// The cases of 7.7 that are the tint's: two bright shapes overlapping and two abutting, a beam
	// narrower than twice the feather, a reflex corner, a soft body's triangle and fins, an opaque
	// body's bands and fins seen from outside and from inside a room, a piece reaching past its
	// colonist's bright shape, one colonist's shadow inside another's sight, and 50 colonists
	// overlapping in one view with their shadows. Against 7.1 and 7.4, pixel by pixel.
	TEST_F(CoverageDrawTest, TheTintIsTheLeastDarknessOverTheColonists) {
		std::vector<Colonist> colonists;
		const float			  pi = std::numbers::pi_v<float>;

		// A colonist in the open with an opaque rock north of him, seen from outside: his bright shape
		// notched by its hard shadow, the shadow sides and the rock's face drawing no feather, a band
		// and a fin on each shadow edge's lit side; and a trunk's soft triangle and fins east of him,
		// whose tip and fin reach into his neighbour's sight.
		{
			const Vec2		  eye(90.0F, 110.0F);
			const float		  radius = 65.0F;
			const Vec2		  nearLeft(83.0F, 72.0F);
			const Vec2		  nearRight(97.0F, 72.0F);
			const Vec2		  leftRay = glm::normalize(nearLeft - eye);
			const Vec2		  rightRay = glm::normalize(nearRight - eye);
			const float		  leftAngle = std::atan2(leftRay.y, leftRay.x);
			const float		  rightAngle = std::atan2(rightRay.y, rightRay.x);
			Colonist		  colonist;
			std::vector<bool> rim;
			for (int side = 0; side < 32; ++side) {
				const float angle = rightAngle + ((2.0F * pi - (rightAngle - leftAngle)) * static_cast<float>(side) / 31.0F);
				colonist.bright.push_back(eye + (Vec2(std::cos(angle), std::sin(angle)) * radius));
				rim.push_back(side < 31);
			}
			colonist.bright.push_back(nearLeft); // the shadow side in, the face across, the side out
			rim.push_back(false);
			colonist.bright.push_back(nearRight);
			rim.push_back(false);
			// The vertex at leftAngle closes the outline back to rightAngle's through the notch.
			colonist.featherRim([&](size_t index) { return rim[index]; });
			const float reach = radius - glm::length(nearLeft - eye);
			hardShadowEdge(nearLeft, leftRay, Vec2(leftRay.y, -leftRay.x), reach + 2.0F, colonist.pieces);
			hardShadowEdge(nearRight, rightRay, Vec2(-rightRay.y, rightRay.x), reach + 2.0F, colonist.pieces);
			softShadow(eye + Vec2(30.0F, 10.0F), glm::normalize(Vec2(30.0F, 10.0F)), 8.0F, 45.0F, 0.5F, colonist.pieces);
			// A piece reaching past his bright shape, onto ground no one sees.
			colonist.pieces.push_back(
				{{eye + Vec2(-40.0F, 30.0F), eye + Vec2(-100.0F, 60.0F), eye + Vec2(-40.0F, 60.0F)}, {0.2F, 0.2F, 0.2F}}
			);
			colonists.push_back(std::move(colonist));
		}
		// His neighbour, whose sight overlaps his: the lens.
		{
			Colonist colonist{.bright = circle({170.0F, 110.0F}, 60.0F, 32)};
			colonist.featherRim([](size_t) { return true; });
			colonists.push_back(std::move(colonist));
		}
		// A beam narrower than twice the feather, all rim.
		{
			Colonist colonist{.bright = {{250.0F, 30.0F}, {470.0F, 50.0F}, {469.0F, 60.0F}, {249.0F, 40.0F}}};
			colonist.featherRim([](size_t) { return true; });
			colonists.push_back(std::move(colonist));
		}
		// A colonist inside a room: its walls are body faces with no feather, its doorway's jambs cast
		// shadow edges with bands and fins down a beam whose far end is rim.
		{
			const Vec2 eye(310.0F, 130.0F);
			const Vec2 upperJamb(360.0F, 125.0F);
			const Vec2 lowerJamb(360.0F, 135.0F);
			const Vec2 upperRay = glm::normalize(upperJamb - eye);
			const Vec2 lowerRay = glm::normalize(lowerJamb - eye);
			const Vec2 upperEnd = upperJamb + (upperRay * (80.0F / upperRay.x));
			const Vec2 lowerEnd = lowerJamb + (lowerRay * (80.0F / lowerRay.x));
			Colonist   colonist{
				  .bright = {{260.0F, 90.0F}, {360.0F, 90.0F}, upperJamb, upperEnd, lowerEnd, lowerJamb, {360.0F, 170.0F}, {260.0F, 170.0F}}
			  };
			colonist.featherRim([](size_t index) { return index == 3; });
			hardShadowEdge(upperJamb, upperRay, Vec2(-upperRay.y, upperRay.x), 82.0F, colonist.pieces);
			hardShadowEdge(lowerJamb, lowerRay, Vec2(lowerRay.y, -lowerRay.x), 82.0F, colonist.pieces);
			colonists.push_back(std::move(colonist));
		}
		// Two bright shapes abutting, and an L-shaped one with a reflex corner.
		for (const float left : {380.0F, 440.0F}) {
			Colonist colonist{.bright = {{left, 180.0F}, {left + 60.0F, 180.0F}, {left + 60.0F, 240.0F}, {left, 240.0F}}};
			colonist.featherRim([](size_t) { return true; });
			colonists.push_back(std::move(colonist));
		}
		{
			Colonist colonist{
				.bright = {{200.0F, 180.0F}, {340.0F, 180.0F}, {340.0F, 210.0F}, {240.0F, 210.0F}, {240.0F, 250.0F}, {200.0F, 250.0F}}
			};
			colonist.featherRim([](size_t) { return true; });
			fanPieces({240.0F, 210.0F}, 1.5F * pi, pi, colonist.pieces);
			colonists.push_back(std::move(colonist));
		}
		// 50 colonists overlapping in one view, each with a trunk's shadow.
		foundation::Rng random(2026);
		for (int count = 0; count < 50; ++count) {
			const Vec2	centre(60.0F + (random.nextFloat() * 390.0F), 300.0F + (random.nextFloat() * 150.0F));
			const float radius = 35.0F + (random.nextFloat() * 20.0F);
			Colonist	colonist{.bright = circle(centre, radius, 16)};
			colonist.featherRim([](size_t) { return true; });
			const float angle = random.nextFloat() * 2.0F * pi;
			const Vec2	away(std::cos(angle), std::sin(angle));
			softShadow(centre + (away * radius * 0.3F), away, 6.0F, radius * 0.9F, 0.3F + (random.nextFloat() * 0.4F), colonist.pieces);
			colonists.push_back(std::move(colonist));
		}

		// The mask of 7.5: the view at 1, then a group a colonist.
		const std::array<Vec2, 4> viewPoints = {
			{{kView.x, kView.y},
			 {kView.x + kView.width, kView.y},
			 {kView.x + kView.width, kView.y + kView.height},
			 {kView.x, kView.y + kView.height}}
		};
		std::vector<std::array<MaskLayer, 1>> insideBrightShapes;
		std::vector<std::array<Vec2, 3>>	  outlines;
		size_t								  pieceCount = 0;
		for (const Colonist& colonist : colonists) {
			pieceCount += colonist.pieces.size();
		}
		insideBrightShapes.reserve(colonists.size());
		outlines.reserve(pieceCount);
		std::vector<MaskShape> tint = {{.points = viewPoints, .paint = MaskPaint::flat(1.0F)}};
		for (const Colonist& colonist : colonists) {
			insideBrightShapes.push_back({{{.image = MaskImage::polygon(colonist.bright)}}});
			MaskShape group{.blend = MaskBlend::Darken};
			group.children.push_back({.points = colonist.bright, .paint = MaskPaint::flat(0.0F)});
			appendShapes(colonist.pieces, insideBrightShapes.back(), outlines, group.children);
			tint.push_back(std::move(group));
		}
		const std::array<MaskLayer, 1> tintMask = {{{.image = MaskImage::content(tint)}}};
		render(tintMask);

		std::vector<std::pair<Vec2, Vec2>> edges;
		for (const Colonist& colonist : colonists) {
			for (size_t index = 0; index < colonist.bright.size(); ++index) {
				edges.emplace_back(colonist.bright[index], colonist.bright[(index + 1) % colonist.bright.size()]);
			}
			for (const Piece& piece : colonist.pieces) {
				for (size_t corner = 0; corner < 3; ++corner) {
					edges.emplace_back(piece.corners[corner], piece.corners[(corner + 1) % 3]);
				}
			}
		}
		// Each bright shape's box: outside it his darkness is 1, which leaves the least alone.
		std::vector<std::array<float, 4>> boxes;
		for (const Colonist& colonist : colonists) {
			std::array<float, 4> box = {colonist.bright[0].x, colonist.bright[0].y, colonist.bright[0].x, colonist.bright[0].y};
			for (const Vec2 point : colonist.bright) {
				box = {std::min(box[0], point.x), std::min(box[1], point.y), std::max(box[2], point.x), std::max(box[3], point.y)};
			}
			boxes.push_back(box);
		}
		const auto expected = [&](Vec2 point) {
			float least = 1.0F;
			for (size_t index = 0; index < colonists.size(); ++index) {
				const std::array<float, 4>& box = boxes[index];
				if (point.x >= box[0] && point.x <= box[2] && point.y >= box[1] && point.y <= box[3]) {
					least = std::min(least, colonists[index].darkness(point));
				}
			}
			return least;
		};
		const EdgeGrid grid(edges);
		expectEveryPixel(expected, grid, "tint");
		expectOpaque("tint");

		// The sweep leaves out pixels by an edge; these cross the edges where a seam would show, a
		// pixel at a time. Across the abutting bright shapes' shared edge, each side the rule's value
		// and no step between.
		for (float y = 190.5F; y < 230.0F; y += 1.0F) {
			EXPECT_NEAR(value({439.5F, y}), expected({439.5F, y}), kTolerance) << "left of the abutting edge at y " << y;
			EXPECT_NEAR(value({440.5F, y}), expected({440.5F, y}), kTolerance) << "right of the abutting edge at y " << y;
			EXPECT_NEAR(value({439.5F, y}), value({440.5F, y}), kStepTolerance) << "a step across the abutting edge at y " << y;
		}
		// Along a feather strip's shared diagonal, the right one's top edge from (440, 180) to (500, 192):
		// the pixels on it and either side.
		for (float x = 446.5F; x < 494.0F; x += 1.0F) {
			const float diagonal = 180.0F + ((x - 440.0F) * 0.2F);
			expectProbe(expected, {x, std::floor(diagonal) - 1.5F}, {0.0F, 1.0F}, 4, "the feather strip's diagonal");
		}
		// Across every other edge the sweep leaves out: each of the named cases' bright shapes and
		// pieces a pixel at a time along it, and a seeded sample of the crowd's.
		const auto edgesOf = [](const Colonist& colonist, std::vector<std::pair<Vec2, Vec2>>& into) {
			for (size_t index = 0; index < colonist.bright.size(); ++index) {
				into.emplace_back(colonist.bright[index], colonist.bright[(index + 1) % colonist.bright.size()]);
			}
			for (const Piece& piece : colonist.pieces) {
				for (size_t corner = 0; corner < 3; ++corner) {
					into.emplace_back(piece.corners[corner], piece.corners[(corner + 1) % 3]);
				}
			}
		};
		constexpr size_t				   kCrowdSize = 50; // the last colonists, the crowd
		std::vector<std::pair<Vec2, Vec2>> namedEdges;
		std::vector<std::pair<Vec2, Vec2>> crowdEdges;
		for (size_t index = 0; index < colonists.size(); ++index) {
			edgesOf(colonists[index], index + kCrowdSize < colonists.size() ? namedEdges : crowdEdges);
		}
		int edgeFailures = 0;
		for (const auto& [start, end] : namedEdges) {
			const int samples = std::max(1, static_cast<int>(std::ceil(glm::length(end - start))));
			probeAcross(expected, grid, start, end, samples, edgeFailures);
		}
		foundation::Rng pick(77);
		for (int count = 0; count < 300; ++count) {
			const auto& [start, end] = crowdEdges[pick.nextUInt32() % crowdEdges.size()];
			probeAcross(expected, grid, start, end, 3, edgeFailures);
		}
		EXPECT_EQ(edgeFailures, 0) << "pixels or steps off across the edges the sweep leaves out";

		// The beam, 10 px across against a feather of 12: never fully bright, and at its middle the
		// nearer side's fade alone, not both sides' darkened together.
		const Vec2	beamMiddle(360.0F, 45.0F);
		const float beamDepth = std::abs(cross(glm::normalize(Vec2(220.0F, 20.0F)), beamMiddle + Vec2(0.5F, 0.5F) - Vec2(250.0F, 30.0F)));
		const float beamWidth = std::abs(cross(glm::normalize(Vec2(220.0F, 20.0F)), Vec2(249.0F, 40.0F) - Vec2(250.0F, 30.0F)));
		EXPECT_NEAR(value(beamMiddle), fade(std::min(beamDepth, beamWidth - beamDepth)), kTolerance);
		EXPECT_GT(value(beamMiddle), 0.4F);
		// His neighbour's core is fully bright, though the trunk's triangle and fin reach into it.
		for (const Vec2 point : {Vec2(130.5F, 125.5F), Vec2(150.5F, 130.5F), Vec2(160.5F, 128.5F)}) {
			EXPECT_GT(colonists[0].darkness(point), 0.1F) << "the shadow should reach " << point.x << ", " << point.y;
			EXPECT_LE(value(point), kTolerance) << "his shadow over his neighbour's light at " << point.x << ", " << point.y;
		}
		// The piece past his bright shape lightens nothing outside it.
		EXPECT_GE(value({20.5F, 160.5F}), 1.0F - kTolerance);
		// Inside the room, against the wall: no feather, fully bright.
		EXPECT_LE(value({261.5F, 130.5F}), kTolerance);
		// No colonist's shadow over another's light, at 50 colonists: wherever one colonist sees a
		// point fully bright, the tint there is 0.
		int fullyLit = 0;
		for (int y = 262; y < kSize; y += 3) {
			for (int x = 0; x < kSize; x += 3) {
				const Vec2 point(static_cast<float>(x) + 0.5F, static_cast<float>(y) + 0.5F);
				const bool lit = std::any_of(colonists.begin(), colonists.end(), [&](const Colonist& colonist) {
					return colonist.darkness(point) == 0.0F;
				});
				if (lit && !grid.nearEdge(point)) {
					++fullyLit;
					EXPECT_LE(value(point), kTolerance) << "a shadow over another's light at " << x << ", " << y;
				}
			}
		}
		EXPECT_GT(fullyLit, 1000);
	}

} // namespace Renderer
