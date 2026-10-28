// What the base shader writes, drawn through Primitives into a RenderTarget
// against a hidden GL 3.3 core context and read back; skipped where no context
// can be made (headless CI). Output is premultiplied: a colour c at coverage a
// over transparent stores (c * a, a), and an additive draw adds c * a and leaves
// the destination's alpha alone.

#include "primitives/Primitives.h"

#include "BaseShader.h"
#include "backend/gl/GLBackend.h"
#include "resources/RenderTarget.h"
#include "vector/Types.h"

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/geometric.hpp>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <numbers>
#include <vector>

namespace Renderer {

	class PrimitivesDrawTest : public ::testing::Test {
	  protected:
		static constexpr int kSize = 32;

		void SetUp() override {
			if (glfwInit() != GLFW_TRUE) {
				GTEST_SKIP() << "GLFW initialization failed (no display available in CI)";
			}
			glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
			glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
			glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
			glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
			glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
			window = glfwCreateWindow(kSize, kSize, "offscreen", nullptr, nullptr);
			if (window == nullptr) {
				glfwTerminate();
				GTEST_SKIP() << "Could not create GLFW window (no OpenGL context available)";
			}
			glfwMakeContextCurrent(window);
			// The GL backend initialises GLEW on this context
			std::unique_ptr<backend::gl::GLBackend> gl = backend::gl::GLBackend::create();
			if (gl == nullptr) {
				glfwDestroyWindow(window);
				window = nullptr;
				glfwTerminate();
				GTEST_SKIP() << "GLEW initialization failed";
			}
			Primitives::init(std::move(gl), base_shader::source());
			glViewport(0, 0, kSize, kSize);
			Primitives::setCoordinateSystem(nullptr);
			Primitives::setViewport(kSize, kSize);
			target = RenderTarget::create(kSize, kSize);
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

		// Clear `into` to `background` and run `draw` into it as one frame.
		template <typename Draw>
		static void drawInto(RenderTarget& into, const Foundation::Color& background, Draw draw) {
			into.begin();
			Primitives::setViewport(into.width(), into.height());
			Primitives::clear(background);
			Primitives::beginFrame();
			draw();
			Primitives::endFrame();
			into.end();
		}

		// The same into the fixture's target, then read it back.
		template <typename Draw>
		void render(const Foundation::Color& background, Draw draw) {
			drawInto(target, background, draw);
			pixels.assign(static_cast<size_t>(kSize) * kSize * 4, 0);
			target.readPixels(pixels);
		}

		// The pixel at screen position (x, y), y down; readPixels rows run bottom up.
		std::array<int, 4> pixel(int x, int y) const {
			const size_t i = ((static_cast<size_t>(kSize - 1 - y) * kSize) + static_cast<size_t>(x)) * 4;
			return {pixels[i], pixels[i + 1], pixels[i + 2], pixels[i + 3]};
		}

		// Within `tolerance` per channel: the GPU may round 127.5 either way.
		static void expectNear(const std::array<int, 4>& actual, const std::array<int, 4>& expected, int tolerance = 1) {
			for (size_t c = 0; c < 4; ++c) {
				EXPECT_LE(std::abs(actual[c] - expected[c]), tolerance)
					<< "channel " << c << ": " << actual[0] << "," << actual[1] << "," << actual[2] << "," << actual[3];
			}
		}

		static void fill(const Foundation::Color& color, Primitives::BlendMode blend) {
			Primitives::drawRect(
				{.bounds = {0.0F, 0.0F, static_cast<float>(kSize), static_cast<float>(kSize)}, .style = {.fill = color}, .blend = blend}
			);
		}

		GLFWwindow*			 window = nullptr;
		RenderTarget		 target;
		std::vector<uint8_t> pixels;
	};

	TEST_F(PrimitivesDrawTest, AlphaOverTransparentStoresPremultipliedColourAndPlainAlpha) {
		render(Foundation::Color(0.0F, 0.0F, 0.0F, 0.0F), [] { fill({1.0F, 0.5F, 0.0F, 0.5F}, Primitives::BlendMode::Alpha); });
		// Straight blending (SRC_ALPHA, ONE_MINUS_SRC_ALPHA) stored alpha 0.5 * 0.5 here.
		expectNear(pixel(16, 16), {128, 64, 0, 128});
	}

