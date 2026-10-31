// RenderTarget on the null backend, headless, and against a hidden GL 3.3 core
// context, skipped where none can be made (headless CI). The GL queries here are
// the test's, checking what the renderer's own target stack bound; the renderer
// never asks.

#include "resources/RenderTarget.h"

#include "BaseShader.h"
#include "CoordinateSystem/CoordinateSystem.h"
#include "backend/NullBackend.h"
#include "backend/gl/GLBackend.h"
#include "primitives/Primitives.h"

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace Renderer {

	// --- On the null backend, headless: the target stack and what reaches the backend ---

	class RenderTargetNullTest : public ::testing::Test {
	  protected:
		void SetUp() override {
			auto owned = std::make_unique<backend::NullBackend>();
			gpu = owned.get();
			Primitives::init(std::move(owned), base_shader::source());
			Primitives::setCoordinateSystem(nullptr);
			Primitives::setViewport(32, 32);
			gpu->clearRecord();
		}

		void TearDown() override { Primitives::shutdown(); }

		backend::NullBackend* gpu = nullptr;
	};

	TEST_F(RenderTargetNullTest, CreateMakesATextureAndATargetThatGoWithIt) {
		const backend::NullBackend::Counts before = gpu->live();
		{
			RenderTarget target = RenderTarget::create(8, 4, TextureFormat::Rgba16F);
			ASSERT_TRUE(target.isValid());
			ASSERT_NE(gpu->texture(target.texture()), nullptr);
			EXPECT_EQ(gpu->texture(target.texture())->width, 8);
			EXPECT_EQ(gpu->texture(target.texture())->height, 4);
			EXPECT_EQ(gpu->texture(target.texture())->format, TextureFormat::Rgba16F);
			EXPECT_EQ(gpu->live().textures, before.textures + 1);
			EXPECT_EQ(gpu->live().targets, before.targets + 1);
		}
		EXPECT_EQ(gpu->live().textures, before.textures);
		EXPECT_EQ(gpu->live().targets, before.targets);
	}

	TEST_F(RenderTargetNullTest, BeginAndEndBindFromTheStack) {
		RenderTarget outer = RenderTarget::create(8, 8);
		RenderTarget inner = RenderTarget::create(2, 2);
		outer.begin();
		inner.begin();
		inner.end();
		outer.end();
		ASSERT_EQ(gpu->targetsBound().size(), 4U);
		const RenderTargetHandle outerHandle = gpu->targetsBound()[0];
		EXPECT_TRUE(outerHandle.isValid());
		EXPECT_NE(gpu->targetsBound()[1], outerHandle);
		EXPECT_EQ(gpu->targetsBound()[2], outerHandle);
		EXPECT_FALSE(gpu->targetsBound()[3].isValid()); // the window
	}

	TEST_F(RenderTargetNullTest, MisnestedEndIsRefusedAndTheWindowSizeWaits) {
		RenderTarget outer = RenderTarget::create(8, 8);
		RenderTarget inner = RenderTarget::create(2, 2);
		outer.begin();
		inner.begin();
		outer.end(); // refused: inner is still open
		EXPECT_EQ(gpu->targetsBound().size(), 2U);
		EXPECT_TRUE(RenderTarget::anyBegun());

		// A begun target keeps the window's recorded size where it was.
		Primitives::setViewport(16, 16);
		EXPECT_EQ(gpu->windowWidth(), 32);

		inner.end();
		outer.end();
		EXPECT_FALSE(RenderTarget::anyBegun());
		ASSERT_EQ(gpu->targetsBound().size(), 4U);
		EXPECT_FALSE(gpu->targetsBound()[3].isValid());
		Primitives::setViewport(16, 16);
		EXPECT_EQ(gpu->windowWidth(), 16);
	}

	TEST_F(RenderTargetNullTest, ClearPremultipliesAndReadPixelsReachesTheBackend) {
		RenderTarget target = RenderTarget::create(2, 2);
		target.begin();
		Primitives::clear(Foundation::Color(1.0F, 0.5F, 0.0F, 0.5F));
		target.end();
		ASSERT_EQ(gpu->clears().size(), 1U);
		EXPECT_EQ(gpu->clears()[0].r, 0.5F);
		EXPECT_EQ(gpu->clears()[0].g, 0.25F);
		EXPECT_EQ(gpu->clears()[0].a, 0.5F);

		std::vector<uint8_t> pixels(2 * 2 * 4, 7);
		target.readPixels(pixels);
		EXPECT_EQ(pixels, std::vector<uint8_t>(2 * 2 * 4, 0)); // the null backend reads zeros
		std::vector<uint8_t> tooShort(3, 7);
		target.readPixels(tooShort);
		EXPECT_EQ(tooShort, std::vector<uint8_t>(3, 7));
	}

	TEST(RenderTargetNoBackendTest, ATargetKeptAcrossReinitLeavesTheNextSessionsTexturesAlone) {
		// A target made in one session and destroyed in the next, after the next has
		// made a texture in the same slot (a font atlas, say).
		Primitives::init(std::make_unique<backend::NullBackend>(), base_shader::source());
		RenderTarget kept = RenderTarget::create(4, 4);
		ASSERT_TRUE(kept.isValid());
		Primitives::shutdown();

		auto				  owned = std::make_unique<backend::NullBackend>();
		backend::NullBackend& next = *owned;
		Primitives::init(std::move(owned), base_shader::source());
		const Texture		atlasTexture = Primitives::createTexture({.width = 8, .height = 8});
		const TextureHandle atlas = atlasTexture.handle();
		ASSERT_TRUE(atlas.isValid());
		EXPECT_NE(atlas, kept.texture());
		kept = RenderTarget(); // releases its stale handles through the new backend
		ASSERT_NE(next.texture(atlas), nullptr);
		EXPECT_EQ(next.texture(atlas)->width, 8);
		EXPECT_EQ(next.live().textures, 1U);
		Primitives::shutdown();
	}

	TEST(RenderTargetNoBackendTest, CreateBeforeInitIsInvalid) {
		const RenderTarget target = RenderTarget::create(4, 4);
		EXPECT_FALSE(target.isValid());
	}

	// --- Against the GL backend ---

	class RenderTargetTest : public ::testing::Test {
	  protected:
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
			// The GL backend initialises GLEW on this context
			std::unique_ptr<backend::gl::GLBackend> gl = backend::gl::GLBackend::create();
			if (gl == nullptr) {
				glfwDestroyWindow(window);
				window = nullptr;
				glfwTerminate();
				GTEST_SKIP() << "GLEW initialization failed";
			}
			Primitives::init(std::move(gl), base_shader::source());
			// What the platform layer does: the window's viewport, told to the renderer.
			glViewport(0, 0, 32, 32);
			Primitives::setViewport(32, 32);
		}

		void TearDown() override {
			if (window != nullptr) {
				Primitives::shutdown();
				glfwDestroyWindow(window);
				glfwTerminate();
			}
		}

		static GLint boundFramebuffer() {
			GLint fbo = -1;
			glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fbo);
			return fbo;
		}

		static std::array<GLint, 4> viewport() {
			std::array<GLint, 4> rect{};
			glGetIntegerv(GL_VIEWPORT, rect.data());
			return rect;
		}

		static void expectSolid(const std::vector<uint8_t>& pixels, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
			for (size_t i = 0; i + 3 < pixels.size(); i += 4) {
				ASSERT_EQ(pixels[i + 0], r) << "pixel " << i / 4;
				ASSERT_EQ(pixels[i + 1], g) << "pixel " << i / 4;
				ASSERT_EQ(pixels[i + 2], b) << "pixel " << i / 4;
				ASSERT_EQ(pixels[i + 3], a) << "pixel " << i / 4;
			}
		}

		GLFWwindow* window = nullptr;
	};

	TEST_F(RenderTargetTest, ClearReadsBack) {
		RenderTarget target = RenderTarget::create(4, 4);
		ASSERT_TRUE(target.isValid());
		EXPECT_TRUE(target.texture().isValid());
		EXPECT_EQ(target.width(), 4);
		EXPECT_EQ(target.height(), 4);

		target.begin();
		Primitives::clear(Foundation::Color(1.0F, 0.0F, 0.0F, 1.0F));
		target.end();

		std::vector<uint8_t> pixels(4 * 4 * 4, 0);
		target.readPixels(pixels);
		expectSolid(pixels, 255, 0, 0, 255);

		// The texture itself holds the same, for whoever samples it.
		std::vector<uint8_t> texels(4 * 4 * 4, 0);
		glBindTexture(GL_TEXTURE_2D, backend::gl::name(target.texture()));
		glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, texels.data());
		glBindTexture(GL_TEXTURE_2D, 0);
		EXPECT_EQ(texels, pixels);
	}

	TEST_F(RenderTargetTest, BeginBindsAndEndRestoresTheBackbuffer) {
		RenderTarget target = RenderTarget::create(4, 8);
		ASSERT_TRUE(target.isValid());
		EXPECT_EQ(boundFramebuffer(), 0);

		target.begin();
		EXPECT_NE(boundFramebuffer(), 0);
		EXPECT_EQ(viewport(), (std::array<GLint, 4>{0, 0, 4, 8}));

		target.end();
		EXPECT_EQ(boundFramebuffer(), 0);
		EXPECT_EQ(viewport(), (std::array<GLint, 4>{0, 0, 32, 32}));
	}

	TEST_F(RenderTargetTest, NestedTargetsUnwindInOrder) {
		RenderTarget outer = RenderTarget::create(8, 8);
		RenderTarget inner = RenderTarget::create(2, 2);
		ASSERT_TRUE(outer.isValid());
		ASSERT_TRUE(inner.isValid());

		outer.begin();
		const GLint outerFbo = boundFramebuffer();
		inner.begin();
		EXPECT_NE(boundFramebuffer(), outerFbo);
		EXPECT_EQ(viewport(), (std::array<GLint, 4>{0, 0, 2, 2}));

		inner.end();
		EXPECT_EQ(boundFramebuffer(), outerFbo);
		EXPECT_EQ(viewport(), (std::array<GLint, 4>{0, 0, 8, 8}));

		outer.end();
		EXPECT_EQ(boundFramebuffer(), 0);
		EXPECT_EQ(viewport(), (std::array<GLint, 4>{0, 0, 32, 32}));
	}

	TEST_F(RenderTargetTest, ReadPixelsWhileBegunLeavesTheTargetBound) {
		RenderTarget target = RenderTarget::create(2, 2);
		ASSERT_TRUE(target.isValid());

		target.begin();
		Primitives::clear(Foundation::Color(0.0F, 1.0F, 0.0F, 1.0F));
		const GLint			 fbo = boundFramebuffer();
		std::vector<uint8_t> pixels(2 * 2 * 4, 0);
		target.readPixels(pixels);
		EXPECT_EQ(boundFramebuffer(), fbo);
		target.end();

		expectSolid(pixels, 0, 255, 0, 255);
	}

	TEST_F(RenderTargetTest, ReadPixelsRefusesAShortBuffer) {
		RenderTarget target = RenderTarget::create(2, 2);
		ASSERT_TRUE(target.isValid());
		target.begin();
		Primitives::clear(Foundation::Color(0.0F, 0.0F, 1.0F, 1.0F));
		target.end();

		std::vector<uint8_t> tooShort(2 * 2 * 4 - 1, 7);
		target.readPixels(tooShort);
		EXPECT_EQ(tooShort, std::vector<uint8_t>(2 * 2 * 4 - 1, 7));
	}

	TEST_F(RenderTargetTest, DestroyingTheTargetFreesItsTexture) {
		TextureHandle texture;
		{
			RenderTarget target = RenderTarget::create(2, 2);
			ASSERT_TRUE(target.isValid());
			texture = target.texture();
			EXPECT_NE(backend::gl::name(texture), 0U);
		}
		EXPECT_EQ(backend::gl::name(texture), 0U);
	}

	TEST_F(RenderTargetTest, MovedFromTargetIsInvalid) {
		RenderTarget source = RenderTarget::create(2, 2);
		ASSERT_TRUE(source.isValid());
		const TextureHandle texture = source.texture();

		RenderTarget moved(std::move(source));
		EXPECT_FALSE(source.isValid()); // NOLINT(bugprone-use-after-move)
		EXPECT_TRUE(moved.isValid());
		EXPECT_EQ(moved.texture(), texture);
		EXPECT_NE(backend::gl::name(texture), 0U);
	}

	TEST_F(RenderTargetTest, EmptySizeIsInvalidAndInert) {
		RenderTarget target = RenderTarget::create(0, 4);
		EXPECT_FALSE(target.isValid());
		EXPECT_FALSE(target.texture().isValid());
		EXPECT_EQ(target.width(), 0);

		target.begin();
		EXPECT_EQ(boundFramebuffer(), 0);
		target.end();
		std::vector<uint8_t> pixels(4, 9);
		target.readPixels(pixels);
		EXPECT_EQ(pixels, std::vector<uint8_t>(4, 9));
	}

	TEST_F(RenderTargetTest, ReadBackbufferReportsTheRecordedSize) {
		int width = 0;
		int height = 0;
		Primitives::readBackbuffer({}, width, height);
		EXPECT_EQ(width, 32);
		EXPECT_EQ(height, 32);

		glClearColor(0.0F, 0.0F, 1.0F, 1.0F);
		glClear(GL_COLOR_BUFFER_BIT);
		std::vector<uint8_t> pixels(static_cast<size_t>(width * height * 4), 0);
		Primitives::readBackbuffer(pixels, width, height);
		expectSolid(pixels, 0, 0, 255, 255);

		// A begun target changes neither the size reported nor what is read.
		RenderTarget target = RenderTarget::create(2, 2);
		target.begin();
		Primitives::clear(Foundation::Color(1.0F, 1.0F, 1.0F, 1.0F));
		Primitives::readBackbuffer(pixels, width, height);
		EXPECT_EQ(width, 32);
		expectSolid(pixels, 0, 0, 255, 255);
		target.end();
	}

	TEST_F(RenderTargetTest, BackbufferSizeIsTheWindowFramebufferWhenACoordinateSystemIsSet) {
		// What the app does on a HiDPI window: the framebuffer is larger than the logical
		// size a world pass may hand setViewport; the backbuffer keeps the framebuffer size.
		CoordinateSystem coordinateSystem;
		coordinateSystem.setViewport({.logicalWidth = 16, .logicalHeight = 16, .framebufferWidth = 32, .framebufferHeight = 32});
		Primitives::setCoordinateSystem(&coordinateSystem);
		Primitives::setViewport(16, 16);
		int width = 0;
		int height = 0;
		Primitives::readBackbuffer({}, width, height);
		EXPECT_EQ(width, 32);
		EXPECT_EQ(height, 32);

		Primitives::setCoordinateSystem(nullptr);
		Primitives::setViewport(16, 16);
		Primitives::readBackbuffer({}, width, height);
		EXPECT_EQ(width, 16);
		Primitives::setViewport(32, 32);
	}

	TEST_F(RenderTargetTest, MisnestedEndIsRefusedAndTheStackRecovers) {
		RenderTarget outer = RenderTarget::create(8, 8);
		RenderTarget inner = RenderTarget::create(2, 2);
		ASSERT_TRUE(outer.isValid());
		ASSERT_TRUE(inner.isValid());

		outer.begin();
		const GLint outerFbo = boundFramebuffer();
		inner.begin();
		const GLint innerFbo = boundFramebuffer();

		outer.end(); // refused: inner is still open
		EXPECT_EQ(boundFramebuffer(), innerFbo);
		EXPECT_EQ(viewport(), (std::array<GLint, 4>{0, 0, 2, 2}));

		inner.end();
		EXPECT_EQ(boundFramebuffer(), outerFbo);
		EXPECT_EQ(viewport(), (std::array<GLint, 4>{0, 0, 8, 8}));

		outer.end(); // still begun, so this one pops
		EXPECT_EQ(boundFramebuffer(), 0);
		EXPECT_EQ(viewport(), (std::array<GLint, 4>{0, 0, 32, 32}));

		// Back at the backbuffer: setViewport refreshes its size again.
		Primitives::setViewport(16, 16);
		int width = 0;
		int height = 0;
		Primitives::readBackbuffer({}, width, height);
		EXPECT_EQ(width, 16);
		Primitives::setViewport(32, 32);
	}

} // namespace Renderer
