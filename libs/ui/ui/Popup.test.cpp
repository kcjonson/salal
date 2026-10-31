// The popup stack: slots and paint order, hit testing that follows it, entries out of their opener's
// clip and flow, placement, dismissal by an outside press, exclusivity, modality per player, bringing
// an entry to the front, removal, the lint, and an entry's draws escaping its opener's clip.

#include "ui/Context.h"
#include "ui/debug/LayoutLint.h"
#include "ui/shapes/Rectangle.h"

#include "BaseShader.h"
#include "backend/NullBackend.h"
#include "primitives/Primitives.h"

#include <gtest/gtest.h>

#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace ui {

	namespace {

		constexpr Foundation::Vec2 kViewport{800.0F, 600.0F};
		const Foundation::Rect	   kScreen{0.0F, 0.0F, 800.0F, 600.0F};

		using foundation::InputEvent;
		using foundation::MouseButton;

		Rectangle::Args box(const char* id, float x, float y, float width, float height) {
			return {.id = id, .position = {x, y}, .size = {width, height}};
		}

		PopupOptions menuBelow(int player = 0, std::function<void()> onDismiss = nullptr) {
			return {.kind = PopupKind::Menu, .player = player, .placement = Placement{}, .onDismiss = std::move(onDismiss)};
		}

		class UiPopupTest : public ::testing::Test {
		  protected:
			bool press(Foundation::Vec2 at, MouseButton button = MouseButton::Left) {
				const bool taken = context.dispatch({.type = InputEvent::Type::PointerDown, .mouseButton = button, .position = at});
				context.dispatch({.type = InputEvent::Type::PointerUp, .mouseButton = button, .position = at});
				context.layout();
				return taken;
			}

			std::string idAt(Foundation::Vec2 point) {
				context.layout();
				const Component* hit = context.find(context.hitTest(point));
				return hit != nullptr ? hit->getId() : std::string("none");
			}

			Context		   context{kViewport};
			Ref<Component> root = context.root();
		};

	} // namespace

	// --- Slots and order ---

	TEST_F(UiPopupTest, ADialogOpenedFromAMenuLandsAboveItAndATooltipAboveBoth) {
		const Ref<Rectangle> trigger = root.addChild<Rectangle>(box("trigger", 10.0F, 10.0F, 100.0F, 30.0F));
		const Ref<Rectangle> menu = trigger.get()->openPopup<Rectangle>(box("menu", 0.0F, 0.0F, 200.0F, 200.0F), menuBelow());
		const Ref<Rectangle> row = menu.addChild<Rectangle>(box("row", 0.0F, 0.0F, 200.0F, 30.0F));
		const Ref<Rectangle> dialog = row.get()->openPopup<Rectangle>({.id = "dialog"}, {.kind = PopupKind::Dialog, .cover = true});
		const Ref<Rectangle> tip = trigger.get()->openPopup<Rectangle>(
			box("tip", 0.0F, 0.0F, 60.0F, 20.0F), {.kind = PopupKind::Tooltip, .placement = Placement{.side = Side::Above}}
		);
		context.layout();

		EXPECT_EQ(context.popups(), (std::vector{menu.componentId(), dialog.componentId(), tip.componentId()}));
		EXPECT_EQ(trigger.get()->paintSlot(), 0);
		EXPECT_EQ(menu.get()->paintSlot(), 1);
		EXPECT_EQ(row.get()->paintSlot(), 1);
		EXPECT_EQ(dialog.get()->paintSlot(), 2);
		EXPECT_EQ(tip.get()->paintSlot(), kTooltipSlot);
		EXPECT_LT(trigger.get()->drawKey(), menu.get()->drawKey());
		EXPECT_LT(row.get()->drawKey(), dialog.get()->drawKey());
		EXPECT_LT(dialog.get()->drawKey(), tip.get()->drawKey());
		EXPECT_EQ(dialog.get()->getSize(), kViewport);

		// The dialog covers the menu; the tooltip is never hit.
		EXPECT_EQ(idAt({20.0F, 60.0F}), "dialog");
		const Foundation::Rect shown = tip.get()->screenBounds();
		EXPECT_EQ(idAt({shown.x + 5.0F, shown.y + 5.0F}), "dialog");
	}

	TEST_F(UiPopupTest, AnEntryEscapesItsOpenersClipAndLeavesItsFlow) {
		const Ref<Component> list = root.addChild<Component>({
			.id = "list",
			.size = {100.0F, 100.0F},
			.layout = {.display = Display::Flex, .flexDirection = FlexDirection::Column},
			.overflow = Overflow::Scroll,
		});
		const Ref<Rectangle> first = list.addChild<Rectangle>(box("first", 0.0F, 0.0F, 100.0F, 40.0F));
		const Ref<Rectangle> second = list.addChild<Rectangle>(box("second", 0.0F, 0.0F, 100.0F, 40.0F));
		const Ref<Rectangle> menu = first.get()->openPopup<Rectangle>(box("menu", 0.0F, 0.0F, 150.0F, 300.0F), menuBelow());
		context.layout();

		EXPECT_FALSE(menu.get()->getClip().has_value());
		EXPECT_TRUE(second.get()->getClip().has_value());
		EXPECT_EQ(second.get()->getBounds().y, 40.0F);
		EXPECT_EQ(list.get()->getScrollMax(), Foundation::Vec2(0.0F, 0.0F));
		const Foundation::Rect placed = menu.get()->screenBounds();
		EXPECT_EQ(placed.x, 8.0F);	// kept the margin inside the viewport
		EXPECT_EQ(placed.y, 44.0F); // under the first row, 4 px off it
		EXPECT_EQ(idAt({140.0F, 200.0F}), "menu");
	}

	// --- Placement (R12.30) ---

	TEST_F(UiPopupTest, PlacementTakesItsSideFlipsWhenThatOverflowsAndClampsInside) {
		const Foundation::Vec2 size{200.0F, 100.0F};
		const Foundation::Rect below = place({100.0F, 100.0F, 50.0F, 20.0F}, size, kScreen, {});
		EXPECT_EQ(below.x, 100.0F);
		EXPECT_EQ(below.y, 124.0F);
		EXPECT_EQ(place({100.0F, 560.0F, 50.0F, 20.0F}, size, kScreen, {}).y, 456.0F);
		EXPECT_EQ(place({750.0F, 100.0F, 50.0F, 20.0F}, size, kScreen, {}).x, 592.0F);
		const Foundation::Rect right =
			place({100.0F, 100.0F, 50.0F, 20.0F}, {100.0F, 40.0F}, kScreen, {.side = Side::Right, .align = Align::Center});
		EXPECT_EQ(right.x, 154.0F);
		EXPECT_EQ(right.y, 90.0F);
		const Foundation::Rect atPoint = place({}, {100.0F, 50.0F}, kScreen, {.at = Foundation::Vec2{790.0F, 590.0F}});
		EXPECT_EQ(atPoint.x, 692.0F);
		EXPECT_EQ(atPoint.y, 536.0F);
		EXPECT_EQ(place({100.0F, 100.0F, 50.0F, 20.0F}, {900.0F, 50.0F}, kScreen, {}).x, 8.0F);
	}

	// --- Dismissal (R9.13) ---

	TEST_F(UiPopupTest, AnOutsidePressClosesAMenuAndIsConsumedUnlessItIsSecondaryOrOnATrigger) {
		const Ref<Rectangle> opener = root.addChild<Rectangle>(box("opener", 0.0F, 0.0F, 100.0F, 30.0F));
		int					 dismissed = 0;
		int					 pressesOnPlain = 0;
		int					 pressesOnTrigger = 0;
		Rectangle::Args		 plainArgs = box("plain", 400.0F, 0.0F, 100.0F, 30.0F);
		plainArgs.onPointerDown = [&](PointerEvent& /*event*/) {
			++pressesOnPlain;
		};
		root.addChild<Rectangle>(plainArgs);
		Rectangle::Args triggerArgs = box("trigger", 600.0F, 0.0F, 100.0F, 30.0F);
		triggerArgs.onPointerDown = [&](PointerEvent& /*event*/) {
			++pressesOnTrigger;
		};
		root.addChild<Rectangle>(triggerArgs).get()->setPopupTrigger(true);
		const auto open = [&] {
			opener.get()->openPopup<Rectangle>(box("menu", 0.0F, 0.0F, 150.0F, 100.0F), menuBelow(0, [&] { ++dismissed; }));
			context.layout();
		};

		open();
		EXPECT_TRUE(press({20.0F, 60.0F})); // inside: it stays
		EXPECT_EQ(context.popups().size(), 1U);
		EXPECT_TRUE(press({300.0F, 400.0F})); // empty space: closed, and the scene doesn't get it
		EXPECT_TRUE(context.popups().empty());
		EXPECT_EQ(dismissed, 1);

		open();
		press({450.0F, 10.0F});
		EXPECT_EQ(pressesOnPlain, 0); // consumed by the close
		open();
		press({450.0F, 10.0F}, MouseButton::Right);
		EXPECT_EQ(pressesOnPlain, 1); // closed, and the right press went on
		open();
		press({650.0F, 10.0F});
		EXPECT_EQ(pressesOnTrigger, 1); // a trigger hears the press that closed it
		EXPECT_EQ(dismissed, 4);
		EXPECT_TRUE(context.popups().empty());
	}

	TEST_F(UiPopupTest, AMenuClosesItsPlayersOtherMenusButNotTheOneItOpensFrom) {
		const Ref<Rectangle> a = root.addChild<Rectangle>(box("a", 0.0F, 0.0F, 100.0F, 30.0F));
		const Ref<Rectangle> b = root.addChild<Rectangle>(box("b", 200.0F, 0.0F, 100.0F, 30.0F));
		int					 dismissed = 0;
		const auto			 count = [&] {
			  ++dismissed;
		};
		const Ref<Rectangle> menuA = a.get()->openPopup<Rectangle>(box("menuA", 0.0F, 0.0F, 100.0F, 100.0F), menuBelow(0, count));
		const Ref<Rectangle> row = menuA.addChild<Rectangle>(box("row", 0.0F, 0.0F, 100.0F, 30.0F));
		row.get()->openPopup<Rectangle>(box("submenu", 0.0F, 0.0F, 100.0F, 100.0F), menuBelow(0, count));
		EXPECT_EQ(context.popups().size(), 2U);

		const Ref<Rectangle> menuB = b.get()->openPopup<Rectangle>(box("menuB", 0.0F, 0.0F, 100.0F, 100.0F), menuBelow(0, count));
		EXPECT_EQ(context.popups(), (std::vector{menuB.componentId()}));
		EXPECT_EQ(dismissed, 2);
		a.get()->openPopup<Rectangle>(box("theirs", 0.0F, 0.0F, 100.0F, 100.0F), menuBelow(1, count));
		EXPECT_EQ(context.popups().size(), 2U); // another player's menu leaves player 0's alone
	}

	// --- Modality ---

	TEST_F(UiPopupTest, ADialogMakesWhatIsUnderItInertToThePointerOnlyForItsPlayer) {
		int				presses = 0;
		Rectangle::Args underArgs = box("under", 0.0F, 0.0F, 100.0F, 100.0F);
		underArgs.onPointerDown = [&](PointerEvent& /*event*/) {
			++presses;
		};
		const Ref<Rectangle> under = root.addChild<Rectangle>(underArgs);

		const Ref<Rectangle> theirs =
			root.get()->openPopup<Rectangle>(box("theirs", 300.0F, 300.0F, 100.0F, 100.0F), {.kind = PopupKind::Dialog, .player = 1});
		press({50.0F, 50.0F});
		EXPECT_EQ(presses, 1); // player 1's dialog doesn't hold the mouse
		context.closePopup(theirs);

		const Ref<Rectangle> dialog =
			root.get()->openPopup<Rectangle>(box("dialog", 300.0F, 300.0F, 100.0F, 100.0F), {.kind = PopupKind::Dialog});
		dialog.addChild<Rectangle>(box("ok", 10.0F, 10.0F, 40.0F, 20.0F));
		EXPECT_TRUE(press({50.0F, 50.0F}));
		EXPECT_EQ(presses, 1);
		EXPECT_EQ(idAt({50.0F, 50.0F}), "none");
		EXPECT_TRUE(context.dispatch({.type = InputEvent::Type::PointerMove, .position = {60.0F, 60.0F}}));
		EXPECT_FALSE(under.get()->isHovered());
		EXPECT_EQ(idAt({320.0F, 320.0F}), "ok");
		EXPECT_EQ(context.popups().size(), 1U); // a press outside a dialog doesn't close it
	}

	// --- Bringing to the front, removal ---

	TEST_F(UiPopupTest, BringingAnEntryToTheFrontLiftsTheEntriesOpenedFromItWithIt) {
		const Ref<Rectangle> toastA =
			root.get()->openPopup<Rectangle>(box("toastA", 600.0F, 10.0F, 180.0F, 60.0F), {.kind = PopupKind::Toast});
		const Ref<Rectangle> fromA = toastA.get()->openPopup<Rectangle>(box("fromA", 0.0F, 0.0F, 100.0F, 60.0F), menuBelow());
		const Ref<Rectangle> toastB =
			root.get()->openPopup<Rectangle>(box("toastB", 600.0F, 80.0F, 180.0F, 60.0F), {.kind = PopupKind::Toast});
		context.bringToFront(toastA);
		context.layout();
		EXPECT_EQ(context.popups(), (std::vector{toastB.componentId(), toastA.componentId(), fromA.componentId()}));
		EXPECT_LT(toastB.get()->paintSlot(), toastA.get()->paintSlot());
		EXPECT_LT(toastA.get()->paintSlot(), fromA.get()->paintSlot());
	}

	TEST_F(UiPopupTest, RemovingAnOpenerClosesItsEntriesWithNoDismissal) {
		const Ref<Rectangle> opener = root.addChild<Rectangle>(box("opener", 0.0F, 0.0F, 100.0F, 30.0F));
		int					 dismissed = 0;
		opener.get()->openPopup<Rectangle>(box("menu", 0.0F, 0.0F, 100.0F, 100.0F), menuBelow(0, [&] { ++dismissed; }));
		root.get()->removeChild(opener);
		EXPECT_TRUE(context.popups().empty());
		EXPECT_EQ(dismissed, 0);
		EXPECT_EQ(idAt({20.0F, 60.0F}), "none");
	}

	// --- The lint ---

	TEST_F(UiPopupTest, TheLintTakesAnEntryAsARootOfItsOwn) {
		const Ref<Rectangle> opener = root.addChild<Rectangle>(box("opener", 10.0F, 10.0F, 100.0F, 30.0F));
		root.addChild<Rectangle>(box("beside", 10.0F, 60.0F, 100.0F, 30.0F));
		opener.get()->openPopup<Rectangle>(box("menu", 0.0F, 0.0F, 150.0F, 100.0F), menuBelow());
		context.layout();
		EXPECT_EQ(lint(context, kViewport).violations.size(), 0U);
	}

	// --- Drawing ---

	class UiPopupDrawTest : public ::testing::Test {
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

		std::vector<Renderer::BatchInstance> painted() const {
			EXPECT_EQ(gpu->draws().size(), 1U);
			const Renderer::backend::NullBackend::RecordedDraw& draw = gpu->draws().at(0);
			const Renderer::backend::GeometryDesc*				geometry = gpu->geometry(draw.call.geometry);
			const std::span<const uint8_t>						bytes = gpu->bufferContents(geometry->vertices);
			std::vector<Renderer::BatchInstance>				quads(bytes.size() / sizeof(Renderer::BatchInstance));
			std::memcpy(quads.data(), bytes.data(), quads.size() * sizeof(Renderer::BatchInstance));
			return {quads.begin() + draw.call.firstInstance, quads.begin() + draw.call.firstInstance + draw.call.instanceCount};
		}

		Renderer::backend::NullBackend* gpu = nullptr;
	};

	// Walked under its clipping opener, the entry draws with no clip, after everything in the scene.
	TEST_F(UiPopupDrawTest, AnEntryDrawsLastAndUnclippedUnderAClippingOpener) {
		Context				 context(kViewport);
		const Ref<Component> panel = context.root().addChild<Component>({.size = {100.0F, 100.0F}, .overflow = Overflow::Clip});
		const Ref<Rectangle> inside = panel.addChild<Rectangle>(box("inside", 0.0F, 0.0F, 50.0F, 50.0F));
		inside.get()->openPopup<Rectangle>(box("menu", 0.0F, 0.0F, 150.0F, 200.0F), menuBelow());
		context.root().addChild<Rectangle>(box("later", 300.0F, 0.0F, 50.0F, 50.0F));
		context.layout();

		Renderer::Primitives::beginFrame();
		context.render();
		Renderer::Primitives::endFrame();

		const std::vector<Renderer::BatchInstance> quads = painted();
		ASSERT_EQ(quads.size(), 3U);
		EXPECT_EQ(quads[0].corners[0].x, 0.0F); // inside, clipped to the panel
		EXPECT_EQ(quads[0].clip, Foundation::Vec4(0.0F, 0.0F, 100.0F, 100.0F));
		EXPECT_EQ(quads[1].corners[0].x, 300.0F); // later, in the scene
		EXPECT_EQ(quads[2].corners[0].y, 54.0F);  // the menu, last, under the row
		EXPECT_EQ(quads[2].clip, Foundation::Vec4(0.0F));
	}

} // namespace ui