	TEST_F(PrimitivesDrawTest, AlphaOverOpaqueIsOver) {
		render(Foundation::Color(0.0F, 0.0F, 1.0F, 1.0F), [] { fill({1.0F, 0.0F, 0.0F, 0.5F}, Primitives::BlendMode::Alpha); });
		expectNear(pixel(16, 16), {128, 0, 128, 255});
	}

	TEST_F(PrimitivesDrawTest, AdditiveAddsColourAndKeepsTheDestinationAlpha) {
		render(Foundation::Color(0.2F, 0.2F, 0.2F, 1.0F), [] { fill({0.0F, 1.0F, 0.0F, 0.5F}, Primitives::BlendMode::Additive); });
		expectNear(pixel(16, 16), {51, 179, 51, 255});

		render(Foundation::Color(0.0F, 0.0F, 0.0F, 0.0F), [] { fill({0.0F, 1.0F, 0.0F, 0.5F}, Primitives::BlendMode::Additive); });
		expectNear(pixel(16, 16), {0, 128, 0, 0});
	}

	TEST_F(PrimitivesDrawTest, AlphaAndAdditiveInterleavedDrawInOrder) {
		render(Foundation::Color(0.0F, 0.0F, 0.0F, 1.0F), [] {
			fill({1.0F, 0.0F, 0.0F, 1.0F}, Primitives::BlendMode::Alpha);
			fill({0.0F, 0.0F, 1.0F, 0.5F}, Primitives::BlendMode::Additive);
			fill({0.0F, 1.0F, 0.0F, 0.5F}, Primitives::BlendMode::Alpha);
		});
		// Red, plus half blue, then half green over that.
		expectNear(pixel(16, 16), {128, 128, 64, 255});
	}

	TEST_F(PrimitivesDrawTest, ImageSamplesPremultipliedTexelsUnderItsTint) {
		const std::array<uint8_t, 16> texels = {100, 50, 0, 200, 100, 50, 0, 200, 100, 50, 0, 200, 100, 50, 0, 200};
		const TextureHandle			  texture = Primitives::createTexture({.width = 2, .height = 2, .pixels = texels.data()});
		ASSERT_TRUE(texture.isValid());
		const Foundation::Rect bounds{0.0F, 0.0F, static_cast<float>(kSize), static_cast<float>(kSize)};

		render(Foundation::Color(0.0F, 0.0F, 0.0F, 0.0F), [&] { Primitives::drawImage({.texture = texture, .bounds = bounds}); });
		expectNear(pixel(16, 16), {100, 50, 0, 200});

		render(Foundation::Color(0.0F, 0.0F, 0.0F, 0.0F), [&] {
			Primitives::drawImage({.texture = texture, .bounds = bounds, .tint = {1.0F, 1.0F, 1.0F, 0.5F}});
		});
		expectNear(pixel(16, 16), {50, 25, 0, 100});
		Primitives::destroyTexture(texture);
	}

	TEST_F(PrimitivesDrawTest, ArcCoversItsBandAndSweepOnly) {
		// A quarter from +x round to +y, which is down the screen.
		render(Foundation::Color(0.0F, 0.0F, 0.0F, 0.0F), [] {
			Primitives::drawArc(
				{.center = {16.0F, 16.0F},
				 .innerRadius = 6.0F,
				 .outerRadius = 14.0F,
				 .startAngle = 0.0F,
				 .sweep = std::numbers::pi_v<float> * 0.5F,
				 .color = Foundation::Color::white()}
			);
		});
		expectNear(pixel(23, 23), {255, 255, 255, 255}); // 45 degrees, radius about 9.9
		expectNear(pixel(16, 16), {0, 0, 0, 0});		 // the hole
		expectNear(pixel(30, 30), {0, 0, 0, 0});		 // past the outer radius
		expectNear(pixel(23, 9), {0, 0, 0, 0});			 // above the centre: outside the sweep
		expectNear(pixel(9, 23), {0, 0, 0, 0});			 // left of the centre: outside the sweep
	}

