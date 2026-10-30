#pragma once

// salal_icons()' build step (cmake/SalalIcons.cmake): a folder of SVGs that pass the icon rules
// turned into one MSDF atlas on msdfgen, its msdf-atlas-gen JSON, and the C++ that embeds both with
// the name table.

#include "ui/iconbuild/SvgCheck.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace ui::iconbuild {

	inline constexpr std::uint32_t kFirstCodepoint = 0xE000; // the start of the private use area

	struct IconSource {
		std::string name; // the file name without .svg
		std::string file; // for messages
		std::string text;
	};

	// Every icon gets a square cell of cellSize px; its viewBox fills the cell less half the
	// distance range on each side, so the field reaches the cell's edge and no further.
	struct AtlasOptions {
		int cellSize = 48;
		int distanceRange = 8;
	};

	struct BuiltIcon {
		std::string	  name;
		std::uint32_t codepoint = 0;
		int			  x = 0; // the cell's top-left in the atlas, in px
		int			  y = 0;
	};

	struct BuiltAtlas {
		int						  width = 0;
		int						  height = 0;
		std::vector<std::uint8_t> rgb; // the top row first
		std::vector<BuiltIcon>	  icons;
		std::string				  json;
	};

	// The icons in name order, codepoints from kFirstCodepoint in that order, cells in rows. Null,
	// with every problem, if any icon breaks a rule or has a name that isn't [A-Za-z0-9_-]+.
	std::optional<BuiltAtlas> buildAtlas(std::vector<IconSource> icons, const AtlasOptions& options, std::vector<Problem>& problems);

	std::vector<std::uint8_t> encodePng(const BuiltAtlas& atlas);

	struct IconSources {
		std::string header;
		std::string source;
	};

	// The header declaring <nameSpace>::iconAtlas() and the source holding the atlas, its JSON and
	// the name table. `header` is how the source includes the header; `origin` names the folder.
	IconSources generateIconSources(
		const BuiltAtlas&			  atlas,
		std::span<const std::uint8_t> png,
		const std::string&			  nameSpace,
		const std::string&			  header,
		const std::string&			  origin
	);

} // namespace ui::iconbuild
