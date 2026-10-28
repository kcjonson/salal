#include "PngEncoder.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <cstddef>
#include <cstring>

namespace Renderer {

	std::vector<unsigned char> flipRowsRGBA(const unsigned char* rgba, int width, int height) {
		if (width <= 0 || height <= 0) {
			return {};
		}
		const std::size_t		   rowBytes = static_cast<std::size_t>(width) * 4;
		std::vector<unsigned char> flipped(rowBytes * static_cast<std::size_t>(height));
		for (int y = 0; y < height; ++y) {
			std::memcpy(
				flipped.data() + (static_cast<std::size_t>(height - 1 - y) * rowBytes),
				rgba + (static_cast<std::size_t>(y) * rowBytes),
				rowBytes
			);
		}
		return flipped;
	}

	std::vector<unsigned char> encodePngToMemory(const unsigned char* rgba, int width, int height) {
		std::vector<unsigned char> out;
		auto					   append = [](void* context, void* data, int size) {
			  auto*		  bytes = static_cast<std::vector<unsigned char>*>(context);
			  const auto* first = static_cast<const unsigned char*>(data);
			  bytes->insert(bytes->end(), first, first + size); // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
		};
		if (width <= 0 || height <= 0 || stbi_write_png_to_func(append, &out, width, height, 4, rgba, width * 4) == 0) {
			out.clear();
		}
		return out;
	}

	bool writePngToFile(const unsigned char* rgba, int width, int height, const std::string& path) {
		return width > 0 && height > 0 && stbi_write_png(path.c_str(), width, height, 4, rgba, width * 4) != 0;
	}

} // namespace Renderer
