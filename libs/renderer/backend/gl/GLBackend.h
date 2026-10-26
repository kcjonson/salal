#pragma once

// The OpenGL 3.3 core backend: every GL object the renderer draws with lives in
// its tables, behind the handles of backend/Backend.h. One per GL context, made on
// the calling thread's current context by create(), which also initialises GLEW;
// destroy it while that context is still current.
//
// This header is the opt-in GL path (as are the gl/ wrappers and Shader), outside
// the layering check that keeps GL off the renderer's public headers.

#include "backend/Backend.h"
#include "backend/SlotTable.h"
#include "backend/gl/GLBuffer.h"
#include "backend/gl/GLFramebuffer.h"
#include "backend/gl/GLQuery.h"
#include "backend/gl/GLTexture.h"
#include "backend/gl/GLVertexArray.h"
#include "backend/gl/Shader.h"
#include <GL/glew.h>
#include <memory>

namespace Renderer::backend::gl {

	class GLBackend final : public Backend {
	  public:
		// Initialise GLEW on the current context and set the fixed state; null, with
		// an error logged, when GLEW cannot initialise.
		static std::unique_ptr<GLBackend> create();

		~GLBackend() override;

		GLBackend(const GLBackend&) = delete;
		GLBackend& operator=(const GLBackend&) = delete;
		GLBackend(GLBackend&&) = delete;
		GLBackend& operator=(GLBackend&&) = delete;

		void beginFrame() override;
		void endFrame() override;
		void setWindowSize(int width, int height) override;
		void clear(const Foundation::Color& color) override;
		void invalidateState() override;

		TextureHandle createTexture(const TextureDesc& desc) override;
		void		  updateTexture(TextureHandle texture, int x, int y, int width, int height, const void* pixels) override;
		void		  destroyTexture(TextureHandle texture) override;

		RenderTargetHandle createRenderTarget(TextureHandle colour) override;
		void			   setRenderTarget(RenderTargetHandle target) override;
		void			   readPixels(RenderTargetHandle target, std::span<uint8_t> rgba) override;
		void			   destroyRenderTarget(RenderTargetHandle target) override;

		BufferHandle createBuffer(BufferUsage usage, size_t bytes, const void* data) override;
		void		 updateBuffer(BufferHandle buffer, size_t offset, size_t bytes, const void* data) override;
		void		 destroyBuffer(BufferHandle buffer) override;

		GeometryHandle createGeometry(const GeometryDesc& desc) override;
		void		   destroyGeometry(GeometryHandle geometry) override;

		PipelineHandle createPipeline(const ShaderSource& source) override;
		void		   destroyPipeline(PipelineHandle pipeline) override;

		void setUniforms(const DrawUniforms& uniforms) override;
		void draw(std::span<const DrawCall> calls) override;
		void beginForeignDraws(PipelineHandle pipeline, BlendState blend) override;
		void endForeignDraws() override;

		QueryHandle createTimestampQuery() override;
		void		writeTimestamp(QueryHandle query) override;
		bool		timestampReady(QueryHandle query) override;
		uint64_t	timestampNs(QueryHandle query) override;
		void		destroyTimestampQuery(QueryHandle query) override;

		// GL names, 0 for an invalid or stale handle: for a game pass binding a
		// renderer texture to its own sampler, and for the GL tests.
		[[nodiscard]] GLuint textureName(TextureHandle texture) const;
		[[nodiscard]] GLuint programName(PipelineHandle pipeline) const;

		// The binding point the DrawUniforms block reads, and how many setUniforms()
		// calls the ring holds before it is orphaned.
		static constexpr GLuint kUniformBinding = 0;
		static constexpr int	kUniformSlots = 512;

	  private:
		GLBackend();

		struct TextureSlot {
			GLTexture	  texture;
			TextureFormat format = TextureFormat::Rgba8;
		};

		struct TargetSlot {
			GLFramebuffer framebuffer;
			int			  width = 0;
			int			  height = 0;
		};

		struct BufferSlot {
			GLBuffer	buffer;
			BufferUsage usage = BufferUsage::Static;
			size_t		capacity = 0;
		};

		struct GeometrySlot {
			GLVertexArray vertexArray;
			VertexLayout  layout = VertexLayout::Uber;
		};

		void   applyFixedState();
		GLuint framebuffer(RenderTargetHandle target) const;

		SlotTable<TextureHandle, TextureSlot>	  textures;
		SlotTable<RenderTargetHandle, TargetSlot> targets;
		SlotTable<BufferHandle, BufferSlot>		  buffers;
		SlotTable<GeometryHandle, GeometrySlot>	  geometries;
		SlotTable<PipelineHandle, Shader>		  pipelines;
		SlotTable<QueryHandle, GLQuery>			  queries;

		// setUniforms() writes slot after slot of one buffer, and orphans it (new
		// storage, the old kept until the draws reading it finish) when it runs out.
		// The last values written, and their slot, which a repeat of them binds again.
		GLBuffer	 uniformRing;
		GLsizeiptr	 uniformStride = 0;
		int			 uniformSlot = 0;
		DrawUniforms lastUniforms;
		GLintptr	 lastUniformOffset = -1;

		RenderTargetHandle boundTarget;
		int				   windowWidth = 0;
		int				   windowHeight = 0;
	};

	// The GL name of `texture` in the live GLBackend's table; 0 for an invalid or
	// stale handle, or with no GLBackend. A game pass binds a renderer texture with it.
	GLuint name(TextureHandle texture);

} // namespace Renderer::backend::gl
