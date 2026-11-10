// lintAt and scrollbarsShown: a scene's ui at a window size through the App's scale and the scene's
// frame, a violation as a line, a window held to its minimum size, a Retina pixel ratio, and which
// scrollers show a bar.

#include "testing/HeadlessLayout.h"

#include "tests/AppHarness.h"

#include <ui/shapes/Rectangle.h>

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace app::testing {

	namespace {

		inline constexpr char kA[] = "A";
		using SceneA = Recording<kA>;

		// A 1900 x 1000 panel, which fits a 1920 x 1080 canvas and nothing smaller, over a list that
		// scrolls when it's given more than 300 px of rows.
		class PanelScene final : public TypedScene<> {
		  public:
			const char* name() const override { return "Panel"; }
			bool		framed() const override { return true; }
			void		render(float /*alpha*/) override {}
			void		onEnter(App& /*app*/) override {
				   ui::Ref<ui::Component> panel = root().addChild<ui::Rectangle>({.id = "panel", .size = {1900.0F, 1000.0F}});
				   list = panel.addChild<ui::Component>({.id = "list", .size = {200.0F, 300.0F}, .overflow = ui::Overflow::Scroll});
				   rows = list.addChild<ui::Rectangle>({.id = "rows", .size = {200.0F, 100.0F}});
			}

			ui::Ref<ui::Component> list;
			ui::Ref<ui::Rectangle> rows;
		};

		// The panel on a canvas too small for it: past its root, and past the window.
		const std::string kSpill = "child-outside-parent root/panel at 0,0 1900x1000 against root\n"
								   "outside-viewport root/panel at 0,0 1900x1000 against viewport\n";

		class HeadlessLayoutTest : public AppTest {
		  protected:
			static AppConfig referenced() {
				AppConfig scaled = config();
				scaled.uiScale = {
					.referenceWidth = 1920,
					.referenceHeight = 1080,
					.floor = 2.0F / 3.0F,
					.minFactor = 0.8F,
					.maxFactor = 1.2F,
					.frameWidth = 2600.0F,
					.frameHeight = 1440.0F,
				};
				return scaled;
			}
		};

	} // namespace

	TEST_F(HeadlessLayoutTest, ASceneIsLintedAtEachWindowSizeThroughTheScaleAndTheFrame) {
		start(referenced());
		app->scenes().push<PanelScene>();
		frame(0.0);
		// The canvas is 1920 x 1080 or more at each of these, so the panel fits.
		EXPECT_EQ(lintAt(*app, *platform, 1280, 720), "");
		EXPECT_EQ(lintAt(*app, *platform, 1920, 1080), "");
		EXPECT_EQ(lintAt(*app, *platform, 3840, 2160), "");
		EXPECT_EQ(lintAt(*app, *platform, 5120, 1440), ""); // past the frame's cap
		EXPECT_EQ(app->viewport().framebufferWidth, 5120);	// and the window keeps the size

		// At the largest interface size the canvas is 1600 x 900.
		app->setUiScale(1.2F);
		EXPECT_EQ(lintAt(*app, *platform, 1920, 1080), kSpill);
	}

	TEST_F(HeadlessLayoutTest, WithNoReferenceTheWindowIsTheCanvas) {
		start();
		app->scenes().push<PanelScene>();
		frame(0.0);
		EXPECT_EQ(lintAt(*app, *platform, 1920, 1080), "");
		EXPECT_EQ(lintAt(*app, *platform, 1280, 720), kSpill);
	}

	TEST_F(HeadlessLayoutTest, TheSizeIsFramebufferPixelsAtAPixelRatio) {
		start(referenced(), [](HeadlessPlatform& headless) { headless.setPixelRatio(2.0F); });
		app->scenes().push<PanelScene>();
		frame(0.0);
		EXPECT_EQ(lintAt(*app, *platform, 3840, 2160), "");
		EXPECT_EQ(app->viewport().logicalWidth, 1920);
		EXPECT_EQ(app->viewport().framebufferWidth, 3840);
	}

	TEST_F(HeadlessLayoutTest, AWindowHeldToItsMinimumSaysSoInsteadOfLintingAnotherSize) {
		start(referenced(), [](HeadlessPlatform& headless) { headless.setMinimumSize(1280, 720); });
		app->scenes().push<PanelScene>();
		frame(0.0);
		EXPECT_EQ(lintAt(*app, *platform, 800, 600), "the window is 1280x720, not the 800x600 asked for\n");
	}

	TEST_F(HeadlessLayoutTest, ASceneWithNoUiAndNoSceneAtAllAreClean) {
		start();
		EXPECT_EQ(lintAt(*app, *platform, 1280, 720), "");
		EXPECT_TRUE(scrollbarsShown(*app).empty());
		app->scenes().push<SceneA>();
		frame(0.0);
		EXPECT_EQ(lintAt(*app, *platform, 1024, 768), "");
		EXPECT_FALSE(app->scenes().top()->hasUi());
	}

	TEST_F(HeadlessLayoutTest, ScrollbarsShownNamesTheScrollersWhoseContentRunsPastThem) {
		start(referenced());
		app->scenes().push<PanelScene>();
		frame(0.0);
		EXPECT_EQ(lintAt(*app, *platform, 1920, 1080), "");
		EXPECT_TRUE(scrollbarsShown(*app).empty());

		top<PanelScene>().rows.get()->setSize({200.0F, 900.0F});
		frame(0.0);
		EXPECT_EQ(scrollbarsShown(*app), (std::vector<std::string>{"root/panel/list"}));
	}

} // namespace app::testing
