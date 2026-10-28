// PNG encoding, decoded again with stb_image to check what was written. Headless.

#include "PngEncoder.h"

#include <gtest/gtest.h>

#include <stb_image.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Renderer {

	namespace {
		// Three rows of two pixels, each pixel's bytes distinct.
		constexpr std::array<unsigned char, 24> kPixels{
			10,	 20,  30,  255, 40,	 50,  60,  128, //
			70,	 80,  90,  255, 100, 110, 120, 0,	//
			130, 140, 150, 255, 160, 170, 180, 64,
		};

		struct Decoded {
			int						   width = 0;
			int						   height = 0;
			std::vector<unsigned char> rgba;
		};

		Decoded decode(stbi_uc* pixels, int width, int height) {
			Decoded decoded{.width = width, .height = height};
			if (pixels != nullptr) {
				decoded.rgba.assign(pixels, pixels + (static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4));
				stbi_image_free(pixels);
			}
			return decoded;
		}

		Decoded decodeMemory(const std::vector<unsigned char>& png) {
			int		 width = 0;
			int		 height = 0;
			int		 channels = 0;
			stbi_uc* pixels = stbi_load_from_memory(png.data(), static_cast<int>(png.size()), &width, &height, &channels, 4);
			return decode(pixels, width, height);
		}
	} // namespace

	TEST(PngEncoderTest, EncodesWhatDecodesToTheSamePixels) {
		stbi_set_flip_vertically_on_load(0);
		const std::vector<unsigned char> png = encodePngToMemory(kPixels.data(), 2, 3);
		ASSERT_GE(png.size(), 24U);
		EXPECT_EQ(
			std::vector<unsigned char>(png.begin(), png.begin() + 8),
			(std::vector<unsigned char>{0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'})
		);

		const Decoded decoded = decodeMemory(png);
		EXPECT_EQ(decoded.width, 2);
		EXPECT_EQ(decoded.height, 3);
		EXPECT_EQ(decoded.rgba, std::vector<unsigned char>(kPixels.begin(), kPixels.end()));
	}

	TEST(PngEncoderTest, FlipReversesTheRows) {
		const std::vector<unsigned char> flipped = flipRowsRGBA(kPixels.data(), 2, 3);
		std::vector<unsigned char>		 expected(kPixels.begin() + 16, kPixels.end());
		expected.insert(expected.end(), kPixels.begin() + 8, kPixels.begin() + 16);
		expected.insert(expected.end(), kPixels.begin(), kPixels.begin() + 8);
		EXPECT_EQ(flipped, expected);
	}

	TEST(PngEncoderTest, ASizeThatIsntPositiveGivesNothing) {
		EXPECT_TRUE(flipRowsRGBA(kPixels.data(), 0, 3).empty());
		EXPECT_TRUE(encodePngToMemory(kPixels.data(), 2, 0).empty());
		EXPECT_TRUE(encodePngToMemory(kPixels.data(), -1, 3).empty());
		EXPECT_FALSE(writePngToFile(kPixels.data(), 0, 0, "unused.png"));
	}

	TEST(PngEncoderTest, WritesAFileThatDecodesToTheSamePixels) {
		stbi_set_flip_vertically_on_load(0);
		const std::filesystem::path path = std::filesystem::temp_directory_path() / "salal_png_encoder_test.png";
		ASSERT_TRUE(writePngToFile(kPixels.data(), 2, 3, path.string()));

		int			  width = 0;
		int			  height = 0;
		int			  channels = 0;
		stbi_uc*	  pixels = stbi_load(path.string().c_str(), &width, &height, &channels, 4);
		const Decoded decoded = decode(pixels, width, height);
		std::filesystem::remove(path);
		EXPECT_EQ(decoded.width, 2);
		EXPECT_EQ(decoded.height, 3);
		EXPECT_EQ(decoded.rgba, std::vector<unsigned char>(kPixels.begin(), kPixels.end()));
	}

} // namespace Renderer