	TEST_F(PrimitivesDrawTest, PolygonFillsAndItsBorderSitsOutside) {
		const std::array<Foundation::Vec2, 4> square = {{{8.0F, 8.0F}, {24.0F, 8.0F}, {24.0F, 24.0F}, {8.0F, 24.0F}}};
		render(Foundation::Color(0.0F, 0.0F, 0.0F, 0.0F), [&] {
			Primitives::drawPolygon(
				{.vertices = square,
				 .style = {
					 .fill = {1.0F, 0.0F, 0.0F, 1.0F}, .border = Foundation::LineStyle{.color = {0.0F, 0.0F, 1.0F, 1.0F}, .width = 4.0F}
				 }}
			);
		});
		expectNear(pixel(16, 16), {255, 0, 0, 255}); // fill
		expectNear(pixel(10, 16), {255, 0, 0, 255}); // a pixel in from the edge: fill only
		// The seam. The fill ends at x = 8; the border's inner edge runs a pixel in, at 9,
		// so its ramp (0.84 at half a pixel inside, 0.16 at half outside) lies over fill and
		// no background shows through on either side.
		expectNear(pixel(9, 16), {215, 0, 40, 255}, 2);
		expectNear(pixel(8, 16), {40, 0, 215, 255}, 2);
		expectNear(pixel(7, 16), {0, 0, 255, 255});
		expectNear(pixel(6, 16), {0, 0, 255, 255});
		expectNear(pixel(5, 5), {0, 0, 255, 255}); // the mitered corner
		expectNear(pixel(2, 16), {0, 0, 0, 0});	   // past the border
	}

	TEST_F(PrimitivesDrawTest, TranslucentAndAdditiveStrokesCoverEachPixelOnce) {
		// Each stroke alone over transparent, at alpha 0.5: one cover stores 128 in alpha
		// (or, additive, in the colour), two would store 191. `inside` is a pixel well
		// within the stroke.
		struct Case {
			const char*					  name;
			std::vector<Foundation::Vec2> points;
			float						  width;
			Primitives::LineCap			  cap;
			Primitives::LineJoin		  join;
			bool						  closed;
			std::array<int, 2>			  inside;
		};
		const std::vector<Foundation::Vec2> elbow = {{4.0F, 6.0F}, {26.0F, 6.0F}, {26.0F, 28.0F}};
		const std::vector<Foundation::Vec2> zigzag = {{3.0F, 26.0F}, {10.0F, 8.0F}, {17.0F, 26.0F}, {24.0F, 8.0F}, {30.0F, 26.0F}};
		const std::vector<Foundation::Vec2> square = {{6.0F, 6.0F}, {26.0F, 6.0F}, {26.0F, 26.0F}, {6.0F, 26.0F}};
		const std::vector<Foundation::Vec2> triangle = {{4.0F, 27.0F}, {16.0F, 4.0F}, {28.0F, 27.0F}};
		const std::vector<Case>				cases = {
			{"round elbow, butt caps", elbow, 6.0F, Primitives::LineCap::Butt, Primitives::LineJoin::Round, false, {15, 6}},
			{"mitered elbow, square caps", elbow, 6.0F, Primitives::LineCap::Square, Primitives::LineJoin::Miter, false, {26, 15}},
			{"round zigzag, round caps", zigzag, 4.0F, Primitives::LineCap::Round, Primitives::LineJoin::Round, false, {13, 16}},
			{"mitered closed square", square, 4.0F, Primitives::LineCap::Butt, Primitives::LineJoin::Miter, true, {16, 6}},
			{"round closed triangle", triangle, 5.0F, Primitives::LineCap::Butt, Primitives::LineJoin::Round, true, {15, 27}},
		};
		for (const Primitives::BlendMode blend : {Primitives::BlendMode::Alpha, Primitives::BlendMode::Additive}) {
			const int channel = blend == Primitives::BlendMode::Alpha ? 3 : 1;
			for (const Case& stroke : cases) {
				render(Foundation::Color(0.0F, 0.0F, 0.0F, 0.0F), [&] {
					Primitives::drawPolyline(
						{.points = stroke.points,
						 .width = stroke.width,
						 .color = {0.0F, 1.0F, 0.0F, 0.5F},
						 .cap = stroke.cap,
						 .join = stroke.join,
						 .closed = stroke.closed,
						 .blend = blend}
					);
				});
				int most = 0;
				for (int y = 0; y < kSize; ++y) {
					for (int x = 0; x < kSize; ++x) {
						most = std::max(most, pixel(x, y)[channel]);
					}
				}
				EXPECT_LE(most, 129) << stroke.name << (channel == 3 ? ", alpha" : ", additive");
				EXPECT_NEAR(pixel(stroke.inside[0], stroke.inside[1])[channel], 128, 1) << stroke.name;
			}
		}

		// A border-only polygon at alpha 0.5, its 53-degree notch mitered into the corner.
		const std::array<Foundation::Vec2, 7> notched = {
			{{6.0F, 6.0F}, {12.0F, 6.0F}, {16.0F, 14.0F}, {20.0F, 6.0F}, {26.0F, 6.0F}, {26.0F, 26.0F}, {6.0F, 26.0F}}
		};
		render(Foundation::Color(0.0F, 0.0F, 0.0F, 0.0F), [&] {
			Primitives::drawPolygon(
				{.vertices = notched,
				 .style = {
					 .fill = Foundation::Color::transparent(),
					 .border = Foundation::LineStyle{.color = {1.0F, 1.0F, 1.0F, 0.5F}, .width = 3.0F}
				 }}
			);
		});
		int most = 0;
		for (int y = 0; y < kSize; ++y) {
			for (int x = 0; x < kSize; ++x) {
				most = std::max(most, pixel(x, y)[3]);
			}
		}
		EXPECT_LE(most, 129);
		EXPECT_NEAR(pixel(4, 16)[3], 128, 1);
	}

