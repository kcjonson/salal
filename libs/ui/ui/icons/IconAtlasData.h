#pragma once

// What salal_icons() compiles into a target (docs/ui-design.md, "Icons"): the MSDF atlas PNG, its
// msdf-atlas-gen JSON and the name table from an SVG's file name to its private-use codepoint.
// The icon service owns the atlas texture it uploads from this and builds glyph quads from the JSON.

#include <cstdint>
#include <span>
#include <string_view>

namespace ui {

	struct IconName {
		std::string_view name; // the file name without .svg: icons/home.svg is "home"
		std::uint32_t	 codepoint;
	};

	struct IconAtlasData {
		std::string_view				  name; // the namespace it was generated into
		std::span<const unsigned char>	  png;
		std::span<const std::string_view> json;	 // in pieces, since MSVC caps one string literal
		std::span<const IconName>		  names; // sorted by name
	};

} // namespace ui
