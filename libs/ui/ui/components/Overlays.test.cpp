// The overlay components through the dispatcher: a menu's choice, its keys and its disabled items; a
// select's value and where focus goes after; a press outside a menu, focus leaving one, and a
// context menu at the press; a dialog's modality, focus scope and the ways it closes; the hover
// tooltip's delay, what closes it, and the width it fills; toasts timing out, pressed, capped and
// brought to the front; each one fading in to full opacity, as the tree snapshot reports it, and a
// fade that leaves the style alone. Headless; nothing draws.

#include "ui/Context.h"
#include "ui/components/Button.h"
#include "ui/components/ContextMenu.h"
#include "ui/components/Dialog.h"
#include "ui/components/DropdownButton.h"
#include "ui/components/FadeIn.h"
#include "ui/components/Menu.h"
#include "ui/components/Select.h"
#include "ui/components/Toast.h"
#include "ui/components/Tooltip.h"
#include "ui/debug/TreeSnapshot.h"

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ui {

	namespace {

		using foundation::InputEvent;
		using foundation::Key;
		using foundation::MouseButton;

		// Takes all the width it's offered, as a long wrapping text does.
		class Filler : public Component {
		  public:
			using Component::Component;

		  protected:
			[[nodiscard]] std::optional<Foundation::Vec2> intrinsicSize(std::optional<float> width, float availableWidth) const override {
				return Foundation::Vec2{width.value_or(std::min(availableWidth, 1000.0F)), 20.0F};
			}
		};

		// The opacity the tree snapshot gives the first node of a type under `node`.
		std::optional<float> opacityOf(const nlohmann::json& node, std::string_view type) {
			if (node.at("type").get<std::string>() == type) {
				return node.at("opacity").get<float>();
			}
			for (const nlohmann::json& child : node.at("children")) {
				if (const std::optional<float> found = opacityOf(child, type)) {
					return found;
				}
			}
			return std::nullopt;
		}

		constexpr foundation::DeviceId kKeyboard{.kind = foundation::DeviceKind::Keyboard};

		class UiOverlayComponentsTest : public ::testing::Test {
		  protected:
			void click(Foundation::Vec2 at, MouseButton button = MouseButton::Left) {
				context.layout();
				context.dispatch({.type = InputEvent::Type::PointerDown, .mouseButton = button, .position = at});
				context.dispatch({.type = InputEvent::Type::PointerUp, .mouseButton = button, .position = at});
				context.layout();
			}

			void move(Foundation::Vec2 to) {
				context.layout();
				context.dispatch({.type = InputEvent::Type::PointerMove, .position = to});
				context.layout();
			}

			void key(Key pressed) {
				context.dispatch({.type = InputEvent::Type::KeyDown, .device = kKeyboard, .key = pressed});
				context.dispatch({.type = InputEvent::Type::KeyUp, .device = kKeyboard, .key = pressed});
				context.layout();
			}

			// Time passing, in steps the context won't clamp.
			void wait(double seconds) {
				for (; seconds > 0.0; seconds -= 0.1) {
					context.update(std::min(seconds, 0.1));
				}
				context.layout();
			}

			Foundation::Vec2 middleOf(const Component& component) {
				context.layout();
				const Foundation::Rect box = component.screenBounds();
				return {box.x + box.width * 0.5F, box.y + box.height * 0.5F};
			}

			// As /api/ui/tree reports it.
			std::optional<float> reportedOpacity(std::string_view type) {
				const nlohmann::json tree = nlohmann::json::parse(treeJson(&context, "overlays", {800.0F, 600.0F}));
				for (const nlohmann::json& top : tree.at("roots")) {
					if (const std::optional<float> found = opacityOf(top, type)) {
						return found;
					}
				}
				return std::nullopt;
			}

			Context		   context{{800.0F, 600.0F}};
			Ref<Component> root = context.root();
		};

	} // namespace

	// --- Menus ---

	TEST_F(UiOverlayComponentsTest, AMenuClosesThenRunsItsChoiceAndPassesOverDisabledItems) {
		std::vector<std::string> ran;
		Ref<Button>				 opener = root.addChild<Button>({.label = "Edit"});
		opener.get()->focus();
		Ref<Menu> menu = Menu::open(
			*opener.get(),
			{.items = {
				 {.label = "Cut", .onSelect = [&] { ran.emplace_back("cut"); }},
				 {.label = "Copy", .onSelect = [&] { ran.emplace_back("copy"); }, .enabled = false},
				 {.label = "Paste", .onSelect = [&] { ran.emplace_back(menu.get() == nullptr ? "paste, closed" : "paste, open"); }},
			 }}
		);
		context.layout();
		ASSERT_NE(menu.get(), nullptr);
		EXPECT_TRUE(menu.get()->row(0).get()->isFocused());
		key(Key::Down); // over Copy
		EXPECT_TRUE(menu.get()->row(2).get()->isFocused());
		key(Key::Down); // wraps
		EXPECT_TRUE(menu.get()->row(0).get()->isFocused());

		click(middleOf(*menu.get()->row(1).get())); // disabled: nothing, and nothing reaches the opener
		ASSERT_NE(menu.get(), nullptr);
		key(Key::Up);
		key(Key::Enter);
		EXPECT_EQ(menu.get(), nullptr);
		EXPECT_EQ(ran, (std::vector<std::string>{"paste, closed"}));
		EXPECT_TRUE(opener.get()->isFocused()); // back where it was
	}

	TEST_F(UiOverlayComponentsTest, ASelectOpensOnItsValueAndReportsOnlyAChange) {
		std::vector<std::string> changes;
		Ref<Select>				 select = root.addChild<Select>({
						 .position = {20.0F, 20.0F},
						 .options = {{.label = "Easy", .value = "easy"}, {.label = "Normal", .value = "normal"}, {.label = "Hard", .value = "hard"}},
						 .value = "normal",
						 .onChange = [&](const std::string& value) { changes.push_back(value); },
		 });
		click(middleOf(*select.get()));
		ASSERT_TRUE(select.get()->isOpen());
		const Menu* menu = select.get()->getMenu().get();
		EXPECT_TRUE(menu->row(1).get()->isFocused());
		EXPECT_EQ(menu->screenBounds().y, select.get()->screenBounds().bottom() + 4.0F);
		EXPECT_EQ(menu->screenBounds().width, select.get()->screenBounds().width);

		key(Key::Down);
		key(Key::Enter);
		EXPECT_FALSE(select.get()->isOpen());
		EXPECT_EQ(select.get()->getSelectedLabel(), "Hard");
		EXPECT_TRUE(select.get()->isFocused());

		key(Key::Enter); // opens again, on Hard
		ASSERT_TRUE(select.get()->isOpen());
		key(Key::Enter); // the same value: closes, no change
		EXPECT_FALSE(select.get()->isOpen());
		key(Key::Enter);
		key(Key::Escape); // cancel closes it, focus back on the select
		EXPECT_FALSE(select.get()->isOpen());
		EXPECT_TRUE(select.get()->isFocused());
		EXPECT_EQ(changes, (std::vector<std::string>{"hard"}));
	}

	// A press outside closes the menu and goes no further, so a press on its own button toggles it shut.
	TEST_F(UiOverlayComponentsTest, APressOutsideAMenuClosesItAndIsSpent) {
		int					presses = 0;
		Ref<Button>			other = root.addChild<Button>({.position = {400.0F, 0.0F}, .label = "Other", .onPress = [&] { ++presses; }});
		Ref<DropdownButton> actions = root.addChild<DropdownButton>({.label = "Actions", .items = {{.label = "Build"}}});
		click(middleOf(*actions.get()));
		ASSERT_TRUE(actions.get()->isOpen());
		click(middleOf(*other.get()));
		EXPECT_FALSE(actions.get()->isOpen());
		EXPECT_EQ(presses, 0);

		click(middleOf(*actions.get()));
		ASSERT_TRUE(actions.get()->isOpen());
		click(middleOf(*actions.get()));
		EXPECT_FALSE(actions.get()->isOpen());
	}

	TEST_F(UiOverlayComponentsTest, ADropdownOpensOnItsSideAndClosesWhenFocusLeaves) {
		Ref<Button>			other = root.addChild<Button>({.label = "Other"});
		Ref<DropdownButton> actions = root.addChild<DropdownButton>({
			.position = {200.0F, 400.0F},
			.label = "Build",
			.items = {{.label = "Wall"}, {.label = "Door"}},
			.side = Side::Above,
		});
		actions.get()->focus();
		key(Key::Enter);
		ASSERT_TRUE(actions.get()->isOpen());
		EXPECT_EQ(actions.get()->getMenu().get()->screenBounds().bottom(), actions.get()->screenBounds().y - 4.0F);
		EXPECT_TRUE(actions.get()->getMenu().get()->row(0).get()->isFocused());
		key(Key::Tab); // out of the menu, which closes
		EXPECT_FALSE(actions.get()->isOpen());
		EXPECT_TRUE(other.get()->isFocused());
	}

	// A secondary press elsewhere closes the open context menu and still reaches what's under it.
	TEST_F(UiOverlayComponentsTest, AContextMenuOpensAtThePressAndStaysOnScreen) {
		std::vector<Ref<ContextMenu>> menus;
		root.addChild<Rectangle>(
			{.size = {800.0F, 600.0F}, .onPointerDown = [&](PointerEvent& event) {
				 if (event.button == MouseButton::Right) {
					 menus.push_back(
						 ContextMenu::open(*event.currentTarget, event.local, {.items = {{.label = "Inspect"}, {.label = "Rename"}}})
					 );
				 }
			 }}
		);
		click({100.0F, 100.0F}, MouseButton::Right);
		ASSERT_EQ(menus.size(), 1U);
		Foundation::Rect placed = menus[0].get()->screenBounds();
		EXPECT_EQ(placed.x, 100.0F);
		EXPECT_EQ(placed.y, 100.0F);

		click({790.0F, 590.0F}, MouseButton::Right);
		ASSERT_EQ(menus.size(), 2U);
		EXPECT_EQ(menus[0].get(), nullptr);
		placed = menus[1].get()->screenBounds();
		EXPECT_EQ(placed.right(), 792.0F);	// kept 8 px inside the right edge
		EXPECT_EQ(placed.bottom(), 590.0F); // flipped above the press
		key(Key::Escape);
		EXPECT_EQ(menus[1].get(), nullptr);
	}

	// --- Dialogs ---

	TEST_F(UiOverlayComponentsTest, AModalDialogTakesThePointerAndKeepsFocusUntilItCloses) {
		int			under = 0;
		int			closed = 0;
		Ref<Button> below = root.addChild<Button>({.label = "Below", .onPress = [&] { ++under; }});
		Ref<Button> opener = root.addChild<Button>({.position = {0.0F, 100.0F}, .label = "Open"});
		opener.get()->focus();
		Ref<Dialog> dialog = Dialog::open(
			*opener.get(),
			{.title = "Abandon run?", .panelSize = {320.0F, kAuto}, .footer = true, .closeOnScrim = false, .onClose = [&] { ++closed; }}
		);
		Ref<Button> yes = dialog.get()->footer().addChild<Button>({.label = "Abandon"});
		Ref<Button> no = dialog.get()->footer().addChild<Button>({.label = "Keep playing"});
		wait(0.1);
		EXPECT_TRUE(yes.get()->isFocused()); // the scope took focus to its first stop
		key(Key::Tab);
		EXPECT_TRUE(no.get()->isFocused());
		key(Key::Tab); // the close button
		key(Key::Tab); // and round again, inside the dialog
		EXPECT_TRUE(yes.get()->isFocused());

		click(middleOf(*below.get()));
		EXPECT_EQ(under, 0);
		ASSERT_NE(dialog.get(), nullptr);
		key(Key::Enter); // the focused button takes it; the opener doesn't
		EXPECT_NE(dialog.get(), nullptr);
		key(Key::Escape);
		EXPECT_EQ(dialog.get(), nullptr);
		EXPECT_EQ(closed, 1);
		EXPECT_TRUE(opener.get()->isFocused());

		dialog = Dialog::open(*opener.get(), {.title = "Again", .onClose = [&] { ++closed; }});
		click({5.0F, 595.0F}); // the scrim
		EXPECT_EQ(dialog.get(), nullptr);
		EXPECT_EQ(closed, 2);
		click(middleOf(*below.get()));
		EXPECT_EQ(under, 1);
	}

	TEST_F(UiOverlayComponentsTest, ANonModalDialogLeavesWhatsUnderItLive) {
		int			under = 0;
		Ref<Button> below = root.addChild<Button>({.label = "Below", .onPress = [&] { ++under; }});
		Ref<Dialog> dialog = Dialog::open(*root.get(), {.title = "Colonist", .panelSize = {200.0F, 100.0F}, .modal = false});
		context.layout();
		EXPECT_EQ(dialog.get()->getOverlayKind(), OverlayKind::Panel);
		click(middleOf(*below.get()));
		EXPECT_EQ(under, 1);
		EXPECT_NE(dialog.get(), nullptr);
		dialog.get()->close();
		EXPECT_EQ(dialog.get(), nullptr);
	}

	// --- Tooltips ---

	TEST_F(UiOverlayComponentsTest, ATooltipOpensAfterTheRestAndClosesOnLeaveOrPress) {
		Ref<Button> save = root.addChild<Button>({.position = {100.0F, 100.0F}, .label = "Save"});
		// Any component as content, the tooltip measured from it.
		save.get()->setTooltip(tooltip([](Tooltip& tip) { tip.addChild<Rectangle>({.size = {100.0F, 20.0F}}); }));
		const Foundation::Vec2 over = middleOf(*save.get());
		move(over);
		wait(0.4);
		EXPECT_TRUE(context.overlays().empty());
		wait(0.15);
		ASSERT_EQ(context.overlays().size(), 1U);
		const Component* tip = context.find(context.overlays().front());
		EXPECT_EQ(tip->getOverlayKind(), OverlayKind::Tooltip);
		EXPECT_EQ(tip->paintSlot(), kTooltipSlot);
		EXPECT_EQ(tip->screenBounds().x, over.x);
		EXPECT_EQ(tip->screenBounds().y, over.y + 16.0F);
		EXPECT_EQ(tip->screenBounds().width, 116.0F); // 8 px of padding each side

		move({600.0F, 500.0F});
		wait(0.1);
		EXPECT_TRUE(context.overlays().empty());
		move(over);
		wait(0.6);
		EXPECT_EQ(context.overlays().size(), 1U);
		click(over);
		wait(1.0);
		EXPECT_TRUE(context.overlays().empty()); // shut until the pointer leaves
	}

	TEST_F(UiOverlayComponentsTest, ATooltipFillsUpToItsMaxWidthNotItsOpenersBox) {
		Ref<Button> save = root.addChild<Button>({.position = {100.0F, 100.0F}, .size = {40.0F, 24.0F}, .label = "Save"});
		Tooltip::open(*save.get(), {20.0F, 12.0F}, {}, [](Tooltip& tip) { tip.addChild<Filler>({}); });
		context.layout();
		ASSERT_EQ(context.overlays().size(), 1U);
		EXPECT_EQ(context.find(context.overlays().front())->getSize().x, 280.0F); // its maxWidth
	}

	// --- Toasts ---

	TEST_F(UiOverlayComponentsTest, ToastsTimeOutClosePressedAndKeepToTheirCap) {
		std::vector<std::string> heard;
		Ref<ToastStack>			 stack = ToastStack::open(*root.get(), {.maxToasts = 2});
		stack.get()->add({.title = "Saved", .autoDismissMs = 0.0, .onDismiss = [&] { heard.emplace_back("saved"); }});
		stack.get()->add({.title = "Low fuel", .autoDismissMs = 1000.0, .onDismiss = [&] { heard.emplace_back("fuel"); }});
		Ref<Toast> raid = stack.get()->add({
			.title = "Raid",
			.message = "Pirates are boarding the cargo bay.",
			.severity = ToastSeverity::Critical,
			.autoDismissMs = 0.0,
			.onPress = [&] { heard.emplace_back("pressed raid"); },
			.onDismiss = [&] { heard.emplace_back("raid"); },
		});
		context.layout();
		EXPECT_EQ(stack.get()->count(), 2U);
		EXPECT_EQ(heard, (std::vector<std::string>{"saved"})); // the oldest went
		const Foundation::Rect shown = raid.get()->screenBounds();
		EXPECT_EQ(shown.right(), 784.0F); // the newest nearest the bottom-right corner
		EXPECT_EQ(shown.bottom(), 584.0F);
		EXPECT_EQ(shown.width, 300.0F);

		wait(1.1);
		EXPECT_EQ(stack.get()->count(), 1U);
		click(middleOf(*raid.get()));
		EXPECT_EQ(stack.get()->count(), 0U);
		EXPECT_EQ(heard, (std::vector<std::string>{"saved", "fuel", "pressed raid", "raid"}));
	}

	TEST_F(UiOverlayComponentsTest, ANewToastComesToTheFrontOverADialogOpenedSince) {
		int				pressed = 0;
		Ref<ToastStack> stack = ToastStack::open(*root.get(), {.anchor = ToastAnchor::TopLeft});
		Ref<Dialog>		dialog = Dialog::open(*root.get(), {.title = "Inventory", .closeOnScrim = false});
		Ref<Toast>		toast = stack.get()->add({.title = "Docked", .autoDismissMs = 0.0, .onPress = [&] { ++pressed; }});
		context.layout();
		EXPECT_EQ(context.overlays(), (std::vector{dialog.componentId(), stack.componentId()}));
		EXPECT_EQ(toast.get()->screenBounds().x, 16.0F);
		EXPECT_EQ(toast.get()->screenBounds().y, 16.0F);
		click(middleOf(*toast.get())); // over the modal dialog, so it takes the press
		EXPECT_EQ(pressed, 1);
		EXPECT_NE(dialog.get(), nullptr);
	}

	// --- Entrances ---

	TEST_F(UiOverlayComponentsTest, EachOverlayFadesInToFullOpacityAndStaysThere) {
		Ref<Button> opener = root.addChild<Button>({.position = {100.0F, 100.0F}, .label = "Build"});
		opener.get()->setTooltip(tooltip([](Tooltip& tip) { tip.addChild<Rectangle>({.size = {100.0F, 20.0F}}); }));
		move(middleOf(*opener.get()));
		wait(1.0); // the rest, then the fade
		EXPECT_EQ(reportedOpacity("Tooltip"), 1.0F);

		Menu::open(*opener.get(), {.items = {{.label = "Wall"}}});
		context.layout();
		EXPECT_EQ(reportedOpacity("Menu"), 0.0F);
		wait(0.5);
		EXPECT_EQ(reportedOpacity("Menu"), 1.0F);

		Dialog::open(*root.get(), {.title = "Colonist", .panelSize = {200.0F, 100.0F}, .modal = false});
		Ref<ToastStack> stack = ToastStack::open(*root.get(), {});
		stack.get()->add({.title = "Saved", .autoDismissMs = 0.0});
		context.layout();
		EXPECT_EQ(reportedOpacity("Dialog"), 0.0F);
		EXPECT_EQ(reportedOpacity("Toast"), 0.0F);
		wait(0.5);
		EXPECT_EQ(reportedOpacity("Dialog"), 1.0F);
		EXPECT_EQ(reportedOpacity("Toast"), 1.0F);
		wait(1.0);
		EXPECT_EQ(reportedOpacity("Dialog"), 1.0F);
		EXPECT_EQ(reportedOpacity("Toast"), 1.0F);
	}

	TEST_F(UiOverlayComponentsTest, AFadeEndsAtTheStylesOpacityAndLeavesTheStyleAlone) {
		Ref<Rectangle>	   box = root.addChild<Rectangle>({.size = {10.0F, 10.0F}, .style = {.opacity = 0.5F}});
		const Tween<float> appear = fadeIn(*box.get());
		EXPECT_EQ(reportedOpacity("Rectangle"), 0.0F);
		wait(0.05);
		const float halfway = reportedOpacity("Rectangle").value_or(0.0F);
		EXPECT_GT(halfway, 0.0F);
		EXPECT_LT(halfway, 0.5F);
		EXPECT_EQ(box.get()->getDeclaredStyle().opacity, 0.5F);
		wait(0.5);
		EXPECT_EQ(reportedOpacity("Rectangle"), 0.5F);
		EXPECT_EQ(box.get()->getStyle().opacity, 0.5F);
	}

} // namespace ui