	TEST_F(PrimitivesDrawTest, AMiteredJointDrawsNoSeamAcrossTheStroke) {
		// Opaque white over black: every pixel whose centre is a pixel and a half inside the
		// stroke is whole. A cut miter's box once ended inside its piece at a gentle turn,
		// and its end face's ramp drew a darker column across the stroke at the joint.
		const auto expectWhole = [&](const char* name, const auto& inside) {
			for (int y = 0; y < kSize; ++y) {
				for (int x = 0; x < kSize; ++x) {
					if (inside(static_cast<float>(x) + 0.5F, static_cast<float>(y) + 0.5F)) {
						EXPECT_GE(pixel(x, y)[0], 254) << name << " at " << x << ", " << y;
					}
				}
			}
		};
		const auto mitered = [](const std::vector<Foundation::Vec2>& points, float width) {
			Primitives::drawPolyline(
				{.points = points, .width = width, .color = Foundation::Color::white(), .join = Primitives::LineJoin::Miter}
			);
		};

		// Straight through a joint on a pixel's centre, then on a pixel's edge, width 10: drawPolyline
		// drops a point that straight, so a 3-degree turn keeps its joint.
		for (const float joint : {16.5F, 16.0F}) {
			const std::vector<Foundation::Vec2> line = {{2.0F, 16.5F}, {joint, 16.5F}, {30.0F, 16.5F}};
			render(Foundation::Color(0.0F, 0.0F, 0.0F, 1.0F), [&] { mitered(line, 10.0F); });
			expectWhole("straight", [](float x, float y) { return x > 3.5F && x < 28.5F && std::abs(y - 16.5F) < 3.5F; });
		}

		const float							slight = 3.0F * std::numbers::pi_v<float> / 180.0F;
		const std::vector<Foundation::Vec2> nearlyStraight = {
			{2.0F, 16.5F}, {16.5F, 16.5F}, Foundation::Vec2(16.5F, 16.5F) + (Foundation::Vec2(std::cos(slight), std::sin(slight)) * 13.5F)
		};
		render(Foundation::Color(0.0F, 0.0F, 0.0F, 1.0F), [&] { mitered(nearlyStraight, 10.0F); });
		expectWhole("3 degrees", [&](float x, float y) {
			const Foundation::Vec2 p = Foundation::Vec2(x, y) - nearlyStraight[1];
			const Foundation::Vec2 direction(std::cos(slight), std::sin(slight));
			const float			   before = x < nearlyStraight[1].x && x > 3.5F ? std::abs(p.y) : 99.0F;
			const float			   along = glm::dot(p, direction);
			const float			   after = along > 0.0F && along < 12.0F ? std::abs((direction.x * p.y) - (direction.y * p.x)) : 99.0F;
			return std::min(before, after) < 3.5F;
		});

		// A 10-degree turn.
		const float							turn = 10.0F * std::numbers::pi_v<float> / 180.0F;
		const Foundation::Vec2				direction(std::cos(turn), std::sin(turn));
		const std::vector<Foundation::Vec2> bend = {{2.0F, 12.5F}, {16.5F, 12.5F}, Foundation::Vec2(16.5F, 12.5F) + (direction * 14.0F)};
		render(Foundation::Color(0.0F, 0.0F, 0.0F, 1.0F), [&] { mitered(bend, 10.0F); });
		expectWhole("10 degrees", [&](float x, float y) {
			const Foundation::Vec2 p = Foundation::Vec2(x, y) - bend[1];
			const float			   before = x < bend[1].x && x > 3.5F ? std::abs(p.y) : 99.0F;
			const float			   along = glm::dot(p, direction);
			const float			   after = along > 0.0F && along < 12.5F ? std::abs((direction.x * p.y) - (direction.y * p.x)) : 99.0F;
			return std::min(before, after) < 3.5F;
		});

		// A 32-gon's border, 3 wide: mitered at every vertex, its band whole all round.
		std::vector<Foundation::Vec2> gon;
		for (int k = 0; k < 32; ++k) {
			const float angle = static_cast<float>(k) * 2.0F * std::numbers::pi_v<float> / 32.0F;
			gon.emplace_back(16.0F + (9.0F * std::cos(angle)), 16.0F + (9.0F * std::sin(angle)));
		}
		render(Foundation::Color(0.0F, 0.0F, 0.0F, 1.0F), [&] {
			Primitives::drawPolygon(
				{.vertices = gon,
				 .style = {
					 .fill = Foundation::Color::transparent(),
					 .border = Foundation::LineStyle{.color = Foundation::Color::white(), .width = 3.0F}
				 }}
			);
		});
		// The band runs from a pixel inside the outline to 3 outside it, about 8 to 12 from the
		// centre; a pixel in from both its edges is 9 to 11.
		expectWhole("32-gon border", [](float x, float y) {
			const float r = std::hypot(x - 16.0F, y - 16.0F);
			return r > 9.4F && r < 10.9F;
		});
	}

