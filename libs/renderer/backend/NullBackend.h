#pragma once

// A backend that touches no GPU: it issues handles, keeps what is written into
// buffers, and records what it is asked to do, so the renderer's tests (and a
// game's) run headless and check what would have reached the GPU. Nothing is
// drawn; readPixels() reads zeros where GL would read, and a timestamp is ready, at
// 0, once it is written.

#include "backend/Backend.h"
#include "backend/SlotTable.h"
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace Renderer::backend {

	class NullBackend final : public Backend {
	  public:
		// One setHookUniforms() call.
		struct HookUniforms {
			uint32_t			   binding = 0;
			std::vector<std::byte> bytes;
		};

		// One DrawCall as submitted, with the uniforms it would have read.
		struct RecordedDraw {
			DrawCall				  call;
			DrawUniforms			  uniforms;
			std::vector<HookUniforms> hookUniforms;	  // the last write at each binding point written, by binding point
			int						  submission = 0; // which draw() it came in, from 0

			// What the draw read at `binding`; empty when nothing was written there.
			[[nodiscard]] std::span<const std::byte> hookBlock(uint32_t binding) const;
		};

		// A pipeline as made: its stages' text and the hook blocks it declared (their
		// names still the caller's).
		struct PipelineRecord {
			std::string					  vertex;
			std::string					  fragment;
			std::vector<HookUniformBlock> hookBlocks;
		};

		struct TextureUpdate {
			TextureHandle texture;
			int			  x = 0;
			int			  y = 0;
			int			  width = 0;
			int			  height = 0;
		};

		// Live objects of each kind.
		struct Counts {
			size_t textures = 0;
			size_t targets = 0;
			size_t buffers = 0;
			size_t geometries = 0;
			size_t pipelines = 0;
			size_t queries = 0;
		};

		NullBackend() = default;

		void beginFrame() override { ++frameCount; }
		void endFrame() override {}
		void setWindowSize(int width, int height) override;
		void clear(const Foundation::Color& color) override { clearColors.push_back(color); }
		void invalidateState() override { ++invalidationCount; }

		TextureHandle createTexture(const TextureDesc& desc) override;
		void		  updateTexture(TextureHandle texture, int x, int y, int width, int height, const void* pixels) override;
		void		  destroyTexture(TextureHandle texture) override { textureTable.erase(texture); }

		RenderTargetHandle createRenderTarget(TextureHandle colour) override;
		void			   setRenderTarget(RenderTargetHandle target) override { targetBinds.push_back(target); }
		void			   readPixels(RenderTargetHandle target, std::span<uint8_t> rgba) override;
		void			   destroyRenderTarget(RenderTargetHandle target) override { targetTable.erase(target); }

		BufferHandle createBuffer(BufferUsage usage, size_t bytes, const void* data) override;
		void		 updateBuffer(BufferHandle buffer, size_t offset, size_t bytes, const void* data) override;
		void		 destroyBuffer(BufferHandle buffer) override { bufferTable.erase(buffer); }

		GeometryHandle createGeometry(const GeometryDesc& desc) override;
		void		   destroyGeometry(GeometryHandle geometry) override { geometryTable.erase(geometry); }

		PipelineHandle createPipeline(const ShaderSource& source) override;
		void		   destroyPipeline(PipelineHandle pipeline) override { pipelineTable.erase(pipeline); }

		void setUniforms(const DrawUniforms& uniforms) override;
		void setHookUniforms(uint32_t binding, std::span<const std::byte> bytes) override;
		void draw(std::span<const DrawCall> calls) override;
		void beginForeignDraws(PipelineHandle pipeline, BlendState blend) override;
		void endForeignDraws() override { ++foreignDrawEnds; }

		QueryHandle createTimestampQuery() override { return queryTable.insert(false); }
		void		writeTimestamp(QueryHandle query) override;
		bool		timestampReady(QueryHandle query) override;
		uint64_t	timestampNs(QueryHandle /*query*/) override { return 0; }
		void		destroyTimestampQuery(QueryHandle query) override { queryTable.erase(query); }

		// --- What it was asked ---

		[[nodiscard]] const std::vector<RecordedDraw>&		 draws() const { return recordedDraws; }
		[[nodiscard]] const std::vector<DrawUniforms>&		 uniformWrites() const { return uniformHistory; }
		[[nodiscard]] const std::vector<HookUniforms>&		 hookUniformWrites() const { return hookHistory; }
		[[nodiscard]] const std::vector<RenderTargetHandle>& targetsBound() const { return targetBinds; }
		[[nodiscard]] const std::vector<TextureUpdate>&		 textureUpdates() const { return updates; }
		[[nodiscard]] const std::vector<Foundation::Color>&	 clears() const { return clearColors; }
		[[nodiscard]] int									 frames() const { return frameCount; }
		[[nodiscard]] int									 invalidations() const { return invalidationCount; }
		[[nodiscard]] int									 timestampsWritten() const { return timestampCount; }
		// Textures createTexture() made, destroyed since or not.
		[[nodiscard]] int texturesMade() const { return textureCount; }
		// beginForeignDraws() calls, the pipeline of the last, and endForeignDraws() calls.
		[[nodiscard]] int			 foreignDrawsBegun() const { return foreignDrawBegins; }
		[[nodiscard]] PipelineHandle foreignPipeline() const { return lastForeignPipeline; }
		[[nodiscard]] int			 foreignDrawsEnded() const { return foreignDrawEnds; }

		// The buffer's bytes as last written; empty for an invalid or stale handle.
		[[nodiscard]] std::span<const uint8_t> bufferContents(BufferHandle buffer) const;
		// What the texture was made from (pixels not kept); null for an invalid or stale handle.
		[[nodiscard]] const TextureDesc*	texture(TextureHandle texture) const { return textureTable.find(texture); }
		[[nodiscard]] const GeometryDesc*	geometry(GeometryHandle geometry) const { return geometryTable.find(geometry); }
		[[nodiscard]] const PipelineRecord* pipeline(PipelineHandle pipeline) const { return pipelineTable.find(pipeline); }
		[[nodiscard]] Counts				live() const;
		[[nodiscard]] int					windowWidth() const { return window.first; }
		[[nodiscard]] int					windowHeight() const { return window.second; }

		// Forget what it was asked (draws, uniforms, binds, updates, counters); its objects stay.
		void clearRecord();

	  private:
		struct BufferRecord {
			BufferUsage			 usage = BufferUsage::Static;
			std::vector<uint8_t> bytes;
		};

		SlotTable<TextureHandle, TextureDesc>		 textureTable;
		SlotTable<RenderTargetHandle, TextureHandle> targetTable;
		SlotTable<BufferHandle, BufferRecord>		 bufferTable;
		SlotTable<GeometryHandle, GeometryDesc>		 geometryTable;
		SlotTable<PipelineHandle, PipelineRecord>	 pipelineTable;
		SlotTable<QueryHandle, bool>				 queryTable; // written yet

		std::vector<RecordedDraw>		recordedDraws;
		std::vector<DrawUniforms>		uniformHistory;
		std::vector<HookUniforms>		hookHistory;
		std::vector<HookUniforms>		currentHooks; // by binding point
		std::vector<RenderTargetHandle> targetBinds;
		std::vector<TextureUpdate>		updates;
		std::vector<Foundation::Color>	clearColors;
		DrawUniforms					currentUniforms;
		std::pair<int, int>				window{0, 0};
		PipelineHandle					lastForeignPipeline;
		int								frameCount = 0;
		int								invalidationCount = 0;
		int								timestampCount = 0;
		int								textureCount = 0;
		int								submissionCount = 0;
		int								foreignDrawBegins = 0;
		int								foreignDrawEnds = 0;
	};

} // namespace Renderer::backend
