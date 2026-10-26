#include "backend/gl/GLBackend.h"

#include "primitives/InstanceData.h"
#include "utils/Log.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <optional>

namespace Renderer::backend::gl {

	namespace {
		// The one GLBackend alive, for name(); set and cleared by its constructor and destructor.
		GLBackend* g_live = nullptr;

		struct GLFormat {
			GLenum internal;
			GLenum format;
			GLenum type;
		};

		GLFormat glFormat(TextureFormat format) {
			switch (format) {
				case TextureFormat::Rgb8:
					return {GL_RGB8, GL_RGB, GL_UNSIGNED_BYTE};
				case TextureFormat::Rgba16F:
					return {GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT};
				case TextureFormat::Rgba32F:
					return {GL_RGBA32F, GL_RGBA, GL_FLOAT};
				case TextureFormat::Rgba8:
				default:
					return {GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE};
			}
		}

		// Three-byte pixels break GL's default four-byte row alignment at widths
		// that are not a multiple of four; the other formats are always aligned.
		void setUnpackAlignment(TextureFormat format) {
			glPixelStorei(GL_UNPACK_ALIGNMENT, format == TextureFormat::Rgb8 ? 1 : 4);
		}

		GLenum glUsage(BufferUsage usage) {
			switch (usage) {
				case BufferUsage::Dynamic:
					return GL_DYNAMIC_DRAW;
				case BufferUsage::Stream:
					return GL_STREAM_DRAW;
				case BufferUsage::Static:
				default:
					return GL_STATIC_DRAW;
			}
		}

		void applyBlend(BlendState blend) {
			switch (blend) {
				case BlendState::Alpha:
					glEnable(GL_BLEND);
					glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
					break;
			}
		}

		// A float attribute of `components` at `offset` in a `stride`-byte vertex of the bound GL_ARRAY_BUFFER.
		void floatAttribute(GLuint location, GLint components, size_t stride, size_t offset, GLuint divisor = 0) {
			glEnableVertexAttribArray(location);
			glVertexAttribPointer(
				location,
				components,
				GL_FLOAT,
				GL_FALSE,
				static_cast<GLsizei>(stride),
				reinterpret_cast<const void*>(offset) // NOLINT(performance-no-int-to-ptr)
			);
			if (divisor != 0) {
				glVertexAttribDivisor(location, divisor);
			}
		}

		// Buffers are bound for writing on GL_COPY_WRITE_BUFFER, which no vertex array
		// records: an index buffer bound to GL_ELEMENT_ARRAY_BUFFER would land in
		// whatever vertex array a game's code left bound.
		constexpr GLenum kWriteTarget = GL_COPY_WRITE_BUFFER;
	} // namespace

	std::unique_ptr<GLBackend> GLBackend::create() {
		glewExperimental = GL_TRUE;
		const GLenum error = glewInit();
		if (error != GLEW_OK) {
			LOG_ERROR(Renderer, "GLEW initialization failed: %s", reinterpret_cast<const char*>(glewGetErrorString(error)));
			return nullptr;
		}
		return std::unique_ptr<GLBackend>(new GLBackend());
	}

