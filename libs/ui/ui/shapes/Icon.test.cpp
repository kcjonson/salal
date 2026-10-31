// An Icon through the scene's icon service and Primitives on the null backend. Headless; no GL.

#include "ui/shapes/Icon.h"
#include "ui/Context.h"

#include "BaseShader.h"
#include "backend/NullBackend.h"
#include "primitives/Primitives.h"

#include <icon_test/Icons.h>

#include <gtest/gtest.h>

#include <memory>

namespace ui {

	class IconDrawTest : public ::testing::Test {
	  protected:
		void SetUp() override {
			auto owned = std::make_unique<Renderer::backend::NullBackend>();
			gpu = owned.get();
			Renderer::Primitives::init(std::move(owned), base_shader::source());
			Renderer::Primitives::setCoordinateSystem(nullptr);
			Renderer::Primitives::setViewport(800, 600);
			gpu->clearRecord();
		}

		void TearDown() override { Renderer::Primitives::shutdown(); }

		Renderer::backend::NullBackend* gpu = nullptr;
	};

	// The atlas uploads on the first draw, as the service's texture; an unknown name draws nothing.
	TEST_F(IconDrawTest, AnIconDrawsOnItsAtlasAndAnUnknownOneDoesNot) {
		Context context({800.0F, 600.0F});
		ASSERT_TRUE(context.icons().add(icon_test::iconAtlas()));
		EXPECT_TRUE(context.icons().has("frame"));
		context.root().addChild<Icon>(
			{.position = {10.0F, 10.0F},
			 .size = {40.0F, 20.0F},
			 .style = {.color = Foundation::Color(1.0F, 0.0F, 0.0F, 1.0F)},
			 .name = "frame"}
		);
		context.root().addChild<Icon>({.position = {60.0F, 10.0F}, .size = {20.0F, 20.0F}, .name = "nothing"});
		context.layout();

		Renderer::Primitives::beginFrame();
		context.render();
		Renderer::Primitives::endFrame();

		ASSERT_EQ(gpu->draws().size(), 1U);
		const Renderer::TextureDesc* atlas = gpu->texture(gpu->draws()[0].call.texture);
		ASSERT_NE(atlas, nullptr);
		EXPECT_EQ(atlas->width, 96);
		EXPECT_EQ(atlas->format, Renderer::TextureFormat::Rgb8);
	}

} // namespace ui
