// What the uber program writes, drawn through Primitives into a RenderTarget
// against a hidden GL 3.3 core context and read back; skipped where no context
// can be made (headless CI). Output is premultiplied: a colour c at coverage a
// over transparent stores (c * a, a), and an additive draw adds c * a and leaves
// the destination's alpha alone.

#include "primitives/Primitives.h"

#include "backend/gl/GLBackend.h"
#include "resources/RenderTarget.h"
#include "vector/Types.h"

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <gtest/gtest.h>

#include <array>
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
			Primitives::init(std::move(gl));
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
