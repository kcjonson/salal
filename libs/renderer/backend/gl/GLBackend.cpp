#include "backend/gl/GLBackend.h"

#include "primitives/InstanceData.h"
#include "utils/Log.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <optional>

namespace Renderer::backend::gl {

	namespace {
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

		// Set the unpack alignment an upload in `format` needs and return the host's, to
		// put back after: three-byte pixels break GL's default four-byte row alignment at
		// widths that are not a multiple of four. Pixel store is client state, so reading
		// it costs no GPU round trip; it is the one GL state the backend reads.
		GLint setUnpackAlignment(TextureFormat format) {
			GLint host = 4;
			glGetIntegerv(GL_UNPACK_ALIGNMENT, &host);
			glPixelStorei(GL_UNPACK_ALIGNMENT, format == TextureFormat::Rgb8 ? 1 : 4);
			return host;
		}

		// Where std140 puts each member of the DrawUniforms block, which is where the
		// C++ struct has it; createPipeline() checks the linked program against it.
		struct BlockMember {
			const char* name;
			size_t		offset;
		};
		constexpr BlockMember kBlockMembers[] = {
			{"u_projection", offsetof(DrawUniforms, projection)},
			{"u_transform", offsetof(DrawUniforms, transform)},
			{"u_cameraPosition", offsetof(DrawUniforms, cameraPosition)},
			{"u_viewportSize", offsetof(DrawUniforms, viewportSize)},
			{"u_cameraZoom", offsetof(DrawUniforms, cameraZoom)},
			{"u_pixelsPerMeter", offsetof(DrawUniforms, pixelsPerMeter)},
			{"u_bakedAlpha", offsetof(DrawUniforms, bakedAlpha)},
			{"u_viewportHeight", offsetof(DrawUniforms, viewportHeight)},
			{"u_pixelRatio", offsetof(DrawUniforms, pixelRatio)},
			{"u_instanced", offsetof(DrawUniforms, instanced)},
		};

		// Whether the linked program's block holds every member where kBlockMembers says, logging the first that does not.
		bool blockMatches(GLuint program) {
			for (const BlockMember& member : kBlockMembers) {
				GLuint index = GL_INVALID_INDEX;
				glGetUniformIndices(program, 1, &member.name, &index);
				GLint offset = -1;
				if (index != GL_INVALID_INDEX) {
					glGetActiveUniformsiv(program, 1, &index, GL_UNIFORM_OFFSET, &offset);
				}
				if (offset != static_cast<GLint>(member.offset)) {
					LOG_ERROR(Renderer, "createPipeline: %s is at %d in the shaders, %zu in C++", member.name, offset, member.offset);
					return false;
				}
			}
			return true;
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
		glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &uniformAlignment);
		glGetQueryiv(GL_TIMESTAMP, GL_QUERY_COUNTER_BITS, &timestampBits);
		drawRing = makeRing(kUniformBinding, sizeof(DrawUniforms));
		applyFixedState();
	}

