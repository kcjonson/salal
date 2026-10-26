// The lookups need no GL context and the headless cases below make no GL call
// (a GL call with no context is undefined on macOS); everything that creates a
// texture runs on a hidden GL 3.3 core context and skips where none can be made.

#include "backend/gl/GLTextures.h"

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

namespace Renderer::backend::gl {

	TEST(GLTexturesTest, InvalidHandleNamesNothing) {
		EXPECT_EQ(name(TextureHandle{}), 0U);
	}

	TEST(GLTexturesTest, OutOfRangeIndexNamesNothing) {
		EXPECT_EQ(name(TextureHandle::make(999, 0)), 0U);
	}

	TEST(GLTexturesTest, UpdateAndDestroyOfInvalidHandleAreHarmless) {
		const std::array<uint8_t, 4> pixel{};
		updateTexture(TextureHandle{}, 0, 0, 1, 1, pixel.data());
		destroyTexture(TextureHandle{});
		destroyTexture(TextureHandle::make(999, 0));
	}

	class GLTexturesContextTest : public ::testing::Test {
	  protected:
		void SetUp() override {
			if (glfwInit() != GLFW_TRUE) {
				GTEST_SKIP() << "GLFW initialization failed (no display available)";
			}
			glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
			glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
			glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
			glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
			glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
			window = glfwCreateWindow(32, 32, "textures-test", nullptr, nullptr);
			if (window == nullptr) {
				glfwTerminate();
				GTEST_SKIP() << "Could not create GLFW window (no OpenGL context available)";
			}
			glfwMakeContextCurrent(window);
			glewExperimental = GL_TRUE;
			if (glewInit() != GLEW_OK) {
				glfwDestroyWindow(window);
				window = nullptr;
				glfwTerminate();
				GTEST_SKIP() << "GLEW initialization failed";
			}
		}

		void TearDown() override {
			if (window != nullptr) {
				glfwDestroyWindow(window);
				glfwTerminate();
			}
		}

		// The texture's level 0 as the GL side stores it, tightly packed.
		static std::vector<uint8_t> readBack(TextureHandle handle, int width, int height, int channels) {
			std::vector<uint8_t> pixels(static_cast<size_t>(width * height * channels));
			glPixelStorei(GL_PACK_ALIGNMENT, 1);
			glBindTexture(GL_TEXTURE_2D, name(handle));
			glGetTexImage(GL_TEXTURE_2D, 0, channels == 3 ? GL_RGB : GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
			glBindTexture(GL_TEXTURE_2D, 0);
			glPixelStorei(GL_PACK_ALIGNMENT, 4);
			return pixels;
		}

		static GLint texParameter(TextureHandle handle, GLenum parameter) {
			GLint value = 0;
			glBindTexture(GL_TEXTURE_2D, name(handle));
			glGetTexParameteriv(GL_TEXTURE_2D, parameter, &value);
			glBindTexture(GL_TEXTURE_2D, 0);
			return value;
		}

		static GLint internalFormat(TextureHandle handle) {
			GLint value = 0;
			glBindTexture(GL_TEXTURE_2D, name(handle));
			glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &value);
			glBindTexture(GL_TEXTURE_2D, 0);
			return value;
		}

		GLFWwindow* window = nullptr;
	};

	TEST_F(GLTexturesContextTest, CreatedTextureNamesAGLTexture) {
		const TextureHandle handle = createTexture({.width = 4, .height = 4});
		ASSERT_TRUE(handle.isValid());
		EXPECT_NE(name(handle), 0U);
		EXPECT_TRUE(glIsTexture(name(handle)));
		EXPECT_EQ(internalFormat(handle), GL_RGBA8);
		EXPECT_EQ(texParameter(handle, GL_TEXTURE_MIN_FILTER), GL_LINEAR);
		EXPECT_EQ(texParameter(handle, GL_TEXTURE_MAG_FILTER), GL_LINEAR);
		EXPECT_EQ(texParameter(handle, GL_TEXTURE_WRAP_S), GL_CLAMP_TO_EDGE);
		EXPECT_EQ(texParameter(handle, GL_TEXTURE_WRAP_T), GL_CLAMP_TO_EDGE);
		GLint bound = -1;
		glGetIntegerv(GL_TEXTURE_BINDING_2D, &bound);
		EXPECT_EQ(bound, 0); // left unbound, as every creator unbound by hand before
		destroyTexture(handle);
	}

	TEST_F(GLTexturesContextTest, DestroyedHandleIsStale) {
		const TextureHandle handle = createTexture({.width = 2, .height = 2});
		const GLuint		glName = name(handle);
		destroyTexture(handle);
		EXPECT_EQ(name(handle), 0U);
		EXPECT_FALSE(glIsTexture(glName));
		destroyTexture(handle); // twice is harmless
	}

