// The frame: the window capped and centred, with the canvas under, at and past the cap on each axis; the
// roots and the overlays against it and the background root against the window; the pointer through the
// frame's offset; a scale under it; and the lint held to the window.

#include "ui/Context.h"
#include "ui/components/Dialog.h"
#include "ui/debug/LayoutLint.h"
#include "ui/shapes/Rectangle.h"

#include <gtest/gtest.h>

#include <string>

namespace ui {

	namespace {

		constexpr Foundation::Vec2 kCap{2600.0F, 1440.0F};

		using foundation::InputEvent;

		Rectangle::Args box(const char* id, float x, float y, float width, float height) {
			return {.id = id, .position = {x, y}, .size = {width, height}, .pointerEvents = PointerEvents::Auto};
		}

		std::string idAt(Context& context, Foundation::Vec2 point) {
			context.layout();
			const Component* hit = context.find(context.hitTest(point));
			return hit != nullptr ? hit->getId() : std::string("none");
		}

		void expectRect(const Foundation::Rect& actual, const Foundation::Rect& expected) {
			EXPECT_FLOAT_EQ(actual.x, expected.x);
			EXPECT_FLOAT_EQ(actual.y, expected.y);
			EXPECT_FLOAT_EQ(actual.width, expected.width);
			EXPECT_FLOAT_EQ(actual.height, expected.height);
		}

	} // namespace

	TEST(UiFrameTest, WithNoFrameTheViewportIsTheWindow) {
		Context context({3840.0F, 1080.0F});
		expectRect(context.getFrame(), {0.0F, 0.0F, 3840.0F, 1080.0F});
		EXPECT_EQ(context.getViewport(), context.getCanvas());
		context.layout();
		expectRect(context.root().get()->screenBounds(), {0.0F, 0.0F, 3840.0F, 1080.0F});
	}

	TEST(UiFrameTest, TheFrameIsTheCanvasCappedOnEachAxisAndCentred) {
		struct Case {
			Foundation::Vec2 canvas;
			Foundation::Rect frame;
		};
		const Case cases[] = {
			{.canvas = {1920.0F, 1080.0F}, .frame = {0.0F, 0.0F, 1920.0F, 1080.0F}},	 // under on both
			{.canvas = {2600.0F, 1440.0F}, .frame = {0.0F, 0.0F, 2600.0F, 1440.0F}},	 // at the cap
			{.canvas = {2580.0F, 1080.0F}, .frame = {0.0F, 0.0F, 2580.0F, 1080.0F}},	 // 21:9, still under
			{.canvas = {3840.0F, 1080.0F}, .frame = {620.0F, 0.0F, 2600.0F, 1080.0F}},	 // 32:9: past it in width
			{.canvas = {1920.0F, 1536.0F}, .frame = {0.0F, 48.0F, 1920.0F, 1440.0F}},	 // 5:4: past it in height
			{.canvas = {3000.0F, 2000.0F}, .frame = {200.0F, 280.0F, 2600.0F, 1440.0F}}, // past it in both
		};
		for (const Case& each : cases) {
			SCOPED_TRACE(std::to_string(each.canvas.x) + " x " + std::to_string(each.canvas.y));
			Context context(each.canvas);
			context.setFrame(kCap);
			Ref<Component> first = context.root();
			Ref<Component> second = context.addRoot<Component>({.id = "second"});
			Ref<Component> behind = context.background();
			context.layout();
			expectRect(context.getFrame(), each.frame);
			EXPECT_EQ(context.getViewport(), Foundation::Vec2(each.frame.width, each.frame.height));
			EXPECT_EQ(context.getCanvas(), each.canvas);
			expectRect(first.get()->screenBounds(), each.frame);
			expectRect(second.get()->screenBounds(), each.frame);
			expectRect(behind.get()->screenBounds(), {0.0F, 0.0F, each.canvas.x, each.canvas.y});
		}
	}

	TEST(UiFrameTest, ResizingAndClearingTheFrameMoveTheRoots) {
		Context context({3840.0F, 1080.0F});
		context.setFrame(kCap);
		Ref<Component> fill = context.root().addChild<Rectangle>({.id = "fill", .size = {percent(100.0F), percent(100.0F)}});
		context.layout();
		expectRect(fill.get()->screenBounds(), {620.0F, 0.0F, 2600.0F, 1080.0F});

		context.setViewport({1920.0F, 1080.0F});
		context.layout();
		expectRect(fill.get()->screenBounds(), {0.0F, 0.0F, 1920.0F, 1080.0F});

		context.setViewport({3840.0F, 1080.0F});
		context.setFrame(std::nullopt);
		context.layout();
		expectRect(fill.get()->screenBounds(), {0.0F, 0.0F, 3840.0F, 1080.0F});

		context.setFrame(Foundation::Vec2{0.0F, 100.0F}); // refused
		EXPECT_EQ(context.getFrameMax(), std::nullopt);
	}

	// A 5120 x 1440 window at a scale of 4/3 is a 3840 x 1080 canvas.
	TEST(UiFrameTest, TheFrameIsInTheUisPxUnderAScale) {
		Context context({5120.0F, 1440.0F});
		context.setViewport({5120.0F, 1440.0F}, 4.0F / 3.0F);
		context.setFrame(kCap);
		context.layout();
		EXPECT_FLOAT_EQ(context.getCanvas().x, 3840.0F);
		EXPECT_FLOAT_EQ(context.getFrame().width, 2600.0F);
		EXPECT_FLOAT_EQ(context.getFrame().height, 1080.0F);
		// Centred, on a whole pixel of the window: 620 ui px is 826.67 of them, so 826.
		EXPECT_FLOAT_EQ(context.getFrame().x * context.getScale(), 826.0F);
	}