	GLBackend::~GLBackend() = default;

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
		if (drawRing.lastOffset >= 0) {
			bind(drawRing);
		}
		for (const UniformRing& ring : hookRings) {
			bind(ring); // written, zeros at least, when it was made
		}
	}

	// --- Textures ---

	TextureHandle GLBackend::createTexture(const TextureDesc& desc) {
		if (desc.width <= 0 || desc.height <= 0) {
			LOG_ERROR(Renderer, "createTexture: empty size %dx%d", desc.width, desc.height);
			return {};
		}
		const GLFormat gl = glFormat(desc.format);
		glActiveTexture(GL_TEXTURE0); // the renderer's unit; GLTexture binds the new texture on the active one
		const GLint hostAlignment = setUnpackAlignment(desc.format);
		GLTexture	texture(desc.width, desc.height, gl.internal, gl.format, gl.type, desc.pixels);
		glPixelStorei(GL_UNPACK_ALIGNMENT, hostAlignment);
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
		glActiveTexture(GL_TEXTURE0);
		gl.bind();
		const GLint hostAlignment = setUnpackAlignment(slot->format);
		glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, width, height, format.format, format.type, pixels);
		glPixelStorei(GL_UNPACK_ALIGNMENT, hostAlignment);
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
		const auto* gl = dynamic_cast<const GLBackend*>(backend::active());
		return gl != nullptr ? gl->textureName(texture) : 0;
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
			case VertexLayout::Batched:
				// Locations 6 and 7 are the instanced layout's
				floatAttribute(0, 2, sizeof(BatchVertex), offsetof(BatchVertex, position));
				floatAttribute(1, 2, sizeof(BatchVertex), offsetof(BatchVertex, texCoord));
				floatAttribute(2, 4, sizeof(BatchVertex), offsetof(BatchVertex, color));
				floatAttribute(3, 4, sizeof(BatchVertex), offsetof(BatchVertex, data1));
				floatAttribute(4, 4, sizeof(BatchVertex), offsetof(BatchVertex, data2));
				floatAttribute(5, 4, sizeof(BatchVertex), offsetof(BatchVertex, clipBounds));
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
		if (source.vertex == nullptr || source.fragment == nullptr || !shader.loadFromSource(source.vertex, source.fragment)) {
			LOG_ERROR(Renderer, "createPipeline: the program did not build");
			return {};
		}
		const GLuint program = shader.getProgram();
		const GLuint block = glGetUniformBlockIndex(program, kDrawUniformsBlock);
		if (block == GL_INVALID_INDEX) {
			LOG_ERROR(Renderer, "createPipeline: the program declares no %s block", kDrawUniformsBlock);
			return {};
		}
		// The block's size between its last member's end and the struct's padded size, and
		// every member where the struct has it: a shader reordered within 176 bytes would
		// otherwise link and draw garbage, wherever no GL test runs.
		GLint blockSize = 0;
		glGetActiveUniformBlockiv(program, block, GL_UNIFORM_BLOCK_DATA_SIZE, &blockSize);
		constexpr size_t kBlockEnd = offsetof(DrawUniforms, instanced) + sizeof(int32_t);
		if (blockSize < static_cast<GLint>(kBlockEnd) || static_cast<size_t>(blockSize) > sizeof(DrawUniforms)) {
			LOG_ERROR(
				Renderer, "createPipeline: %s is %d bytes in the shaders, %zu in C++", kDrawUniformsBlock, blockSize, sizeof(DrawUniforms)
			);
			return {};
		}
		if (!blockMatches(program) || !bindHookBlocks(program, source)) {
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

	GLBackend::UniformRing GLBackend::makeRing(GLuint binding, GLsizeiptr size) const {
		UniformRing ring{
			.binding = binding,
			.size = size,
			.stride = ((size + uniformAlignment - 1) / uniformAlignment) * uniformAlignment,
			.buffer = GLBuffer::create(GL_UNIFORM_BUFFER),
			.last = std::vector<std::byte>(static_cast<size_t>(size)),
		};
		glBindBuffer(GL_UNIFORM_BUFFER, ring.buffer);
		glBufferData(GL_UNIFORM_BUFFER, ring.stride * kUniformSlots, nullptr, GL_STREAM_DRAW);
		return ring;
	}

	void GLBackend::write(UniformRing& ring, const void* data) {
		// A repeat of the last values (every instanced draw of a pass shares its camera)
		// binds their slot again: a write into a buffer the GPU is drawing from costs GPU
		// time, about 2.5 us each on NVIDIA.
		if (ring.lastOffset >= 0 && std::memcmp(ring.last.data(), data, ring.last.size()) == 0) {
			bind(ring);
			return;
		}
		glBindBuffer(GL_UNIFORM_BUFFER, ring.buffer);
		if (ring.slot == kUniformSlots) {
			// Every slot may still be read by a draw in flight: fresh storage rather than a wait.
			glBufferData(GL_UNIFORM_BUFFER, ring.stride * kUniformSlots, nullptr, GL_STREAM_DRAW);
			ring.slot = 0;
		}
		ring.lastOffset = ring.stride * ring.slot;
		++ring.slot;
		glBufferSubData(GL_UNIFORM_BUFFER, ring.lastOffset, ring.size, data);
		std::memcpy(ring.last.data(), data, ring.last.size());
		bind(ring);
	}

	void GLBackend::bind(const UniformRing& ring) {
		glBindBufferRange(GL_UNIFORM_BUFFER, ring.binding, ring.buffer, ring.lastOffset, ring.size);
	}

	GLBackend::UniformRing* GLBackend::hookRing(GLuint binding) {
		const auto found =
			std::find_if(hookRings.begin(), hookRings.end(), [binding](const UniformRing& ring) { return ring.binding == binding; });
		return found != hookRings.end() ? &*found : nullptr;
	}

	bool GLBackend::hookBlockMatches(GLuint program, GLuint index, const HookUniformBlock& hook) const {
		// The size the build generated is the driver's, padded to 16 as std140 pads the block,
		// and each member sits where the build says; a member the driver reports inactive has
		// no offset to check.
		const char* hookName = hook.hook != nullptr ? hook.hook : "?";
		GLint		size = 0;
		glGetActiveUniformBlockiv(program, index, GL_UNIFORM_BLOCK_DATA_SIZE, &size);
		const GLint		   padded = (size + 15) / 16 * 16;
		const UniformRing* ring = nullptr;
		for (const UniformRing& existing : hookRings) {
			ring = existing.binding == hook.binding ? &existing : ring;
		}
		if (padded != static_cast<GLint>(hook.size) || (ring != nullptr && ring->size != static_cast<GLsizeiptr>(hook.size))) {
			LOG_ERROR(Renderer, "createPipeline: hook %s's block is %d bytes in the program, %u in C++", hookName, padded, hook.size);
			return false;
		}
		for (const HookUniformMember& member : hook.members) {
			GLuint memberIndex = GL_INVALID_INDEX;
			glGetUniformIndices(program, 1, &member.name, &memberIndex);
			if (memberIndex == GL_INVALID_INDEX) {
				continue;
			}
			GLint offset = -1;
			glGetActiveUniformsiv(program, 1, &memberIndex, GL_UNIFORM_OFFSET, &offset);
			if (offset != static_cast<GLint>(member.offset)) {
				LOG_ERROR(
					Renderer,
					"createPipeline: hook %s's %s is at %d in the program, %u in C++",
					hookName,
					member.name,
					offset,
					member.offset
				);
				return false;
			}
		}
		return true;
	}

	bool GLBackend::bindHookBlocks(GLuint program, const ShaderSource& source) {
		// Every block checked before any is bound or any ring made, so a pipeline that fails
		// leaves nothing behind. A point's ring is made, zeroed, the first time a pipeline
		// declares it. A block the driver has inactive (its only reads folded away, where
		// glslang kept them) is skipped rather than failing every draw.
		struct Active {
			const HookUniformBlock* hook;
			GLuint					index;
		};
		std::vector<Active> active;
		for (const HookUniformBlock& hook : source.hookBlocks) {
			const char* hookName = hook.hook != nullptr ? hook.hook : "?";
			if (hook.binding == kUniformBinding || hook.block == nullptr) {
				LOG_ERROR(Renderer, "createPipeline: hook %s's block has no name or claims binding point 0", hookName);
				return false;
			}
			const GLuint index = glGetUniformBlockIndex(program, hook.block);
			if (index == GL_INVALID_INDEX) {
				LOG_WARNING(
					Renderer,
					"createPipeline: hook %s's block %s is inactive in the linked program; writes to it are ignored",
					hookName,
					hook.block
				);
				if (std::find(inactiveHookBindings.begin(), inactiveHookBindings.end(), hook.binding) == inactiveHookBindings.end()) {
					inactiveHookBindings.push_back(hook.binding);
				}
				continue;
			}
			if (!hookBlockMatches(program, index, hook)) {
				return false;
			}
			active.push_back({.hook = &hook, .index = index});
		}
		for (const Active& block : active) {
			glUniformBlockBinding(program, block.index, block.hook->binding);
			if (hookRing(block.hook->binding) == nullptr) {
				hookRings.push_back(makeRing(block.hook->binding, block.hook->size));
				const std::vector<std::byte> zeros(block.hook->size);
				write(hookRings.back(), zeros.data());
			}
		}
		return true;
	}

	void GLBackend::setUniforms(const DrawUniforms& uniforms) {
		write(drawRing, &uniforms);
	}

	void GLBackend::setHookUniforms(uint32_t binding, std::span<const std::byte> bytes) {
		UniformRing* ring = hookRing(binding);
		if (ring == nullptr && std::find(inactiveHookBindings.begin(), inactiveHookBindings.end(), binding) != inactiveHookBindings.end()) {
			return; // the block is inactive; createPipeline() said so once
		}
		if (ring == nullptr || static_cast<GLsizeiptr>(bytes.size()) != ring->size) {
			LOG_ERROR(
				Renderer, "setHookUniforms: %zu bytes for binding point %u, which no pipeline declares that size for", bytes.size(), binding
			);
			return;
		}
		write(*ring, bytes.data());
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
			const bool		wide = geometry->layout == VertexLayout::Batched;
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
		// No pipeline binds no program, so the caller's draws draw nothing rather than
		// run under whatever program a game's pass left bound.
		const Shader* shader = pipelines.find(pipeline);
		glUseProgram(shader != nullptr ? shader->getProgram() : 0);
	}

	void GLBackend::endForeignDraws() {
		GLVertexArray::unbind();
		glDisable(GL_BLEND);
	}

	// --- Timestamp queries ---

	// glQueryCounter is core in 3.3, but a driver may count GL_TIMESTAMP with no bits (some of macOS's);
	// there every query is refused, so a GPU timer reports none rather than zeros.
	QueryHandle GLBackend::createTimestampQuery() {
		if (timestampBits == 0) {
			static bool warned = false;
			if (!warned) {
				warned = true;
				LOG_WARNING(Renderer, "createTimestampQuery: GL_TIMESTAMP has no counter bits on this driver; no GPU timing");
			}
			return {};
		}
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