	TEST_F(PrimitivesDrawTest, ABorderFoldedOverASmallBumpDrawsNothingPastItsWidth) {
		// A 60 x 60 square at (10, 10) with a bump on one edge smaller than its 8-wide border.
		// The border's centre line folds over the bump; a flat piece once took its tip from
		// that fold and ran a solid spike some 60 pixels out of the square.
		constexpr int kLarge = 160;
		RenderTarget  large = RenderTarget::create(kLarge, kLarge);
		ASSERT_TRUE(large.isValid());
		std::vector<Foundation::Vec2> bump = {
			{0.0F, 0.0F}, {20.0F, 0.0F}, {25.0F, 3.0F}, {27.0F, 4.0F}, {32.0F, 0.0F}, {60.0F, 0.0F}, {60.0F, 60.0F}, {0.0F, 60.0F}
		};
		for (Foundation::Vec2& point : bump) {
			point += Foundation::Vec2(10.0F, 10.0F);
		}
		drawInto(large, Foundation::Color(0.0F, 0.0F, 0.0F, 1.0F), [&] {
			Primitives::drawPolygon(
				{.vertices = bump,
				 .style = {
					 .fill = Foundation::Color::transparent(),
					 .border = Foundation::LineStyle{.color = Foundation::Color::white(), .width = 8.0F}
				 }}
			);
		});
		std::vector<uint8_t> read(static_cast<size_t>(kLarge) * kLarge * 4, 0);
		large.readPixels(read);
		// The border's outer edge is at 78 on the far sides; nothing lit two pixels past it.
		int outside = 0;
		for (int y = 0; y < kLarge; ++y) {
			for (int x = 0; x < kLarge; ++x) {
				const uint8_t red = read[((static_cast<size_t>(kLarge - 1 - y) * kLarge) + static_cast<size_t>(x)) * 4];
				if (red > 0 && (x >= 80 || y >= 80)) {
					++outside;
				}
			}
		}
		EXPECT_EQ(outside, 0);
	}