	TEST(UiFrameTest, ThePointerReachesFramedContentThroughTheOffsetAndTheBackgroundPastIt) {
		Context context({3840.0F, 1080.0F});
		context.setFrame(kCap);
		context.root().addChild<Rectangle>(box("button", 10.0F, 10.0F, 100.0F, 40.0F));
		context.background().addChild<Rectangle>(box("table", 0.0F, 0.0F, 3840.0F, 1080.0F));
		EXPECT_EQ(idAt(context, {20.0F, 20.0F}), "table");	 // left of the frame
		EXPECT_EQ(idAt(context, {640.0F, 20.0F}), "button"); // 620 + 20
		EXPECT_EQ(idAt(context, {800.0F, 20.0F}), "table");	 // in the frame, past the button: the background shows through
		EXPECT_TRUE(context.dispatch({.type = InputEvent::Type::PointerMove, .position = {640.0F, 20.0F}}));

		// The background paints under a root added before it.
		const std::vector<ComponentId>& order = context.paintOrder();
		ASSERT_FALSE(order.empty());
		EXPECT_EQ(context.find(order.front())->getId(), "background");
	}

	TEST(UiFrameTest, OverlaysLayOutAgainstTheFrameAndADialogsScrimCoversTheWindow) {
		Context context({3840.0F, 1080.0F});
		context.setFrame(kCap);
		const Foundation::Rect frame = context.getFrame();
		Ref<Component>		   opener = context.root().addChild<Rectangle>(box("opener", 0.0F, 0.0F, 100.0F, 40.0F));

		// A cover is the frame: a toast stack's corner is the frame's.
		Ref<Rectangle> cover =
			opener.get()->openOverlay<Rectangle>({.id = "cover"}, {.kind = OverlayKind::Toast, .placement = Absolute{.cover = true}});
		// A margined absolute entry is kept inside the frame, not the window.
		Ref<Rectangle> pinned = opener.get()->openOverlay<Rectangle>(
			box("pinned", 0.0F, 0.0F, 200.0F, 100.0F),
			{.kind = OverlayKind::Toast, .placement = Absolute{.at = {3800.0F, 0.0F}, .margin = 8.0F}}
		);
		// An anchored entry flips and clamps inside the frame: the opener is at the frame's left edge.
		Ref<Rectangle> menu = opener.get()->openOverlay<Rectangle>(
			box("menu", 0.0F, 0.0F, 300.0F, 100.0F), {.kind = OverlayKind::Menu, .placement = Anchored{.side = Side::Left}}
		);
		context.layout();
		expectRect(cover.get()->screenBounds(), frame);
		EXPECT_FLOAT_EQ(pinned.get()->screenBounds().right(), frame.right() - 8.0F);
		EXPECT_FLOAT_EQ(pinned.get()->screenBounds().y, frame.y + 8.0F);
		EXPECT_GE(menu.get()->screenBounds().x, frame.x);

		// A dialog's scrim is the window's, and its box is centred in the frame.
		Ref<Dialog> dialog = Dialog::open(*opener.get(), {.title = "Quit?", .panelSize = {400.0F, 200.0F}});
		context.layout();
		expectRect(dialog.get()->screenBounds(), {0.0F, 0.0F, 3840.0F, 1080.0F});
		const Component* panel = context.find(dialog.get()->getChildren().front());
		ASSERT_NE(panel, nullptr);
		const Foundation::Rect panelBox = panel->screenBounds();
		EXPECT_FLOAT_EQ(panelBox.x + (panelBox.width * 0.5F), frame.x + (frame.width * 0.5F));
		EXPECT_FLOAT_EQ(panelBox.y + (panelBox.height * 0.5F), frame.y + (frame.height * 0.5F));

		// A resize moves the covers with the frame.
		context.setViewport({3000.0F, 1080.0F});
		context.layout();
		expectRect(cover.get()->screenBounds(), {200.0F, 0.0F, 2600.0F, 1080.0F});
		expectRect(dialog.get()->screenBounds(), {0.0F, 0.0F, 3000.0F, 1080.0F});
	}

	TEST(UiFrameTest, TheLintHoldsFramedContentToItsRootAndTheBackgroundToTheWindow) {
		Context context({3840.0F, 1080.0F});
		context.setFrame(kCap);
		context.root().addChild<Rectangle>(box("inside", 0.0F, 0.0F, 2500.0F, 1080.0F));
		context.background().addChild<Rectangle>(box("table", 0.0F, 0.0F, 3840.0F, 1080.0F));
		context.layout();
		EXPECT_EQ(lint(context, context.getCanvas()).violations.size(), 0U);

		context.root().addChild<Rectangle>(box("spill", 2590.0F, 0.0F, 20.0F, 20.0F)); // past the frame, inside the window
		context.layout();
		const LintResult result = lint(context, context.getCanvas());
		ASSERT_EQ(result.violations.size(), 1U);
		EXPECT_EQ(result.violations[0].rule, LintRule::ChildOutsideParent);
	}

} // namespace ui