	GLBackend::GLBackend() {
		GLint alignment = 256;
		glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &alignment);
		const GLsizeiptr size = sizeof(DrawUniforms);
		uniformStride = ((size + alignment - 1) / alignment) * alignment;
		uniformRing = GLBuffer::create(GL_UNIFORM_BUFFER);
		glBindBuffer(GL_UNIFORM_BUFFER, uniformRing);
		glBufferData(GL_UNIFORM_BUFFER, uniformStride * kUniformSlots, nullptr, GL_STREAM_DRAW);
		applyFixedState();
		g_live = this;
	}

	GLBackend::~GLBackend() {
		if (g_live == this) {
			g_live = nullptr;
		}
	}

	// --- Frame and state ---

	void GLBackend::applyFixedState() {
		glDisable(GL_DEPTH_TEST);
		glDisable(GL_CULL_FACE);
		glDisable(GL_SCISSOR_TEST);
	}

	void GLBackend::beginFrame() {
		applyFixedState();
	}

	void GLBackend::endFrame() {}

	void GLBackend::setWindowSize(int width, int height) {
		windowWidth = width;
		windowHeight = height;
	}

	void GLBackend::clear(const Foundation::Color& color) {
		glClearColor(color.r, color.g, color.b, color.a);
		glClear(GL_COLOR_BUFFER_BIT);
	}

	void GLBackend::invalidateState() {
		applyFixedState();
	}

	// --- Textures ---

	TextureHandle GLBackend::createTexture(const TextureDesc& desc) {
		if (desc.width <= 0 || desc.height <= 0) {
			LOG_ERROR(Renderer, "createTexture: empty size %dx%d", desc.width, desc.height);
			return {};
		}
		const GLFormat gl = glFormat(desc.format);
		setUnpackAlignment(desc.format);
		GLTexture texture(desc.width, desc.height, gl.internal, gl.format, gl.type, desc.pixels);
		setUnpackAlignment(TextureFormat::Rgba8);
		if (!texture.isValid()) { // glGenTextures issued nothing: no GL context is current
			LOG_ERROR(Renderer, "createTexture: GL issued no texture for %dx%d; is a GL context current?", desc.width, desc.height);
			return {};
		}
		if (!desc.linear) { // GLTexture's constructor set linear and left the texture bound
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		}
		GLTexture::unbind();
		const TextureHandle handle = textures.insert({.texture = std::move(texture), .format = desc.format});
		if (!handle.isValid()) {
			LOG_ERROR(Renderer, "Texture table full (%zu live textures); handle not issued", textures.size());
		}
		return handle;
	}

	void GLBackend::updateTexture(TextureHandle texture, int x, int y, int width, int height, const void* pixels) {
		TextureSlot* slot = textures.find(texture);
		if (slot == nullptr || pixels == nullptr) {
			return;
		}
		const GLTexture& gl = slot->texture;
		if (x < 0 || y < 0 || width <= 0 || height <= 0 || x + width > gl.width() || y + height > gl.height()) {
			LOG_ERROR(
				Renderer, "updateTexture: rectangle %d,%d %dx%d is outside the %dx%d texture", x, y, width, height, gl.width(), gl.height()
			);
			return;
		}
		const GLFormat format = glFormat(slot->format);
		gl.bind();
		setUnpackAlignment(slot->format);
		glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, width, height, format.format, format.type, pixels);
		setUnpackAlignment(TextureFormat::Rgba8);
		GLTexture::unbind();
	}

	void GLBackend::destroyTexture(TextureHandle texture) {
		textures.erase(texture);
	}

	GLuint GLBackend::textureName(TextureHandle texture) const {
		const TextureSlot* slot = textures.find(texture);
		if (slot == nullptr && texture.isValid()) {
			// A stale or foreign handle samples texture 0; say so once rather than render silently wrong.
			static bool warned = false;
			if (!warned) {
				warned = true;
				LOG_WARNING(
					Renderer,
					"Texture handle %u (slot %u, generation %u) is not in the table; using texture 0",
					texture.value,
					texture.index(),
					texture.generation()
				);
			}
		}
		return slot != nullptr ? slot->texture.handle() : 0;
	}

	GLuint name(TextureHandle texture) {
		return g_live != nullptr ? g_live->textureName(texture) : 0;
	}

	// --- Render targets ---

	GLuint GLBackend::framebuffer(RenderTargetHandle target) const {
		const TargetSlot* slot = targets.find(target);
		return slot != nullptr ? slot->framebuffer.handle() : 0;
	}

	RenderTargetHandle GLBackend::createRenderTarget(TextureHandle colour) {
		const TextureSlot* texture = textures.find(colour);
		if (texture == nullptr) {
			LOG_ERROR(Renderer, "createRenderTarget: texture %u is not in the table", colour.value);
			return {};
		}
		const int	  width = texture->texture.width();
		const int	  height = texture->texture.height();
		GLFramebuffer fbo = GLFramebuffer::create();
		fbo.bind();
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture->texture.handle(), 0);
		const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
		glBindFramebuffer(GL_FRAMEBUFFER, framebuffer(boundTarget));
		if (status != GL_FRAMEBUFFER_COMPLETE) {
			LOG_ERROR(Renderer, "RenderTarget %dx%d: framebuffer incomplete (0x%x)", width, height, status);
			return {};
		}
		return targets.insert({.framebuffer = std::move(fbo), .width = width, .height = height});
	}

	void GLBackend::setRenderTarget(RenderTargetHandle target) {
		const TargetSlot* slot = targets.find(target);
		boundTarget = slot != nullptr ? target : RenderTargetHandle{};
		if (slot != nullptr) {
			glBindFramebuffer(GL_FRAMEBUFFER, slot->framebuffer);
			glViewport(0, 0, slot->width, slot->height);
		} else {
			glBindFramebuffer(GL_FRAMEBUFFER, 0);
			glViewport(0, 0, windowWidth, windowHeight);
		}
	}

	void GLBackend::readPixels(RenderTargetHandle target, std::span<uint8_t> rgba) {
		const TargetSlot* slot = targets.find(target);
		if (target.isValid() && slot == nullptr) {
			return;
		}
		const int width = slot != nullptr ? slot->width : windowWidth;
		const int height = slot != nullptr ? slot->height : windowHeight;
		if (width <= 0 || height <= 0 || rgba.size() < static_cast<size_t>(width) * static_cast<size_t>(height) * 4) {
			return;
		}
		glBindFramebuffer(GL_READ_FRAMEBUFFER, slot != nullptr ? slot->framebuffer.handle() : 0);
		glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
		glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer(boundTarget));
	}

	void GLBackend::destroyRenderTarget(RenderTargetHandle target) {
		if (targets.erase(target) && target == boundTarget) {
			boundTarget = {}; // GL binds framebuffer 0 when the bound one is deleted
		}
	}

	// --- Buffers and geometry ---

	BufferHandle GLBackend::createBuffer(BufferUsage usage, size_t bytes, const void* data) {
		GLBuffer buffer = GLBuffer::create(kWriteTarget);
		buffer.bind();
		glBufferData(kWriteTarget, static_cast<GLsizeiptr>(bytes), data, glUsage(usage));
		return buffers.insert({.buffer = std::move(buffer), .usage = usage, .capacity = bytes});
	}

	void GLBackend::updateBuffer(BufferHandle buffer, size_t offset, size_t bytes, const void* data) {
		BufferSlot* slot = buffers.find(buffer);
		if (slot == nullptr || bytes == 0 || data == nullptr) {
			return;
		}
		slot->buffer.bind();
		if (slot->usage == BufferUsage::Stream && offset == 0) {
			// New storage rather than a write over the old: a draw still reading it keeps
			// it, and nothing waits. Growth doubles, so the size settles.
			if (bytes > slot->capacity) {
				slot->capacity = std::max(bytes, slot->capacity * 2);
			}
			glBufferData(kWriteTarget, static_cast<GLsizeiptr>(slot->capacity), nullptr, GL_STREAM_DRAW);
		} else if (offset + bytes > slot->capacity) {
			LOG_ERROR(Renderer, "updateBuffer: %zu bytes at %zu overrun the %zu-byte buffer", bytes, offset, slot->capacity);
			return;
		}
		glBufferSubData(kWriteTarget, static_cast<GLintptr>(offset), static_cast<GLsizeiptr>(bytes), data);
	}

	void GLBackend::destroyBuffer(BufferHandle buffer) {
		buffers.erase(buffer);
	}

	GeometryHandle GLBackend::createGeometry(const GeometryDesc& desc) {
		const BufferSlot* vertices = buffers.find(desc.vertices);
		const BufferSlot* indices = buffers.find(desc.indices);
		const BufferSlot* instances = buffers.find(desc.instances);
		if (vertices == nullptr || indices == nullptr || (desc.layout == VertexLayout::InstancedMesh && instances == nullptr)) {
			LOG_ERROR(Renderer, "createGeometry: a buffer is missing");
			return {};
		}

		GLVertexArray vertexArray = GLVertexArray::create();
		vertexArray.bind();
		glBindBuffer(GL_ARRAY_BUFFER, vertices->buffer);
		switch (desc.layout) {
			case VertexLayout::Uber:
				// Locations 6 and 7 are the instanced layout's
				floatAttribute(0, 2, sizeof(UberVertex), offsetof(UberVertex, position));
				floatAttribute(1, 2, sizeof(UberVertex), offsetof(UberVertex, texCoord));
				floatAttribute(2, 4, sizeof(UberVertex), offsetof(UberVertex, color));
				floatAttribute(3, 4, sizeof(UberVertex), offsetof(UberVertex, data1));
				floatAttribute(4, 4, sizeof(UberVertex), offsetof(UberVertex, data2));
				floatAttribute(5, 4, sizeof(UberVertex), offsetof(UberVertex, clipBounds));
				break;
			case VertexLayout::InstancedMesh:
				// Locations 1, 3, 4 and 5 stay disabled and read GL's default (0, 0, 0, 1);
				// the instanced path uses only position and colour.
				floatAttribute(0, 2, sizeof(InstancedMeshVertex), offsetof(InstancedMeshVertex, position));
				floatAttribute(2, 4, sizeof(InstancedMeshVertex), offsetof(InstancedMeshVertex, color));
				// Per instance (divisor 1): (worldPosition.xy, rotation, scale), then the tint
				glBindBuffer(GL_ARRAY_BUFFER, instances->buffer);
				floatAttribute(6, 4, sizeof(InstanceData), 0, 1);
				floatAttribute(7, 4, sizeof(InstanceData), offsetof(InstanceData, colorTint), 1);
				break;
		}
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indices->buffer);
		GLVertexArray::unbind();
		return geometries.insert({.vertexArray = std::move(vertexArray), .layout = desc.layout});
	}

	void GLBackend::destroyGeometry(GeometryHandle geometry) {
		geometries.erase(geometry);
	}

	// --- Pipelines and drawing ---

	PipelineHandle GLBackend::createPipeline(const ShaderSource& source) {
		Shader shader;
		if (!shader.LoadFromFile(source.vertex, source.fragment)) {
			LOG_ERROR(Renderer, "createPipeline: could not build %s and %s", source.vertex, source.fragment);
			return {};
		}
		const GLuint program = shader.getProgram();
		const GLuint block = glGetUniformBlockIndex(program, kDrawUniformsBlock);
		if (block == GL_INVALID_INDEX) {
			LOG_ERROR(Renderer, "createPipeline: %s and %s declare no %s block", source.vertex, source.fragment, kDrawUniformsBlock);
			return {};
		}
		GLint blockSize = 0;
		glGetActiveUniformBlockiv(program, block, GL_UNIFORM_BLOCK_DATA_SIZE, &blockSize);
		if (blockSize <= 0 || static_cast<size_t>(blockSize) > sizeof(DrawUniforms)) {
			LOG_ERROR(
				Renderer, "createPipeline: %s is %d bytes in the shaders, %zu in C++", kDrawUniformsBlock, blockSize, sizeof(DrawUniforms)
			);
			return {};
		}
		glUniformBlockBinding(program, block, kUniformBinding);
		shader.use();
		glUniform1i(glGetUniformLocation(program, "u_atlas"), 0);
		shader.unbind();
		return pipelines.insert(std::move(shader));
	}

	void GLBackend::destroyPipeline(PipelineHandle pipeline) {
		pipelines.erase(pipeline);
	}

	GLuint GLBackend::programName(PipelineHandle pipeline) const {
		const Shader* shader = pipelines.find(pipeline);
		return shader != nullptr ? shader->getProgram() : 0;
	}

	void GLBackend::setUniforms(const DrawUniforms& uniforms) {
		// A repeat of the last values (every instanced draw of a pass shares its camera)
		// binds their slot again: a write into a buffer the GPU is drawing from costs GPU
		// time, about 2.5 us each on NVIDIA.
		if (lastUniformOffset >= 0 && std::memcmp(&lastUniforms, &uniforms, sizeof(DrawUniforms)) == 0) {
			glBindBufferRange(GL_UNIFORM_BUFFER, kUniformBinding, uniformRing, lastUniformOffset, sizeof(DrawUniforms));
			return;
		}
		glBindBuffer(GL_UNIFORM_BUFFER, uniformRing);
		if (uniformSlot == kUniformSlots) {
			// Every slot may still be read by a draw in flight: fresh storage rather than a wait.
			glBufferData(GL_UNIFORM_BUFFER, uniformStride * kUniformSlots, nullptr, GL_STREAM_DRAW);
			uniformSlot = 0;
		}
		const GLintptr offset = uniformStride * uniformSlot;
		++uniformSlot;
		glBufferSubData(GL_UNIFORM_BUFFER, offset, sizeof(DrawUniforms), &uniforms);
		glBindBufferRange(GL_UNIFORM_BUFFER, kUniformBinding, uniformRing, offset, sizeof(DrawUniforms));
		lastUniforms = uniforms;
		lastUniformOffset = offset;
	}

	void GLBackend::draw(std::span<const DrawCall> calls) {
		// Nothing is assumed bound on entry: a game's GL may have run since the last
		// submission. Within this one, a binding changes only where the calls differ.
		GLuint					  program = 0;
		GLuint					  vertexArray = 0;
		std::optional<BlendState> blend;
		TextureHandle			  texture;
		for (const DrawCall& call : calls) {
			const Shader*		pipeline = pipelines.find(call.pipeline);
			const GeometrySlot* geometry = geometries.find(call.geometry);
			if (pipeline == nullptr || geometry == nullptr || call.indexCount == 0) {
				continue;
			}
			if (pipeline->getProgram() != program) {
				program = pipeline->getProgram();
				glUseProgram(program);
			}
			if (geometry->vertexArray.handle() != vertexArray) {
				vertexArray = geometry->vertexArray.handle();
				glBindVertexArray(vertexArray);
			}
			if (blend != call.blend) {
				applyBlend(call.blend);
				blend = call.blend;
			}
			if (call.texture.isValid() && call.texture != texture) {
				if (!texture.isValid()) {
					glActiveTexture(GL_TEXTURE0);
				}
				glBindTexture(GL_TEXTURE_2D, textureName(call.texture));
				texture = call.texture;
			}
			const bool		wide = geometry->layout == VertexLayout::Uber;
			const GLenum	type = wide ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT;
			const uintptr_t offset = static_cast<uintptr_t>(call.indexOffset) * (wide ? sizeof(uint32_t) : sizeof(uint16_t));
			const auto*		indices = reinterpret_cast<const void*>(offset); // NOLINT(performance-no-int-to-ptr)
			if (call.instanceCount > 0) {
				glDrawElementsInstanced(
					GL_TRIANGLES, static_cast<GLsizei>(call.indexCount), type, indices, static_cast<GLsizei>(call.instanceCount)
				);
			} else {
				glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(call.indexCount), type, indices);
			}
		}

		// What the batch has always left behind: no vertex array (a game's index-buffer
		// bind would otherwise land in ours), no texture on unit 0, blending off.
		if (vertexArray != 0) {
			GLVertexArray::unbind();
		}
		if (texture.isValid()) {
			GLTexture::unbind();
		}
		if (blend.has_value()) {
			glDisable(GL_BLEND);
		}
	}

	void GLBackend::beginForeignDraws(PipelineHandle pipeline, BlendState blend) {
		applyBlend(blend);
		const Shader* shader = pipelines.find(pipeline);
		if (shader != nullptr) {
			shader->use();
		}
	}

	void GLBackend::endForeignDraws() {
		GLVertexArray::unbind();
		glDisable(GL_BLEND);
	}

	// --- Timestamp queries ---

	QueryHandle GLBackend::createTimestampQuery() {
		return queries.insert(GLQuery::create());
	}

	void GLBackend::writeTimestamp(QueryHandle query) {
		if (const GLQuery* gl = queries.find(query); gl != nullptr) {
			gl->recordTimestamp();
		}
	}

	bool GLBackend::timestampReady(QueryHandle query) {
		const GLQuery* gl = queries.find(query);
		return gl != nullptr && gl->isResultAvailable();
	}

	uint64_t GLBackend::timestampNs(QueryHandle query) {
		const GLQuery* gl = queries.find(query);
		return gl != nullptr ? gl->getResult() : 0;
	}

	void GLBackend::destroyTimestampQuery(QueryHandle query) {
		queries.erase(query);
	}

} // namespace Renderer::backend::gl
