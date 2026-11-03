#pragma once

// salal_art()'s build step (cmake/SalalArt.cmake): the check that a folder of SVG is what loadSVG
// reads, and the C++ that embeds it.

#include "vector/SvgMarkup.h"

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace renderer::svg {

	// One SVG's check, against what loadSVG reads through nanosvg: paths and the basic shapes (rect,
	// circle, ellipse, line, polyline, polygon), in groups, filled and stroked in colours or linear
	// and radial gradients. What nanosvg drops, or draws as if it were plain geometry, is refused,
	// naming the line and the element: filters, masks, clip paths, text, images, patterns, markers,
	// <use> and <symbol>, <style> sheets, a dash array (loadSVG strokes solid), any reference but one
	// gradient's to another, and a url() that names anything but a gradient in the file. True when it
	// passes.
	bool checkArt(std::string_view file, std::string_view text, std::vector<Problem>& problems);

	// An SVG to embed: its name (the file name less .svg) and its text.
	struct ArtFile {
		std::string name;
		std::string text;
	};

	struct ArtSources {
		std::string header;
		std::string source;
	};

	// The header and source that embed `files` in namespace `nameSpace`: `art(name)`, an SVG's bytes
	// for loadSVG or an empty span, and `artNames()`, sorted. `header` is how the source includes the
	// header; `origin` is the folder, for the banner.
	ArtSources
	generateArtSources(std::span<const ArtFile> files, const std::string& nameSpace, const std::string& header, const std::string& origin);

} // namespace renderer::svg
