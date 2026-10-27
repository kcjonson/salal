#pragma once

// A font family is a small id the game picks for each atlas it hands
// FontRenderer::Initialize, from 0 to 254. Salal names no families; a game
// defines its own as constants:
//
//   inline constexpr Renderer::FontFamily kBodyFont{0};
//   inline constexpr Renderer::FontFamily kMonoFont{1};
//
// FontFamily::Default is not a family: it stands for whichever family the game
// named as its default at Initialize, and every draw and measure that names no
// family uses it.
//
// Lives in primitives/ rather than font/ so TextArgs can name a family without
// including FontRenderer.h.

#include <cstdint>

namespace Renderer { // NOLINT(readability-identifier-naming)

	enum class FontFamily : std::uint8_t {
		Default = 0xFF,
	};

} // namespace Renderer
