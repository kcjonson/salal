// The hook mechanism end to end: salal_base_shader() assembled tests/hooks/offset.vert.glsl into the
// base shader (offset_hook_shader) and tests/fill_hooks/stripes.frag.glsl into another
// (stripes_shader), and an instanced square or a rect is drawn through each into a RenderTarget on
// a hidden GL 3.3 core context and read back; skipped where none can be made (headless CI). The
// camera puts world (x, y) at pixel (x, y).

#include "backend/gl/GLBackend.h"
#include "generated/OffsetHookShader.h"
#include "generated/StripesShader.h"
#include "primitives/Primitives.h"
#include "resources/RenderTarget.h"
#include "vector/Types.h"

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/packing.hpp>
#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iterator>
#include <memory>
#include <span>
#include <vector>

namespace Renderer {

	// The block offset.vert.glsl declares, member for member, checked against what the build
	// reflected from the shader.
	struct OffsetUniforms {
		Foundation::Vec2 local{0.0F, 0.0F};
		Foundation::Vec2 world{0.0F, 0.0F};
	};
	static_assert(sizeof(OffsetUniforms) == offset_hook_shader::kOffset.size);
	static_assert(std::size(offset_hook_shader::kOffsetMembers) == 2);
	static_assert(offsetof(OffsetUniforms, local) == offset_hook_shader::kOffsetLocal.offset);
	static_assert(sizeof(OffsetUniforms::local) == offset_hook_shader::kOffsetLocal.size);
	static_assert(offsetof(OffsetUniforms, world) == offset_hook_shader::kOffsetWorld.offset);
	static_assert(sizeof(OffsetUniforms::world) == offset_hook_shader::kOffsetWorld.size);
	static_assert(offset_hook_shader::kOffset.binding == 1);

	// stripes.frag.glsl's block, padded to its std140 size.
	struct StripesUniforms {
		float shift = 0.0F;
		float padding[3] = {}; // NOLINT(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
	};
	static_assert(sizeof(StripesUniforms) == stripes_shader::kStripes.size);
	static_assert(offsetof(StripesUniforms, shift) == stripes_shader::kStripesShift.offset);
	static_assert(stripes_shader::kStripesFill.index == 1);

	// The renderer started on `shader` on a hidden GL 3.3 core context, drawing into a kSize square target.
	class HookShaderTest : public ::testing::Test {
	  protected:
		static constexpr int kSize = 32;

		explicit HookShaderTest(const backend::ShaderSource& (*source)())
			: shader(source) {}

		void SetUp() override {
			if (glfwInit() != GLFW_TRUE) {
				GTEST_SKIP() << "GLFW initialization failed (no display available in CI)";
			}
			glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
			glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
			glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
			glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
			glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
			window = glfwCreateWindow(kSize, kSize, "shader-hooks", nullptr, nullptr);
			if (window == nullptr) {
				glfwTerminate();
				GTEST_SKIP() << "Could not create GLFW window (no OpenGL context available)";
			}
			glfwMakeContextCurrent(window);
			std::unique_ptr<backend::gl::GLBackend> gl = backend::gl::GLBackend::create();
			if (gl == nullptr) {
				glfwDestroyWindow(window);
				window = nullptr;
				glfwTerminate();
				GTEST_SKIP() << "GLEW initialization failed";
			}
			Primitives::init(std::move(gl), shader());
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

		// Run `draw` into the target over transparent, then read it back.
		template <typename Draw>
		void render(Draw draw) {
			target.begin();
			Primitives::clear(Foundation::Color(0.0F, 0.0F, 0.0F, 0.0F));
			Primitives::beginFrame();
			draw();
			Primitives::endFrame();
			target.end();
			pixels.assign(static_cast<size_t>(kSize) * kSize * 4, 0);
			target.readPixels(pixels);
		}

		// The pixel at (x, y), y down, as RGBA8; readPixels rows run bottom up.
		std::array<int, 4> pixel(int x, int y) const {
			const size_t at = ((static_cast<size_t>(kSize - 1 - y) * kSize) + static_cast<size_t>(x)) * 4;
			return {pixels[at], pixels[at + 1], pixels[at + 2], pixels[at + 3]};
		}

		// Whether the pixel at (x, y) is covered.
		bool lit(int x, int y) const { return pixel(x, y)[3] == 255; }

		int litCount() const {
			int count = 0;
			for (size_t i = 3; i < pixels.size(); i += 4) {
				count += pixels[i] == 255 ? 1 : 0;
			}
			return count;
		}

		GLFWwindow*			 window = nullptr;
		RenderTarget		 target;
		std::vector<uint8_t> pixels;

	  private:
		const backend::ShaderSource& (*shader)();
	};

