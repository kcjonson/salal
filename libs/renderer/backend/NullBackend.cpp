#include "backend/NullBackend.h"

#include <algorithm>
#include <cstring>
#include <utility>

namespace Renderer::backend {

	void NullBackend::setWindowSize(int width, int height) {
		window = {width, height};
	}

	TextureHandle NullBackend::createTexture(const TextureDesc& desc) {
		if (desc.width <= 0 || desc.height <= 0) {
			return {};
		}
		TextureDesc kept = desc;
		kept.pixels = nullptr;
		++textureCount;
		return textureTable.insert(kept);
	}

	void NullBackend::updateTexture(TextureHandle texture, int x, int y, int width, int height, const void* pixels) {
		if (textureTable.find(texture) != nullptr && pixels != nullptr) {
			updates.push_back({.texture = texture, .x = x, .y = y, .width = width, .height = height});
		}
	}

	RenderTargetHandle NullBackend::createRenderTarget(TextureHandle colour) {
		return textureTable.find(colour) != nullptr ? targetTable.insert(colour) : RenderTargetHandle{};
	}

	void NullBackend::readPixels(RenderTargetHandle target, std::span<uint8_t> rgba) {
		++pixelReadCount;
		// What GL reads, zeroed: the target's texture (the window at its set size), and
		// nothing for a stale target or a span short of width * height * 4.
		const TextureHandle* colour = targetTable.find(target);
		if (target.isValid() && colour == nullptr) {
			return;
		}
		const TextureDesc* texture = colour != nullptr ? textureTable.find(*colour) : nullptr;
		const int		   width = colour != nullptr ? (texture != nullptr ? texture->width : 0) : window.first;
		const int		   height = colour != nullptr ? (texture != nullptr ? texture->height : 0) : window.second;
		const size_t	   bytes = static_cast<size_t>(std::max(width, 0)) * static_cast<size_t>(std::max(height, 0)) * 4;
		if (bytes == 0 || rgba.size() < bytes) {
			return;
		}
		std::fill_n(rgba.begin(), bytes, uint8_t{0});
	}

	BufferHandle NullBackend::createBuffer(BufferUsage usage, size_t bytes, const void* data) {
		BufferRecord record{.usage = usage, .bytes = std::vector<uint8_t>(bytes)};
		if (data != nullptr && bytes > 0) {
			std::memcpy(record.bytes.data(), data, bytes);
		}
		return bufferTable.insert(std::move(record));
	}

	void NullBackend::updateBuffer(BufferHandle buffer, size_t offset, size_t bytes, const void* data) {
		BufferRecord* record = bufferTable.find(buffer);
		if (record == nullptr || bytes == 0 || data == nullptr) {
			return;
		}
		if (record->usage == BufferUsage::Stream && offset == 0) {
			record->bytes.assign(bytes, 0);
		} else if (offset + bytes > record->bytes.size()) {
			return;
		}
		std::memcpy(record->bytes.data() + offset, data, bytes);
	}

	GeometryHandle NullBackend::createGeometry(const GeometryDesc& desc) {
		const bool mesh = desc.layout == VertexLayout::InstancedMesh;
		if (bufferTable.find(desc.vertices) == nullptr ||
			(mesh && (bufferTable.find(desc.indices) == nullptr || bufferTable.find(desc.instances) == nullptr))) {
			return {};
		}
		return geometryTable.insert(desc);
	}

	std::span<const std::byte> NullBackend::RecordedDraw::hookBlock(uint32_t binding) const {
		const auto found =
			std::find_if(hookUniforms.begin(), hookUniforms.end(), [binding](const HookUniforms& hook) { return hook.binding == binding; });
		return found != hookUniforms.end() ? std::span<const std::byte>(found->bytes) : std::span<const std::byte>();
	}

	PipelineHandle NullBackend::createPipeline(const ShaderSource& source) {
		return pipelineTable.insert({
			.vertex = source.vertex != nullptr ? source.vertex : "",
			.fragment = source.fragment != nullptr ? source.fragment : "",
			.hookBlocks = {source.hookBlocks.begin(), source.hookBlocks.end()},
		});
	}

	void NullBackend::setUniforms(const DrawUniforms& uniforms) {
		currentUniforms = uniforms;
		uniformHistory.push_back(uniforms);
	}

	void NullBackend::setHookUniforms(uint32_t binding, std::span<const std::byte> bytes) {
		HookUniforms write{.binding = binding, .bytes = {bytes.begin(), bytes.end()}};
		const auto	 at =
			std::find_if(currentHooks.begin(), currentHooks.end(), [binding](const HookUniforms& hook) { return hook.binding >= binding; });
		if (at != currentHooks.end() && at->binding == binding) {
			at->bytes = write.bytes;
		} else {
			currentHooks.insert(at, write);
		}
		hookHistory.push_back(std::move(write));
	}

	void NullBackend::draw(std::span<const DrawCall> calls) {
		for (const DrawCall& call : calls) {
			recordedDraws.push_back(
				{.call = call, .uniforms = currentUniforms, .hookUniforms = currentHooks, .submission = submissionCount}
			);
		}
		++submissionCount;
	}

	void NullBackend::beginForeignDraws(PipelineHandle pipeline, BlendState /*blend*/) {
		++foreignDrawBegins;
		lastForeignPipeline = pipeline;
	}

	void NullBackend::writeTimestamp(QueryHandle query) {
		if (bool* written = queryTable.find(query); written != nullptr) {
			*written = true;
			++timestampCount;
		}
	}

	bool NullBackend::timestampReady(QueryHandle query) {
		const bool* written = queryTable.find(query);
		return written != nullptr && *written;
	}

	std::span<const uint8_t> NullBackend::bufferContents(BufferHandle buffer) const {
		const BufferRecord* record = bufferTable.find(buffer);
		return record != nullptr ? std::span<const uint8_t>(record->bytes) : std::span<const uint8_t>();
	}

	NullBackend::Counts NullBackend::live() const {
		return {
			.textures = textureTable.size(),
			.targets = targetTable.size(),
			.buffers = bufferTable.size(),
			.geometries = geometryTable.size(),
			.pipelines = pipelineTable.size(),
			.queries = queryTable.size(),
		};
	}

	void NullBackend::clearRecord() {
		recordedDraws.clear();
		uniformHistory.clear();
		hookHistory.clear();
		targetBinds.clear();
		updates.clear();
		clearColors.clear();
		frameCount = 0;
		invalidationCount = 0;
		timestampCount = 0;
		pixelReadCount = 0;
		textureCount = 0;
		submissionCount = 0;
		foreignDrawBegins = 0;
		foreignDrawEnds = 0;
		lastForeignPipeline = {};
	}

} // namespace Renderer::backend