	TEST_F(PrimitivesDrawTest, APolygonsFillRampsOverThePixelOutsideItsEdge) {
		const std::array<Foundation::Vec2, 4> square = {{{8.0F, 8.0F}, {24.0F, 8.0F}, {24.0F, 24.0F}, {8.0F, 24.0F}}};
		render(Foundation::Color(0.0F, 0.0F, 0.0F, 0.0F), [&] {
			Primitives::drawPolygon({.vertices = square, .style = {.fill = Foundation::Color::white()}});
		});
		for (int y = 8; y < 24; ++y) {
			for (int x = 8; x < 24; ++x) {
				expectNear(pixel(x, y), {255, 255, 255, 255});
			}
		}
		// Half a pixel out, half the ramp; a pixel and a half out, nothing.
		expectNear(pixel(7, 16), {128, 128, 128, 128}, 2);
		expectNear(pixel(24, 16), {128, 128, 128, 128}, 2);
		expectNear(pixel(16, 7), {128, 128, 128, 128}, 2);
		expectNear(pixel(6, 16), {0, 0, 0, 0});
		expectNear(pixel(25, 16), {0, 0, 0, 0});
	}

	TEST_F(PrimitivesDrawTest, TwoOpaquePolygonsSharingAnEdgeShowNoSeam) {
		// Off the pixel grid, so the shared diagonal crosses pixels anywhere.
		const std::array<Foundation::Vec2, 3> first = {{{8.3F, 8.2F}, {24.7F, 8.2F}, {8.3F, 24.9F}}};
		const std::array<Foundation::Vec2, 3> second = {{{24.7F, 8.2F}, {24.7F, 24.9F}, {8.3F, 24.9F}}};
		const Foundation::Color				  fill(0.4F, 0.6F, 0.8F, 1.0F);
		render(Foundation::Color(0.0F, 0.0F, 0.0F, 1.0F), [&] {
			Primitives::drawPolygon({.vertices = first, .style = {.fill = fill}});
			Primitives::drawPolygon({.vertices = second, .style = {.fill = fill}});
		});
		for (int y = 9; y < 24; ++y) {
			for (int x = 9; x < 24; ++x) {
				expectNear(pixel(x, y), {102, 153, 204, 255});
			}
		}
	}

	TEST_F(PrimitivesDrawTest, ClearStoresPremultipliedColour) {
		RenderTarget half = RenderTarget::create(kSize, kSize);
		ASSERT_TRUE(half.isValid());
		drawInto(half, Foundation::Color(1.0F, 1.0F, 1.0F, 0.5F), [] {});
		std::vector<uint8_t> cleared(static_cast<size_t>(kSize) * kSize * 4, 0);
		half.readPixels(cleared);
		EXPECT_LE(std::abs(cleared[0] - 128), 1);
		EXPECT_LE(std::abs(cleared[3] - 128), 1);

		// Composited over black it is half white, as a premultiplied target must be.
		const Foundation::Rect bounds{0.0F, 0.0F, static_cast<float>(kSize), static_cast<float>(kSize)};
		render(Foundation::Color(0.0F, 0.0F, 0.0F, 1.0F), [&] { Primitives::drawImage({.texture = half.texture(), .bounds = bounds}); });
		expectNear(pixel(16, 16), {128, 128, 128, 255});
	}

