// The ui's scale: fixed with no reference, as it was, and from the window's resolution with one, on the
// framebuffer's pixels whatever the pixel ratio, with the floor, the player's factor held to its range and
// kept in the settings, and a resize a scene hears of with its ui already at the new scale. And the
// window's minimum size under a saved one.

#include "application/App.h"
#include "tests/AppHarness.h"
#include "tests/TempDir.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace app::testing {

	namespace {

		inline constexpr char kA[] = "A";
		using SceneA = Recording<kA>;

		// A scene with a ui, which writes the canvas its ui has each time it hears of a resize.
		class CanvasScene final : public TypedScene<> {
		  public:
			const char* name() const override { return "Canvas"; }
			void		onEnter(App& /*app*/) override { ui(); }
			void		render(float /*alpha*/) override {}
			void		onResize(const Renderer::Viewport& /*viewport*/) override { heard.push_back(ui().getViewport()); }

			std::vector<Foundation::Vec2> heard;
		};

		// The same, opted into the frame.
		class FramedScene final : public TypedScene<> {
		  public:
			const char* name() const override { return "Framed"; }
			void		onEnter(App& /*app*/) override { ui(); }
			void		render(float /*alpha*/) override {}
			bool		framed() const override { return true; }
		};

		struct Case {
			int	  width; // the window, logical
			int	  height;
			float pixelRatio;
			float factor;
			float scale; // framebuffer pixels per ui px
			float canvasWidth;
			float canvasHeight;
		};

		class AppUiScaleTest : public AppTest {
		  protected:
			// dual-deck-builder's numbers.
			static AppConfig referenced() {
				AppConfig scaled = config();
				scaled.uiScale = {
					.referenceWidth = 1920, .referenceHeight = 1080, .floor = 2.0F / 3.0F, .minFactor = 0.8F, .maxFactor = 1.2F
				};
				return scaled;
			}

			// With dual-deck-builder's frame.
			static AppConfig framedConfig() {
				AppConfig scaled = referenced();
				scaled.uiScale.frameWidth = 2600.0F;
				scaled.uiScale.frameHeight = 1440.0F;
				return scaled;
			}

			static std::function<void(HeadlessPlatform&)> window(int width, int height, float pixelRatio = 1.0F) {
				return [=](HeadlessPlatform& headless) {
					headless.setPixelRatio(pixelRatio);
					headless.setWindowSize(width, height);
				};
			}

			Foundation::Vec2 canvas() {
				Scene* scene = app->scenes().top();
				EXPECT_NE(scene, nullptr);
				return scene != nullptr ? scene->ui().getViewport() : Foundation::Vec2{};
			}

			TempDir temp;
		};

	} // namespace

	TEST_F(AppUiScaleTest, WithNoReferenceTheSettingIsTheScaleOverLogicalPixels) {
		start(config(), window(800, 600, 2.0F));
		app->scenes().push<CanvasScene>();
		frame(0.0);
		EXPECT_FLOAT_EQ(app->uiLayoutScale(), 1.0F);
		EXPECT_FLOAT_EQ(app->uiPixelScale(), 2.0F);
		EXPECT_EQ(canvas(), Foundation::Vec2(800.0F, 600.0F));

		app->setUiScale(2.0F);
		EXPECT_FLOAT_EQ(app->uiScale(), 2.0F);
		EXPECT_FLOAT_EQ(app->uiLayoutScale(), 2.0F);
		EXPECT_EQ(canvas(), Foundation::Vec2(400.0F, 300.0F));

		// The window's size doesn't enter into it.
		platform->resize(1600, 1200);
		frame(0.0);
		EXPECT_FLOAT_EQ(app->uiLayoutScale(), 2.0F);
		EXPECT_EQ(canvas(), Foundation::Vec2(800.0F, 600.0F));

		app->setUiScale(100.0F);
		EXPECT_FLOAT_EQ(app->uiScale(), 4.0F);
		app->setUiScale(0.0F);
		EXPECT_FLOAT_EQ(app->uiScale(), 0.25F);
	}

	TEST_F(AppUiScaleTest, WithAReferenceTheScaleFollowsTheFramebufferAndTheCanvasIsTheReferenceOrMore) {
		const std::vector<Case> cases = {
			{.width = 1920,
			 .height = 1080,
			 .pixelRatio = 1.0F,
			 .factor = 1.0F,
			 .scale = 1.0F,
			 .canvasWidth = 1920.0F,
			 .canvasHeight = 1080.0F},
			{.width = 2560,
			 .height = 1440,
			 .pixelRatio = 1.0F,
			 .factor = 1.0F,
			 .scale = 4.0F / 3.0F,
			 .canvasWidth = 1920.0F,
			 .canvasHeight = 1080.0F},
			{.width = 3840,
			 .height = 2160,
			 .pixelRatio = 1.0F,
			 .factor = 1.0F,
			 .scale = 2.0F,
			 .canvasWidth = 1920.0F,
			 .canvasHeight = 1080.0F},
			// The same 4K framebuffer behind a display scale of 200%: the same interface.
			{.width = 1920,
			 .height = 1080,
			 .pixelRatio = 2.0F,
			 .factor = 1.0F,
			 .scale = 2.0F,
			 .canvasWidth = 1920.0F,
			 .canvasHeight = 1080.0F},
			// Wider and taller than the reference: the canvas grows on the axis with room.
			{.width = 3440,
			 .height = 1440,
			 .pixelRatio = 1.0F,
			 .factor = 1.0F,
			 .scale = 4.0F / 3.0F,
			 .canvasWidth = 2580.0F,
			 .canvasHeight = 1080.0F},
			{.width = 2560,
			 .height = 1600,
			 .pixelRatio = 1.0F,
			 .factor = 1.0F,
			 .scale = 4.0F / 3.0F,
			 .canvasWidth = 1920.0F,
			 .canvasHeight = 1200.0F},
			{.width = 1728,
			 .height = 1117,
			 .pixelRatio = 2.0F,
			 .factor = 1.0F,
			 .scale = 1.8F,
			 .canvasWidth = 1920.0F,
			 .canvasHeight = 2234.0F / 1.8F},
			{.width = 1280,
			 .height = 800,
			 .pixelRatio = 1.0F,
			 .factor = 1.0F,
			 .scale = 2.0F / 3.0F,
			 .canvasWidth = 1920.0F,
			 .canvasHeight = 1200.0F},
			// The player's factor: a larger interface on a smaller canvas, and the other way.
			{.width = 1920,
			 .height = 1080,
			 .pixelRatio = 1.0F,
			 .factor = 1.2F,
			 .scale = 1.2F,
			 .canvasWidth = 1600.0F,
			 .canvasHeight = 900.0F},
			{.width = 1920,
			 .height = 1080,
			 .pixelRatio = 1.0F,
			 .factor = 0.8F,
			 .scale = 0.8F,
			 .canvasWidth = 2400.0F,
			 .canvasHeight = 1350.0F},
			// The floor: 0.8 x 2/3 would be under it.
			{.width = 1280,
			 .height = 720,
			 .pixelRatio = 1.0F,
			 .factor = 0.8F,
			 .scale = 2.0F / 3.0F,
			 .canvasWidth = 1920.0F,
			 .canvasHeight = 1080.0F},
			{.width = 640,
			 .height = 360,
			 .pixelRatio = 2.0F,
			 .factor = 0.8F,
			 .scale = 2.0F / 3.0F,
			 .canvasWidth = 1920.0F,
			 .canvasHeight = 1080.0F},
		};
		for (const Case& each : cases) {
			SCOPED_TRACE(
				std::to_string(each.width) + "x" + std::to_string(each.height) + " at " + std::to_string(each.pixelRatio) + ", factor " +
				std::to_string(each.factor)
			);
			start(referenced(), window(each.width, each.height, each.pixelRatio));
			app->setUiScale(each.factor);
			app->scenes().push<CanvasScene>();
			frame(0.0);
			EXPECT_NEAR(app->uiPixelScale(), each.scale, 1e-5F);
			EXPECT_NEAR(app->uiLayoutScale(), each.scale / each.pixelRatio, 1e-5F);
			EXPECT_NEAR(canvas().x, each.canvasWidth, 1e-2F);
			EXPECT_NEAR(canvas().y, each.canvasHeight, 1e-2F);
			app.reset();
		}
	}

	TEST_F(AppUiScaleTest, AResizeReachesASceneWithItsUiAlreadyAtTheNewScale) {
		start(referenced(), window(1920, 1080));
		app->scenes().push<CanvasScene>();
		frame(0.0);
		auto& scene = top<CanvasScene>();
		EXPECT_EQ(canvas(), Foundation::Vec2(1920.0F, 1080.0F));

		platform->resize(3440, 1440);
		frame(0.0);
		ASSERT_EQ(scene.heard.size(), 1U);
		EXPECT_NEAR(scene.heard[0].x, 2580.0F, 1e-2F);
		EXPECT_NEAR(scene.heard[0].y, 1080.0F, 1e-2F);
		EXPECT_EQ(scene.root().get()->getSize(), scene.heard[0]);
	}

	TEST_F(AppUiScaleTest, FullscreenChangesTheScaleAtOnceAndTheScenesUiAtTheNextPoll) {
		start(referenced(), [](HeadlessPlatform& headless) {
			headless.setWindowSize(1920, 1080);
			headless.addMonitor(3840, 2160);
		});
		app->scenes().push<CanvasScene>();
		frame(0.0);
		app->setFullscreen(0);
		EXPECT_FLOAT_EQ(app->uiPixelScale(), 2.0F);
		frame(0.0);
		EXPECT_FLOAT_EQ(top<CanvasScene>().ui().getScale(), 2.0F);
		EXPECT_EQ(canvas(), Foundation::Vec2(1920.0F, 1080.0F));
	}

	TEST_F(AppUiScaleTest, ASceneEnteringLaterAndAUiMadeLaterHaveTheScale) {
		start(referenced(), window(2560, 1440));
		app->scenes().push<SceneA>();
		frame(0.0);
		EXPECT_NEAR(app->scenes().top()->ui().getScale(), 4.0F / 3.0F, 1e-5F);
		EXPECT_NEAR(canvas().x, 1920.0F, 1e-2F);
	}

	TEST_F(AppUiScaleTest, TheFactorIsHeldToItsRangeAndKeptInTheSettings) {
		AppConfig files = referenced();
		files.settingsFile = temp / "settings.json";
		{
			std::ofstream out(temp / "settings.json");
			out << R"({"version": 1, "ui": {"scale": 1.5}})"; // a setting from when it was the scale itself
		}
		start(files, window(1920, 1080));
		EXPECT_FLOAT_EQ(app->uiScale(), 1.2F);
		app->setUiScale(0.5F);
		EXPECT_FLOAT_EQ(app->uiScale(), 0.8F);
		EXPECT_FLOAT_EQ(app->settings().get<float>("ui.scale", 0.0F), 0.8F);
		app->setUiScale(1.1F);
		EXPECT_FLOAT_EQ(app->uiPixelScale(), 1.1F);
	}

	TEST_F(AppUiScaleTest, CreateRefusesHalfAReferenceAndFactorsOutOfOrder) {
		const std::vector<UiScaleConfig> bad = {
			{.referenceWidth = 1920},
			{.referenceWidth = 1920, .referenceHeight = -1},
			{.floor = -1.0F},
			{.minFactor = 0.0F},
			{.minFactor = 2.0F, .maxFactor = 1.0F},
		};
		for (const UiScaleConfig& each : bad) {
			AppConfig refused = config();
			refused.uiScale = each;
			EXPECT_EQ(App::create(std::move(refused), std::make_unique<HeadlessPlatform>(800, 600)), nullptr);
		}
	}

	// 5120 x 1440 is a 3840 x 1080 canvas, past the frame's 2600 in width.
	TEST_F(AppUiScaleTest, ASceneThatOptsInLaysOutInTheFrameAndOneThatDoesNotHasTheWindow) {
		start(framedConfig(), window(5120, 1440));
		app->scenes().push<FramedScene>();
		frame(0.0);
		ui::Context& framed = top<FramedScene>().ui();
		EXPECT_NEAR(framed.getCanvas().x, 3840.0F, 1e-2F);
		EXPECT_FLOAT_EQ(framed.getViewport().x, 2600.0F);
		EXPECT_NEAR(framed.getViewport().y, 1080.0F, 1e-2F);
		EXPECT_NEAR(framed.getFrame().x, 620.0F, 1.0F);
		EXPECT_EQ(framed.background().get()->getSize(), framed.getCanvas());

		// A resize under the cap: the frame is the window.
		platform->resize(2560, 1440);
		frame(0.0);
		EXPECT_NEAR(framed.getViewport().x, 1920.0F, 1e-2F);
		EXPECT_FLOAT_EQ(framed.getFrame().x, 0.0F);

		platform->resize(5120, 1440);
		app->scenes().push<CanvasScene>();
		frame(0.0);
		EXPECT_NEAR(canvas().x, 3840.0F, 1e-2F);
		EXPECT_EQ(top<CanvasScene>().ui().getFrameMax(), std::nullopt);
	}

	TEST_F(AppUiScaleTest, WithNoFrameConfiguredAFramedSceneHasTheWindow) {
		start(referenced(), window(5120, 1440));
		app->scenes().push<FramedScene>();
		frame(0.0);
		EXPECT_NEAR(canvas().x, 3840.0F, 1e-2F);

		AppConfig half = referenced();
		half.uiScale.frameWidth = 2600.0F;
		app.reset();
		EXPECT_EQ(App::create(std::move(half), std::make_unique<HeadlessPlatform>(800, 600)), nullptr);
	}

	TEST_F(AppUiScaleTest, ASavedWindowSizeUnderTheMinimumOpensAtTheMinimum) {
		AppConfig files = referenced();
		files.settingsFile = temp / "settings.json";
		{
			std::ofstream out(temp / "settings.json");
			out << R"({"version": 1, "video": {"windowWidth": 640, "windowHeight": 900}})";
		}
		start(files, [](HeadlessPlatform& headless) { headless.setMinimumSize(1280, 720); });
		EXPECT_EQ(app->viewport().framebufferWidth, 1280);
		EXPECT_EQ(app->viewport().framebufferHeight, 900);
		EXPECT_GE(app->uiPixelScale(), 2.0F / 3.0F);
	}

} // namespace app::testing
