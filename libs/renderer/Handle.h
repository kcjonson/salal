#pragma once

// Renderer-owned resource handles: a 16-bit slot index and a 16-bit generation,
// one distinct type per Tag so a TextureHandle cannot be passed where another
// kind of handle is expected. A default-constructed handle is invalid.

#include <cstdint>

namespace Renderer { // NOLINT(readability-identifier-naming)

	template <typename Tag>
	struct Handle {
		static constexpr uint32_t kInvalid = 0xFFFFFFFFU;

		uint32_t value = kInvalid;

		[[nodiscard]] bool	   isValid() const { return value != kInvalid; }
		[[nodiscard]] uint16_t index() const { return static_cast<uint16_t>(value & 0xFFFFU); }
		[[nodiscard]] uint16_t generation() const { return static_cast<uint16_t>(value >> 16U); }

		static Handle make(uint16_t index, uint16_t generation) {
			return Handle{(static_cast<uint32_t>(generation) << 16U) | static_cast<uint32_t>(index)};
		}

		bool operator==(const Handle&) const = default;
	};

	using TextureHandle = Handle<struct TextureTag>;

	// The backend's objects (backend/Backend.h); TextureHandle is the only one consumers hold.
	using BufferHandle = Handle<struct BufferTag>;
	using GeometryHandle = Handle<struct GeometryTag>;
	using RenderTargetHandle = Handle<struct RenderTargetTag>;
	using PipelineHandle = Handle<struct PipelineTag>;
	using QueryHandle = Handle<struct QueryTag>;

} // namespace Renderer
