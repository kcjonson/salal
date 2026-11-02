#include "ui/icons/IconAtlas.h"

#include <icon_test/Icons.h>

#include <gtest/gtest.h>

TEST(IconAtlasLoad, FindsIconsByNameAndBuildsTheirQuads) {
	std::string						   error;
	const std::optional<ui::IconAtlas> atlas = ui::IconAtlas::load(icon_test::iconAtlas(), error);
	ASSERT_TRUE(atlas.has_value()) << error;
	EXPECT_EQ(atlas->width(), 96);
	EXPECT_EQ(atlas->height(), 96);
	EXPECT_FLOAT_EQ(atlas->distanceRange(), 8.0F);
	EXPECT_EQ(atlas->pixels().size(), 96U * 96U * 3U);
	EXPECT_EQ(atlas->find("cornr"), nullptr);

	// frame is the fourth icon, the bottom-right cell; the rows upload bottom first, so its cell
	// is the bottom half of the texture, v from 0 to 0.5.
	const ui::IconAtlas::Glyph* frame = atlas->find("frame");
	ASSERT_NE(frame, nullptr);
	EXPECT_FLOAT_EQ(frame->uvMin.x, 0.5F);
	EXPECT_FLOAT_EQ(frame->uvMin.y, 0.0F);
	EXPECT_FLOAT_EQ(frame->uvMax.x, 1.0F);
	EXPECT_FLOAT_EQ(frame->uvMax.y, 0.5F);

	// A 20 px icon: its cell reaches 0.1 of its size past each side.
	const auto quad = atlas->quad("frame", {100.0F, 50.0F}, 20.0F, {1.0F, 0.5F, 0.0F, 1.0F}, Renderer::TextureHandle{});
	ASSERT_TRUE(quad.has_value());
	EXPECT_FLOAT_EQ(quad->position.x, 98.0F);
	EXPECT_FLOAT_EQ(quad->position.y, 48.0F);
	EXPECT_FLOAT_EQ(quad->size.x, 24.0F);
	EXPECT_FLOAT_EQ(quad->pixelRange, 8.0F);
	EXPECT_EQ(quad->color, glm::vec4(1.0F, 0.5F, 0.0F, 1.0F));
	EXPECT_FALSE(atlas->quad("nothing", {}, 20.0F, {}, Renderer::TextureHandle{}).has_value());

	// The decoded pixels, bottom row first: corner's filled top-left quarter sits in the texture's
	// upper half (its cell is the bottom-left one in the PNG, rows 48 to 95 from the top).
	// Inside is where the median of the three channels is past the edge.
	const auto inside = [&](int x, int yFromBottom) {
		const unsigned char* texel = &atlas->pixels()[(static_cast<size_t>(yFromBottom) * 96 + static_cast<size_t>(x)) * 3];
		return std::max(std::min(texel[0], texel[1]), std::min(std::max(texel[0], texel[1]), texel[2])) > 127;
	};
	EXPECT_TRUE(inside(12, 95 - (48 + 12)));
	EXPECT_FALSE(inside(12, 95 - (48 + 36)));
}