	TEST_F(GLTexturesContextTest, ReusedSlotGetsNewGeneration) {
		const TextureHandle stale = createTexture({.width = 2, .height = 2});
		destroyTexture(stale);
		const TextureHandle fresh = createTexture({.width = 2, .height = 2});
		EXPECT_EQ(fresh.index(), stale.index());
		EXPECT_NE(fresh.generation(), stale.generation());
		EXPECT_EQ(name(stale), 0U);
		EXPECT_NE(name(fresh), 0U);
		destroyTexture(fresh);
	}

	TEST_F(GLTexturesContextTest, DistinctHandlesPerTexture) {
		const TextureHandle a = createTexture({.width = 2, .height = 2});
		const TextureHandle b = createTexture({.width = 2, .height = 2});
		EXPECT_NE(a, b);
		EXPECT_NE(name(a), name(b));
		destroyTexture(a);
		EXPECT_EQ(name(a), 0U);
		EXPECT_NE(name(b), 0U);
		destroyTexture(b);
	}

	TEST_F(GLTexturesContextTest, PixelsUploadAndUpdate) {
		const std::array<uint8_t, 16> initial{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
		const TextureHandle			  handle = createTexture({.width = 2, .height = 2, .pixels = initial.data()});
		ASSERT_TRUE(handle.isValid());
		EXPECT_EQ(readBack(handle, 2, 2, 4), std::vector<uint8_t>(initial.begin(), initial.end()));

		const std::array<uint8_t, 4> patch{200, 201, 202, 203};
		updateTexture(handle, 1, 1, 1, 1, patch.data());
		std::vector<uint8_t> expected(initial.begin(), initial.end());
		std::copy(patch.begin(), patch.end(), expected.begin() + 12);
		EXPECT_EQ(readBack(handle, 2, 2, 4), expected);
		destroyTexture(handle);
	}

	TEST_F(GLTexturesContextTest, Rgb8RowsNeedNoPadding) {
		// 3 x 2 RGB: nine-byte rows, which the default four-byte unpack alignment would misread.
		const std::array<uint8_t, 18> pixels{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18};
		const TextureHandle handle = createTexture({.width = 3, .height = 2, .format = TextureFormat::Rgb8, .pixels = pixels.data()});
		ASSERT_TRUE(handle.isValid());
		EXPECT_EQ(internalFormat(handle), GL_RGB8);
		EXPECT_EQ(readBack(handle, 3, 2, 3), std::vector<uint8_t>(pixels.begin(), pixels.end()));
		GLint alignment = 0;
		glGetIntegerv(GL_UNPACK_ALIGNMENT, &alignment);
		EXPECT_EQ(alignment, 4); // the default is back for everyone else
		destroyTexture(handle);
	}

	TEST_F(GLTexturesContextTest, FormatsAndFiltering) {
		const TextureHandle nearest = createTexture({.width = 2, .height = 2, .linear = false});
		EXPECT_EQ(texParameter(nearest, GL_TEXTURE_MIN_FILTER), GL_NEAREST);
		EXPECT_EQ(texParameter(nearest, GL_TEXTURE_MAG_FILTER), GL_NEAREST);
		destroyTexture(nearest);

		const TextureHandle half = createTexture({.width = 2, .height = 2, .format = TextureFormat::Rgba16F});
		EXPECT_EQ(internalFormat(half), GL_RGBA16F);
		destroyTexture(half);

		const TextureHandle single = createTexture({.width = 2, .height = 2, .format = TextureFormat::Rgba32F});
		EXPECT_EQ(internalFormat(single), GL_RGBA32F);
		destroyTexture(single);
	}

	TEST_F(GLTexturesContextTest, EmptySizeIsRefused) {
		EXPECT_FALSE(createTexture({.width = 0, .height = 4}).isValid());
		EXPECT_FALSE(createTexture({.width = 4, .height = -1}).isValid());
	}

	TEST_F(GLTexturesContextTest, UpdateOutsideTheTextureIsRefused) {
		const std::array<uint8_t, 16> initial{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
		const TextureHandle			  handle = createTexture({.width = 2, .height = 2, .pixels = initial.data()});
		ASSERT_TRUE(handle.isValid());

		const std::array<uint8_t, 16> zeros{};
		updateTexture(handle, 1, 1, 2, 2, zeros.data()); // overhangs right and bottom
		updateTexture(handle, -1, 0, 1, 1, zeros.data());
		updateTexture(handle, 0, 0, 0, 1, zeros.data());
		updateTexture(handle, 0, 0, 3, 1, zeros.data());
		EXPECT_EQ(readBack(handle, 2, 2, 4), std::vector<uint8_t>(initial.begin(), initial.end()));
		destroyTexture(handle);
	}

} // namespace Renderer::backend::gl
