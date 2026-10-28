#pragma once

// PNG encoding. PngEncoder.cpp is the one STB_IMAGE_WRITE_IMPLEMENTATION in anything that links the
// renderer, so a game encodes through these rather than compiling stb_image_write itself. Pixels are
// RGBA8, four bytes each, rows packed.

#include <string>
#include <vector>

namespace Renderer {

	// The rows in reverse order: GL reads the bottom row first, an image starts at the top. Empty for
	// a size that isn't positive.
	std::vector<unsigned char> flipRowsRGBA(const unsigned char* rgba, int width, int height);

	// PNG bytes for pixels whose first row is the top; empty on failure.
	std::vector<unsigned char> encodePngToMemory(const unsigned char* rgba, int width, int height);

	// The same, written to `path`; false on failure.
	bool writePngToFile(const unsigned char* rgba, int width, int height, const std::string& path);

} // namespace Renderer
