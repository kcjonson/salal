#include "backend/NullBackend.h"

#include <algorithm>
#include <cstring>

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

	void NullBackend::readPixels(RenderTargetHandle /*target*/, std::span<uint8_t> rgba) {
		std::fill(rgba.begin(), rgba.end(), uint8_t{0});
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
		const bool instancesOk = desc.layout != VertexLayout::InstancedMesh || bufferTable.find(desc.instances) != nullptr;
		if (bufferTable.find(desc.vertices) == nullptr || bufferTable.find(desc.indices) == nullptr || !instancesOk) {
			return {};
		}
		return geometryTable.insert(desc);
	}

	PipelineHandle NullBackend::createPipeline(const ShaderSource& source) {
		return pipelineTable.insert({source.vertex != nullptr ? source.vertex : "", source.fragment != nullptr ? source.fragment : ""});
	}

	void NullBackend::setUniforms(const DrawUniforms& uniforms) {
		currentUniforms = uniforms;
		uniformHistory.push_back(uniforms);
	}

	void NullBackend::draw(std::span<const DrawCall> calls) {
		for (const DrawCall& call : calls) {
			recordedDraws.push_back({.call = call, .uniforms = currentUniforms, .submission = submissionCount});
		}
		++submissionCount;
	}

	void NullBackend::beginForeignDraws(PipelineHandle pipeline, BlendState /*blend*/) {
		++foreignDrawBegins;
		lastForeignPipeline = pipeline;
	}

	void NullBackend::writeTimestamp(QueryHandle query) {
		if (queryTable.find(query) != nullptr) {
			++timestampCount;
		}
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
		targetBinds.clear();
		updates.clear();
		clearColors.clear();
		frameCount = 0;
		invalidationCount = 0;
		timestampCount = 0;
		submissionCount = 0;
		foreignDrawBegins = 0;
		foreignDrawEnds = 0;
		lastForeignPipeline = {};
	}

} // namespace Renderer::backend
