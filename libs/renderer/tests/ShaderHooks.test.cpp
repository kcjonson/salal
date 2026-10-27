// The hook mechanism end to end: salal_base_shader() assembled tests/hooks/offset.vert.glsl
// into the base shader (offset_hook_shader), and an instanced square is drawn through it
// into a RenderTarget on a hidden GL 3.3 core context and read back; skipped where none can
// be made (headless CI). The camera puts world (x, y) at pixel (x, y).

#include "backend/gl/GLBackend.h"
#include "generated/OffsetHookShader.h"
#include "primitives/Primitives.h"
#include "resources/RenderTarget.h"
#include "vector/Types.h"

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
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

	class ShaderHooksTest : public ::testing::Test {
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
			Primitives::init(std::move(gl), offset_hook_shader::source());
			Primitives::setCoordinateSystem(nullptr);
			Primitives::setViewport(kSize, kSize);
			target = RenderTarget::create(kSize, kSize);
			ASSERT_TRUE(target.isValid());

			renderer::TessellatedMesh square;
			square.vertices = {{0.0F, 0.0F}, {1.0F, 0.0F}, {1.0F, 1.0F}, {0.0F, 1.0F}};
			square.indices = {0, 1, 2, 0, 2, 3};
			mesh = Primitives::uploadInstancedMesh(square, 1);
			ASSERT_TRUE(mesh.isValid());
		}

		void TearDown() override {
			if (window != nullptr) {
				mesh = InstancedMeshHandle();
				target = RenderTarget();
				Primitives::shutdown();
				glfwDestroyWindow(window);
				glfwTerminate();
			}
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

		// Whether the pixel at (x, y), y down, is covered; readPixels rows run bottom up.
		bool lit(int x, int y) const {
			return pixels[(((static_cast<size_t>(kSize - 1 - y) * kSize) + static_cast<size_t>(x)) * 4) + 3] == 255;
		}

		int litCount() const {
			int count = 0;
			for (size_t i = 3; i < pixels.size(); i += 4) {
				count += pixels[i] == 255 ? 1 : 0;
			}
			return count;
		}

		GLFWwindow*			 window = nullptr;
		RenderTarget		 target;
		InstancedMeshHandle	 mesh;
		std::vector<uint8_t> pixels;
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

} // namespace Renderer