	TEST_F(PrimitivesDrawTest, ARenderTargetDrawsUprightWithFlippedUvs) {
		RenderTarget source = RenderTarget::create(kSize, kSize);
		ASSERT_TRUE(source.isValid());
		const float size = static_cast<float>(kSize);
		drawInto(source, Foundation::Color(0.0F, 0.0F, 0.0F, 1.0F), [&] {
			Primitives::drawRect({.bounds = {0.0F, 0.0F, size, size * 0.5F}, .style = {.fill = Foundation::Color::red()}});
			Primitives::drawRect({.bounds = {0.0F, size * 0.5F, size, size * 0.5F}, .style = {.fill = Foundation::Color::blue()}});
		});
		const Foundation::Rect bounds{0.0F, 0.0F, size, size};

		render(Foundation::Color(0.0F, 0.0F, 0.0F, 1.0F), [&] {
			Primitives::drawImage({.texture = source.texture(), .bounds = bounds, .uvMin = {0.0F, 1.0F}, .uvMax = {1.0F, 0.0F}});
		});
		expectNear(pixel(16, 4), {255, 0, 0, 255});
		expectNear(pixel(16, 28), {0, 0, 255, 255});

		// With the default UVs it draws upside down: its bottom row is texture row 0.
		render(Foundation::Color(0.0F, 0.0F, 0.0F, 1.0F), [&] { Primitives::drawImage({.texture = source.texture(), .bounds = bounds}); });
		expectNear(pixel(16, 4), {0, 0, 255, 255});
		expectNear(pixel(16, 28), {255, 0, 0, 255});
	}

	TEST_F(PrimitivesDrawTest, ABatchPastTheStreamBuffersFirstSizeDrawsWhole) {
		// 2,048 quads, 8,192 vertices and 12,288 indices: twice what the stream buffers
		// start with. Only the last quad shows; a short upload would lose it.
		render(Foundation::Color(0.0F, 0.0F, 0.0F, 1.0F), [] {
			for (int i = 0; i < 2047; ++i) {
				fill({1.0F, 1.0F, 1.0F, 0.0F}, Primitives::BlendMode::Alpha);
			}
			fill({0.0F, 0.0F, 1.0F, 1.0F}, Primitives::BlendMode::Alpha);
		});
		expectNear(pixel(16, 16), {0, 0, 255, 255});
		expectNear(pixel(1, 30), {0, 0, 255, 255});
	}

	TEST_F(PrimitivesDrawTest, InstancedDrawsPastTheUniformRingEachReadTheirOwnCamera) {
		// One 1 x 1 square, drawn twice at each of the kSize * kSize pixels with that
		// pixel's camera and tint: 1,024 sets of uniforms, twice round the backend's ring
		// of 512, and each second draw a repeat that binds the slot just written. Every
		// pixel has its own camera, so a draw reading another's uniforms shows.
		renderer::TessellatedMesh square;
		square.vertices = {{0.0F, 0.0F}, {1.0F, 0.0F}, {1.0F, 1.0F}, {0.0F, 1.0F}};
		square.indices = {0, 1, 2, 0, 2, 3};
		InstancedMeshHandle mesh = Primitives::uploadInstancedMesh(square, 1);
		ASSERT_TRUE(mesh.isValid());
		const float half = static_cast<float>(kSize) * 0.5F;
		render(Foundation::Color(0.0F, 0.0F, 0.0F, 0.0F), [&] {
			for (int y = 0; y < kSize; ++y) {
				for (int x = 0; x < kSize; ++x) {
					const InstanceData instance({0.0F, 0.0F}, 0.0F, 1.0F, Foundation::Vec4(x / 31.0F, y / 31.0F, 0.5F, 1.0F));
					// The square lands at screen (x, y): world minus camera, plus the viewport's centre.
					const Primitives::WorldCamera camera{.position = {half - static_cast<float>(x), half - static_cast<float>(y)}};
					Primitives::drawInstanced(mesh, &instance, 1, camera);
					Primitives::drawInstanced(mesh, &instance, 1, camera);
				}
			}
		});
		for (int y = 0; y < kSize; ++y) {
			for (int x = 0; x < kSize; ++x) {
				const std::array<int, 4> expected = {(x * 255 + 15) / 31, (y * 255 + 15) / 31, 128, 255};
				expectNear(pixel(x, y), expected);
			}
		}
	}

} // namespace Renderer
