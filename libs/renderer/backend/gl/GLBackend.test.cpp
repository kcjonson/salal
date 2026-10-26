// The GL backend on a hidden GL 3.3 core context, skipped where none can be made;
// the fixture hands it to Primitives, as an app does. The headless case makes no GL
// call (a GL call with no context is undefined on macOS): with no backend in
// Primitives, name() answers without one.

#include "backend/gl/GLBackend.h"
#include "primitives/Primitives.h"

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace Renderer::backend::gl {

	TEST(GLBackendNameTest, WithNoBackendEveryHandleNamesNothing) {
		EXPECT_EQ(name(TextureHandle{}), 0U);
		EXPECT_EQ(name(TextureHandle::make(999, 0)), 0U);
	}

	class GLBackendTest : public ::testing::Test {
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
			std::unique_ptr<GLBackend> created = GLBackend::create();
			if (created == nullptr) {
				glfwDestroyWindow(window);
				window = nullptr;
				glfwTerminate();
				GTEST_SKIP() << "GLEW initialization failed";
			}
			gpu = created.get();
			Primitives::init(std::move(created));
		}

		void TearDown() override {
			if (window != nullptr) {
				Primitives::shutdown();
				glfwDestroyWindow(window);
				glfwTerminate();
			}
		}

		TextureHandle createTexture(const TextureDesc& desc) { return gpu->createTexture(desc); }
		void		  updateTexture(TextureHandle handle, int x, int y, int width, int height, const void* pixels) {
			 gpu->updateTexture(handle, x, y, width, height, pixels);
		}
		void destroyTexture(TextureHandle handle) { gpu->destroyTexture(handle); }

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
		GLBackend*	gpu = nullptr; // owned by Primitives
	};

	TEST_F(GLBackendTest, CreatedTextureNamesAGLTexture) {
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

	TEST_F(GLBackendTest, DestroyedHandleIsStale) {
		const TextureHandle handle = createTexture({.width = 2, .height = 2});
		const GLuint		glName = name(handle);
		destroyTexture(handle);
		EXPECT_EQ(name(handle), 0U);
		EXPECT_FALSE(glIsTexture(glName));
		destroyTexture(handle); // twice is harmless
	}

	TEST_F(GLBackendTest, ReusedSlotGetsNewGeneration) {
		const TextureHandle stale = createTexture({.width = 2, .height = 2});
		destroyTexture(stale);
		const TextureHandle fresh = createTexture({.width = 2, .height = 2});
		EXPECT_EQ(fresh.index(), stale.index());
		EXPECT_NE(fresh.generation(), stale.generation());
		EXPECT_EQ(name(stale), 0U);
		EXPECT_NE(name(fresh), 0U);
		destroyTexture(fresh);
	}

	TEST_F(GLBackendTest, DistinctHandlesPerTexture) {
		const TextureHandle a = createTexture({.width = 2, .height = 2});
		const TextureHandle b = createTexture({.width = 2, .height = 2});
		EXPECT_NE(a, b);
		EXPECT_NE(name(a), name(b));
		destroyTexture(a);
		EXPECT_EQ(name(a), 0U);
		EXPECT_NE(name(b), 0U);
		destroyTexture(b);
	}

	TEST_F(GLBackendTest, PixelsUploadAndUpdate) {
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

	TEST_F(GLBackendTest, Rgb8RowsNeedNoPadding) {
		// 3 x 2 RGB: nine-byte rows, which the default four-byte unpack alignment would misread.
		const std::array<uint8_t, 18> pixels{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18};
		const TextureHandle handle = createTexture({.width = 3, .height = 2, .format = TextureFormat::Rgb8, .pixels = pixels.data()});
		ASSERT_TRUE(handle.isValid());
		EXPECT_EQ(internalFormat(handle), GL_RGB8);
		EXPECT_EQ(readBack(handle, 3, 2, 3), std::vector<uint8_t>(pixels.begin(), pixels.end()));
		destroyTexture(handle);
	}

	TEST_F(GLBackendTest, UploadsUseUnitZeroAndPutBackTheHostsAlignment) {
		// What a game's pass might leave: another unit active with its texture, and its own alignment.
		GLuint foreign = 0;
		glGenTextures(1, &foreign);
		glActiveTexture(GL_TEXTURE3);
		glBindTexture(GL_TEXTURE_2D, foreign);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 8);

		const std::array<uint8_t, 18> pixels{};
		const TextureHandle handle = createTexture({.width = 3, .height = 2, .format = TextureFormat::Rgb8, .pixels = pixels.data()});
		ASSERT_TRUE(handle.isValid());
		updateTexture(handle, 0, 0, 3, 2, pixels.data());

		GLint alignment = 0;
		glGetIntegerv(GL_UNPACK_ALIGNMENT, &alignment);
		EXPECT_EQ(alignment, 8);
		GLint active = 0;
		glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
		EXPECT_EQ(active, GL_TEXTURE0);
		glActiveTexture(GL_TEXTURE3);
		GLint bound = 0;
		glGetIntegerv(GL_TEXTURE_BINDING_2D, &bound);
		EXPECT_EQ(static_cast<GLuint>(bound), foreign); // unit 3 untouched

		glBindTexture(GL_TEXTURE_2D, 0);
		glActiveTexture(GL_TEXTURE0);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
		glDeleteTextures(1, &foreign);
		destroyTexture(handle);
	}

	TEST_F(GLBackendTest, NameResolvesThroughTheBackendPrimitivesHolds) {
		const TextureHandle handle = Primitives::createTexture({.width = 2, .height = 2});
		ASSERT_NE(name(handle), 0U);
		{
			// A second backend made and dropped (a test's, say) leaves name() answering from Primitives' one.
			const std::unique_ptr<GLBackend> spare = GLBackend::create();
			ASSERT_NE(spare, nullptr);
		}
		EXPECT_EQ(name(handle), gpu->textureName(handle));
		EXPECT_NE(name(handle), 0U);
		Primitives::destroyTexture(handle);
	}

	TEST_F(GLBackendTest, FormatsAndFiltering) {
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

	TEST_F(GLBackendTest, UpdateAndDestroyOfInvalidHandleAreHarmless) {
		const std::array<uint8_t, 4> pixel{};
		updateTexture(TextureHandle{}, 0, 0, 1, 1, pixel.data());
		destroyTexture(TextureHandle{});
		destroyTexture(TextureHandle::make(999, 0));
		EXPECT_EQ(name(TextureHandle::make(999, 0)), 0U);
	}

	TEST_F(GLBackendTest, EmptySizeIsRefused) {
		EXPECT_FALSE(createTexture({.width = 0, .height = 4}).isValid());
		EXPECT_FALSE(createTexture({.width = 4, .height = -1}).isValid());
	}

	TEST_F(GLBackendTest, UpdateOutsideTheTextureIsRefused) {
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

	TEST_F(GLBackendTest, TheUniformBlockMatchesDrawUniforms) {
		const PipelineHandle pipeline = gpu->createPipeline({.vertex = "uber.vert", .fragment = "uber.frag"});
		ASSERT_TRUE(pipeline.isValid()) << "the uber shaders are mirrored beside the test executable";
		const GLuint program = gpu->programName(pipeline);
		const GLuint block = glGetUniformBlockIndex(program, kDrawUniformsBlock);
		ASSERT_NE(block, GL_INVALID_INDEX);

		GLint size = 0;
		glGetActiveUniformBlockiv(program, block, GL_UNIFORM_BLOCK_DATA_SIZE, &size);
		EXPECT_GE(size, static_cast<GLint>(offsetof(DrawUniforms, cursorStrength) + sizeof(float)));
		EXPECT_LE(size, static_cast<GLint>(sizeof(DrawUniforms)));
		GLint binding = -1;
		glGetActiveUniformBlockiv(program, block, GL_UNIFORM_BLOCK_BINDING, &binding);
		EXPECT_EQ(binding, static_cast<GLint>(GLBackend::kUniformBinding));
		GLint members = 0;
		glGetActiveUniformBlockiv(program, block, GL_UNIFORM_BLOCK_ACTIVE_UNIFORMS, &members);
		EXPECT_EQ(members, 16);

		// Every member where std140 puts it, which is where the C++ struct has it.
		const std::pair<const char*, size_t> offsets[] = {
			{"u_projection", offsetof(DrawUniforms, projection)},
			{"u_transform", offsetof(DrawUniforms, transform)},
			{"u_cameraPosition", offsetof(DrawUniforms, cameraPosition)},
			{"u_viewportSize", offsetof(DrawUniforms, viewportSize)},
			{"u_cursorWorld", offsetof(DrawUniforms, cursorWorld)},
			{"u_cameraZoom", offsetof(DrawUniforms, cameraZoom)},
			{"u_pixelsPerMeter", offsetof(DrawUniforms, pixelsPerMeter)},
			{"u_bakedAlpha", offsetof(DrawUniforms, bakedAlpha)},
			{"u_viewportHeight", offsetof(DrawUniforms, viewportHeight)},
			{"u_pixelRatio", offsetof(DrawUniforms, pixelRatio)},
			{"u_instanced", offsetof(DrawUniforms, instanced)},
			{"u_groundcoverMode", offsetof(DrawUniforms, groundcoverMode)},
			{"u_groundcoverOpenness", offsetof(DrawUniforms, groundcoverOpenness)},
			{"u_groundcoverReach", offsetof(DrawUniforms, groundcoverReach)},
			{"u_cursorRadius", offsetof(DrawUniforms, cursorRadius)},
			{"u_cursorStrength", offsetof(DrawUniforms, cursorStrength)},
		};
		for (const auto& [member, offset] : offsets) {
			GLuint index = GL_INVALID_INDEX;
			glGetUniformIndices(program, 1, &member, &index);
			ASSERT_NE(index, GL_INVALID_INDEX) << member;
			GLint inBlock = -2;
			glGetActiveUniformsiv(program, 1, &index, GL_UNIFORM_BLOCK_INDEX, &inBlock);
			EXPECT_EQ(inBlock, static_cast<GLint>(block)) << member;
			GLint actual = -1;
			glGetActiveUniformsiv(program, 1, &index, GL_UNIFORM_OFFSET, &actual);
			EXPECT_EQ(actual, static_cast<GLint>(offset)) << member;
		}
		// The atlas stays a plain sampler, on unit 0.
		GLint atlasUnit = -1;
		glGetUniformiv(program, glGetUniformLocation(program, "u_atlas"), &atlasUnit);
		EXPECT_EQ(atlasUnit, 0);
		gpu->destroyPipeline(pipeline);
	}

	TEST_F(GLBackendTest, FixedStateIsSetAgainAtBeginFrameAndInvalidateState) {
		const auto enableAll = [] {
			glEnable(GL_DEPTH_TEST);
			glEnable(GL_CULL_FACE);
			glEnable(GL_SCISSOR_TEST);
		};
		const auto expectAllOff = [] {
			EXPECT_FALSE(glIsEnabled(GL_DEPTH_TEST));
			EXPECT_FALSE(glIsEnabled(GL_CULL_FACE));
			EXPECT_FALSE(glIsEnabled(GL_SCISSOR_TEST));
		};
		expectAllOff(); // from create()
		enableAll();
		gpu->invalidateState();
		expectAllOff();
		enableAll();
		gpu->beginFrame();
		expectAllOff();
	}

	TEST_F(GLBackendTest, InvalidateStateRebindsTheUniforms) {
		gpu->setUniforms({});
		GLint ring = 0;
		glGetIntegeri_v(GL_UNIFORM_BUFFER_BINDING, GLBackend::kUniformBinding, &ring);
		ASSERT_NE(ring, 0);

		// A game's uniform buffer on point 0, then the call that says so.
		GLuint foreign = 0;
		glGenBuffers(1, &foreign);
		glBindBuffer(GL_UNIFORM_BUFFER, foreign);
		glBufferData(GL_UNIFORM_BUFFER, 256, nullptr, GL_STATIC_DRAW);
		glBindBufferBase(GL_UNIFORM_BUFFER, GLBackend::kUniformBinding, foreign);
		gpu->invalidateState();

		GLint bound = 0;
		glGetIntegeri_v(GL_UNIFORM_BUFFER_BINDING, GLBackend::kUniformBinding, &bound);
		EXPECT_EQ(bound, ring);
		glDeleteBuffers(1, &foreign);
	}

	TEST_F(GLBackendTest, ForeignDrawsWithNoPipelineBindNoProgram) {
		const PipelineHandle pipeline = gpu->createPipeline({.vertex = "uber.vert", .fragment = "uber.frag"});
		ASSERT_TRUE(pipeline.isValid());
		glUseProgram(gpu->programName(pipeline)); // whatever a game's pass left bound
		gpu->beginForeignDraws({}, BlendState::Alpha);
		GLint program = -1;
		glGetIntegerv(GL_CURRENT_PROGRAM, &program);
		EXPECT_EQ(program, 0);
		gpu->endForeignDraws();
		gpu->destroyPipeline(pipeline);
	}

	TEST_F(GLBackendTest, ADrawBindsEverythingItUsesAndLeavesOnlyItsProgram) {
		const PipelineHandle pipeline = gpu->createPipeline({.vertex = "uber.vert", .fragment = "uber.frag"});
		ASSERT_TRUE(pipeline.isValid());
		// A stream buffer made empty grows to its first write.
		const BufferHandle	 vertices = gpu->createBuffer(BufferUsage::Stream, 0, nullptr);
		const BufferHandle	 indices = gpu->createBuffer(BufferUsage::Stream, 0, nullptr);
		const GeometryHandle geometry = gpu->createGeometry({.layout = VertexLayout::Uber, .vertices = vertices, .indices = indices});
		ASSERT_TRUE(geometry.isValid());

		// Two flat red triangles over a 4 x 4 target, drawn as two calls, the second from index 3.
		const Foundation::Vec4			red(1.0F, 0.0F, 0.0F, 1.0F);
		const Foundation::Vec4			flat(0.0F, 0.0F, 0.0F, 1.0F); // a zero-size shape: the triangle itself
		const Foundation::Vec4			none(0.0F);
		const std::array<UberVertex, 4> quad = {{
			{{0.0F, 0.0F}, {0.0F, 0.0F}, red, none, flat, none},
			{{4.0F, 0.0F}, {0.0F, 0.0F}, red, none, flat, none},
			{{4.0F, 4.0F}, {0.0F, 0.0F}, red, none, flat, none},
			{{0.0F, 4.0F}, {0.0F, 0.0F}, red, none, flat, none},
		}};
		const std::array<uint32_t, 6>	triangles = {0, 1, 2, 0, 2, 3};
		gpu->updateBuffer(vertices, 0, sizeof(quad), quad.data());
		gpu->updateBuffer(indices, 0, sizeof(triangles), triangles.data());

		const TextureHandle		 atlas = createTexture({.width = 1, .height = 1});
		const TextureHandle		 colour = createTexture({.width = 4, .height = 4});
		const RenderTargetHandle target = gpu->createRenderTarget(colour);
		ASSERT_TRUE(target.isValid());
		gpu->setRenderTarget(target);
		gpu->clear({0.0F, 0.0F, 0.0F, 0.0F});
		gpu->setUniforms({.projection = glm::ortho(0.0F, 4.0F, 4.0F, 0.0F, -1.0F, 1.0F), .viewportHeight = 4.0F, .pixelRatio = 1.0F});

		// What a game's pass might leave: its own vertex array (holding an index
		// buffer of its own), program, active unit and textures.
		GLuint foreignArray = 0;
		GLuint foreignIndices = 0;
		glGenVertexArrays(1, &foreignArray);
		glGenBuffers(1, &foreignIndices);
		glBindVertexArray(foreignArray);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, foreignIndices);
		const TextureHandle foreignTexture = createTexture({.width = 1, .height = 1});
		glActiveTexture(GL_TEXTURE3);
		glBindTexture(GL_TEXTURE_2D, name(foreignTexture));
		glUseProgram(0);

		const std::array<DrawCall, 2> calls = {{
			{.pipeline = pipeline, .geometry = geometry, .texture = atlas, .indexOffset = 0, .indexCount = 3},
			{.pipeline = pipeline, .geometry = geometry, .texture = foreignTexture, .indexOffset = 3, .indexCount = 3},
		}};
		gpu->draw(calls);

		GLint boundArray = -1;
		glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &boundArray);
		EXPECT_EQ(boundArray, 0);
		GLint activeUnit = 0;
		glGetIntegerv(GL_ACTIVE_TEXTURE, &activeUnit);
		EXPECT_EQ(activeUnit, GL_TEXTURE0);
		GLint boundTexture = -1;
		glGetIntegerv(GL_TEXTURE_BINDING_2D, &boundTexture);
		EXPECT_EQ(boundTexture, 0);
		EXPECT_FALSE(glIsEnabled(GL_BLEND));
		GLint program = 0;
		glGetIntegerv(GL_CURRENT_PROGRAM, &program);
		EXPECT_EQ(static_cast<GLuint>(program), gpu->programName(pipeline));
		EXPECT_EQ(glGetError(), static_cast<GLenum>(GL_NO_ERROR));

		std::vector<uint8_t> pixels(4 * 4 * 4, 9);
		gpu->readPixels(target, pixels);
		for (size_t i = 0; i < pixels.size(); i += 4) {
			EXPECT_EQ(pixels[i], 255) << "pixel " << i / 4;
			EXPECT_EQ(pixels[i + 1], 0) << "pixel " << i / 4;
			EXPECT_EQ(pixels[i + 3], 255) << "pixel " << i / 4;
		}

		gpu->setRenderTarget({});
		glDeleteBuffers(1, &foreignIndices);
		glDeleteVertexArrays(1, &foreignArray);
		gpu->destroyRenderTarget(target);
		destroyTexture(foreignTexture);
		destroyTexture(colour);
		destroyTexture(atlas);
		gpu->destroyGeometry(geometry);
		gpu->destroyBuffer(indices);
		gpu->destroyBuffer(vertices);
		gpu->destroyPipeline(pipeline);
	}

} // namespace Renderer::backend::gl