	class ShaderHooksTest : public HookShaderTest {
	  protected:
		ShaderHooksTest()
			: HookShaderTest(offset_hook_shader::source) {}

		void SetUp() override {
			HookShaderTest::SetUp();
			if (IsSkipped() || HasFatalFailure()) {
				return;
			}
			renderer::TessellatedMesh unit;
			unit.vertices = {{0.0F, 0.0F}, {1.0F, 0.0F}, {1.0F, 1.0F}, {0.0F, 1.0F}};
			unit.indices = {0, 1, 2, 0, 2, 3};
			mesh = Primitives::uploadInstancedMesh(unit, 1);
			ASSERT_TRUE(mesh.isValid());
		}

		void TearDown() override {
			mesh = InstancedMeshHandle();
			HookShaderTest::TearDown();
		}

		// The unit square at world `position`, `scale` times its size.
		void square(Foundation::Vec2 position, float scale = 1.0F) const {
			const InstanceData instance(position, 0.0F, scale, Foundation::Vec4(1.0F));
			const float		   half = static_cast<float>(kSize) * 0.5F;
			Primitives::drawInstanced(mesh, &instance, 1, {.position = {half, half}});
		}

		static void offsets(const OffsetUniforms& values) {
			Primitives::setHookUniforms(offset_hook_shader::kOffset, std::as_bytes(std::span(&values, 1)));
		}

		InstancedMeshHandle mesh;
	};

	TEST_F(ShaderHooksTest, AZeroedBlockLeavesEveryVertexWhereItWas) {
		render([&] { square({4.0F, 4.0F}); });
		EXPECT_TRUE(lit(4, 4));
		EXPECT_EQ(litCount(), 1);
	}

	TEST_F(ShaderHooksTest, TheWorldPointMovesTheInstanceAfterItsTransform) {
		render([&] {
			offsets({.world = {5.0F, 3.0F}});
			square({4.0F, 4.0F}, 2.0F);
		});
		EXPECT_FALSE(lit(4, 4));
		EXPECT_TRUE(lit(9, 7));
		EXPECT_TRUE(lit(10, 8));
		EXPECT_EQ(litCount(), 4);
	}

	TEST_F(ShaderHooksTest, TheLocalPointMovesItInMeshSpaceSoTheScaleApplies) {
		render([&] {
			offsets({.local = {2.0F, 1.0F}});
			square({4.0F, 4.0F}, 3.0F);
		});
		// A 3 x 3 square, moved by (2, 1) * 3.
		EXPECT_FALSE(lit(4, 4));
		EXPECT_TRUE(lit(10, 7));
		EXPECT_TRUE(lit(12, 9));
		EXPECT_FALSE(lit(13, 10));
		EXPECT_EQ(litCount(), 9);
	}

	TEST_F(ShaderHooksTest, ADrawKeepsTheValuesItWasIssuedWith) {
		render([&] {
			square({4.0F, 4.0F});
			offsets({.world = {10.0F, 0.0F}});
			square({4.0F, 20.0F});
			const OffsetUniforms back{.world = {0.0F, 2.0F}};
			Primitives::setHookUniforms("offset", std::as_bytes(std::span(&back, 1)));
			square({20.0F, 4.0F});
		});
		EXPECT_TRUE(lit(4, 4));	  // before any offset
		EXPECT_TRUE(lit(14, 20)); // the second set
		EXPECT_TRUE(lit(20, 6));  // the third, by name
		EXPECT_EQ(litCount(), 3);
	}

	TEST_F(ShaderHooksTest, ValuesOfTheWrongSizeAreIgnored) {
		render([&] {
			const Foundation::Vec2 half{5.0F, 5.0F};
			Primitives::setHookUniforms(offset_hook_shader::kOffset, std::as_bytes(std::span(&half, 1)));
			Primitives::setHookUniforms("no_such_hook", std::as_bytes(std::span(&half, 1)));
			square({4.0F, 4.0F});
		});
		EXPECT_TRUE(lit(4, 4));
		EXPECT_EQ(litCount(), 1);
	}

	// stripes.frag.glsl over opaque red and blue, four units a stripe. Its pixels a stripe's edge
	// doesn't cross are exactly one colour; the rects' own edges ramp over a pixel, so the checks
	// keep off them.
	class FillHooksTest : public HookShaderTest {
	  protected:
		static constexpr std::array<int, 4> kRed = {255, 0, 0, 255};
		static constexpr std::array<int, 4> kBlue = {0, 0, 255, 255};
		static constexpr std::array<int, 4> kWhite = {255, 255, 255, 255};
		static constexpr std::array<int, 4> kClear = {0, 0, 0, 0};

		FillHooksTest()
			: HookShaderTest(stripes_shader::source) {}

		static Primitives::Fill stripes(float width) {
			return {
				.hook = &stripes_shader::kStripesFill,
				.params = {
					glm::packUnorm4x8(glm::vec4(1.0F, 0.0F, 0.0F, 1.0F)),
					glm::packUnorm4x8(glm::vec4(0.0F, 0.0F, 1.0F, 1.0F)),
					std::bit_cast<uint32_t>(width)
				},
			};
		}

		static void shift(float by) {
			const StripesUniforms values{.shift = by};
			Primitives::setHookUniforms(stripes_shader::kStripes, std::as_bytes(std::span(&values, 1)));
		}

		// Each channel within one of `expected`'s.
		void expectPixel(int x, int y, const std::array<int, 4>& expected) const {
			const std::array<int, 4> actual = pixel(x, y);
			for (size_t c = 0; c < 4; ++c) {
				EXPECT_LE(std::abs(actual[c] - expected[c]), 1) << "pixel (" << x << ", " << y << ") channel " << c;
			}
		}
	};

	TEST_F(FillHooksTest, TheStripesAreTheColoursItsParamsGiveAndAPlainRectKeepsItsFill) {
		render([&] {
			Primitives::drawRect(
				{.bounds = {0.0F, 0.0F, 32.0F, 16.0F}, .style = {.fill = Foundation::Color::white()}, .fill = stripes(4.0F)}
			);
			Primitives::drawRect({.bounds = {0.0F, 16.0F, 32.0F, 16.0F}, .style = {.fill = Foundation::Color::white()}});
		});
		// Centres at x + 0.5: stripe 0 is [0, 4), red, stripe 1 [4, 8), blue, and so on.
		expectPixel(1, 8, kRed);
		expectPixel(3, 8, kRed);
		expectPixel(4, 8, kBlue);
		expectPixel(7, 8, kBlue);
		expectPixel(9, 8, kRed);
		expectPixel(29, 8, kBlue);
		expectPixel(9, 24, kWhite);
	}

	TEST_F(FillHooksTest, ItsBlockShiftsItAndAPixelOnAnEdgeIsHalfEachColour) {
		render([&] {
			shift(0.5F);
			Primitives::drawRect(
				{.bounds = {0.0F, 0.0F, 32.0F, 32.0F}, .style = {.fill = Foundation::Color::white()}, .fill = stripes(4.0F)}
			);
		});
		// Edges at 0.5, 4.5, 8.5: the centre 4.5 is on one, so pixelSize is one pixel there.
		expectPixel(2, 16, kRed);
		expectPixel(4, 16, {128, 0, 128, 255});
		expectPixel(6, 16, kBlue);
	}

	TEST_F(FillHooksTest, ARectDrawnBeforeItsBlockChangesKeepsTheValuesItWasDrawnUnder) {
		render([&] {
			Primitives::drawRect(
				{.bounds = {0.0F, 0.0F, 32.0F, 16.0F}, .style = {.fill = Foundation::Color::white()}, .fill = stripes(4.0F)}
			);
			shift(4.0F);
			Primitives::drawRect(
				{.bounds = {0.0F, 16.0F, 32.0F, 16.0F}, .style = {.fill = Foundation::Color::white()}, .fill = stripes(4.0F)}
			);
		});
		expectPixel(1, 8, kRed);
		expectPixel(1, 24, kBlue); // shifted a stripe
	}

	TEST_F(FillHooksTest, TheBorderCornersAndClipLieOverTheHookAsOverAFlatFill) {
		render([&] {
			Primitives::pushClip({0.0F, 0.0F, 16.0F, 32.0F});
			Primitives::drawRect(
				{.bounds = {0.0F, 0.0F, 32.0F, 32.0F},
				 .style =
					 {.fill = Foundation::Color::white(),
					  .border =
						  Foundation::BorderStyle{
							  .color = Foundation::Color::white(),
							  .width = 4.0F,
							  .cornerRadius = 8.0F,
							  .position = Foundation::BorderPosition::Inside
						  }},
				 .fill = stripes(4.0F)}
			);
			Primitives::popClip();
		});
		expectPixel(2, 16, kWhite); // the border's band, inside the edge
		expectPixel(5, 16, kBlue);	// past it, the hook
		expectPixel(13, 16, kBlue);
		expectPixel(0, 0, kClear);	 // the rounded corner
		expectPixel(20, 16, kClear); // the clip
	}

} // namespace Renderer
