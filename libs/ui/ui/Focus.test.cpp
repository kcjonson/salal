// Focus: Tab order, keys to the focused component and bubbling, directional focus, focus groups,
// scopes, removal and fixup, players and their devices, the pointer, scrolling into view, and keyed
// children keeping focus. Headless; events go straight to a context.

#include "ui/Context.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace ui {

	namespace {

		using foundation::DeviceId;
		using foundation::DeviceKind;
		using foundation::GamepadAxis;
		using foundation::GamepadButton;
		using foundation::InputEvent;
		using foundation::Key;

		constexpr DeviceId kKeyboard{.kind = DeviceKind::Keyboard};

		DeviceId pad(std::uint8_t index) {
			return {.kind = DeviceKind::Gamepad, .index = index};
		}

		InputEvent keyDown(Key key, std::uint8_t mods = 0) {
			return {.type = InputEvent::Type::KeyDown, .device = kKeyboard, .mods = mods, .key = key};
		}

		InputEvent padDown(std::uint8_t index, GamepadButton button) {
			return {.type = InputEvent::Type::ButtonDown, .device = pad(index), .button = button};
		}

		InputEvent stick(std::uint8_t index, GamepadAxis axis, float value) {
			return {.type = InputEvent::Type::Axis, .device = pad(index), .axis = axis, .value = value};
		}

		Component::Args button(const char* id, float x, float y) {
			return {.id = id, .position = {x, y}, .size = {40.0F, 20.0F}, .focusable = true};
		}

		const char* typeName(KeyEvent::Type type) {
			switch (type) {
				case KeyEvent::Type::Down:
					return "Down";
				case KeyEvent::Type::Up:
					return "Up";
				case KeyEvent::Type::Char:
					return "Char";
				case KeyEvent::Type::Activate:
					return "Activate";
				case KeyEvent::Type::Cancel:
					return "Cancel";
				case KeyEvent::Type::Navigate:
					return "Navigate";
			}
			return "?";
		}

		class UiFocusTest : public ::testing::Test {
		  protected:
			std::string focusedId(int player = 0) {
				const Component* focused = context.focused(player).get();
				return focused != nullptr ? focused->getId() : std::string("none");
			}

			// a, b and c in a row, 100 px apart.
			void row() {
				for (const char* id : {"a", "b", "c"}) {
					root.addChild<Component>(button(id, static_cast<float>(100 * (id[0] - 'a')), 0.0F));
				}
				context.layout();
			}

			Context		   context{{800.0F, 600.0F}};
			Ref<Component> root = context.root();
		};

	} // namespace

	// --- Tab order ---

	TEST_F(UiFocusTest, TabPutsPositiveIndicesFirstSkipsWhatCantTakeFocusAndWraps) {
		root.addChild<Component>(button("a", 0.0F, 0.0F));
		root.addChild<Component>({.id = "b", .size = {40.0F, 20.0F}, .focusable = true, .tabIndex = 2});
		root.addChild<Component>({.id = "c", .size = {40.0F, 20.0F}, .focusable = true, .tabIndex = 1});
		const Ref<Component> byCode = root.addChild<Component>({.id = "byCode", .size = {40.0F, 20.0F}, .focusable = true, .tabIndex = -1});
		const Ref<Component> disabled =
			root.addChild<Component>({.id = "disabled", .enabled = false, .size = {40.0F, 20.0F}, .focusable = true});
		root.addChild<Component>({.id = "hidden", .visible = false, .size = {40.0F, 20.0F}, .focusable = true});
		context.layout();

		std::vector<std::string> order;
		for (int i = 0; i < 4; ++i) {
			EXPECT_TRUE(context.focusNext());
			order.push_back(focusedId());
		}
		EXPECT_EQ(order, (std::vector<std::string>{"c", "b", "a", "c"}));
		context.focusPrevious();
		EXPECT_EQ(focusedId(), "a");

		EXPECT_TRUE(byCode.get()->focus());
		EXPECT_EQ(focusedId(), "byCode");
		EXPECT_FALSE(disabled.get()->focus());
		EXPECT_EQ(focusedId(), "byCode");
	}

	TEST_F(UiFocusTest, BlurComesBeforeFocusAndFocusingTheFocusedIsQuiet) {
		std::vector<std::string> log;
		for (const char* id : {"a", "b"}) {
			Component::Args args = button(id, 0.0F, 0.0F);
			args.onFocus = [&](Component& component) {
				log.push_back(component.getId() + " focus");
			};
			args.onBlur = [&](Component& component) {
				log.push_back(component.getId() + " blur");
			};
			root.addChild<Component>(args);
		}
		context.layout();
		context.focusNext();
		context.focusNext();
		context.focus(context.focused());
		EXPECT_EQ(log, (std::vector<std::string>{"a focus", "a blur", "b focus"}));
		context.clearFocus();
		EXPECT_EQ(log.back(), "b blur");
		EXPECT_EQ(focusedId(), "none");
	}

	// --- Keys ---

	TEST_F(UiFocusTest, KeysGoToTheFocusedComponentThenBubbleAndTabIsTheUis) {
		std::vector<std::string> log;
		const Ref<Component>	 panel = root.addChild<Component>({
				.id = "panel",
				.size = {200.0F, 200.0F},
				.onKeyDown = [&](KeyEvent& event) { log.push_back(std::string("panel ") + typeName(event.type)); },
		});
		Component::Args			 fieldArgs = button("field", 0.0F, 0.0F);
		fieldArgs.onKeyDown = [&](KeyEvent& event) {
			log.push_back(std::string("field ") + typeName(event.type));
			if (event.key == Key::A) {
				event.consume();
			}
		};
		const Ref<Component> field = panel.addChild<Component>(fieldArgs);
		context.layout();
		field.get()->focus();

		EXPECT_TRUE(context.dispatch(keyDown(Key::A)));
		EXPECT_FALSE(context.dispatch(keyDown(Key::B)));
		EXPECT_FALSE(context.dispatch({.type = InputEvent::Type::Char, .device = kKeyboard, .codepoint = U'x'}));
		EXPECT_TRUE(context.dispatch(keyDown(Key::Tab)));
		EXPECT_FALSE(context.dispatch(keyDown(Key::Enter))); // activated, and nobody consumed it
		EXPECT_FALSE(context.dispatch(padDown(0, GamepadButton::B)));
		EXPECT_EQ(
			log,
			(std::vector<std::string>{
				"field Down",
				"field Down",
				"panel Down",
				"field Char",
				"panel Char",
				"field Down",
				"panel Down",
				"field Activate",
				"panel Activate",
				"field Cancel",
				"panel Cancel",
			})
		);
		EXPECT_EQ(context.inputMode(), InputMode::Controller);
	}

	// --- Directional focus ---

	TEST_F(UiFocusTest, ADirectionTakesTheNearestThatWayUnlessANeighbourIsSet) {
		for (int r = 0; r < 3; ++r) {
			for (int c = 0; c < 3; ++c) {
				const std::string id = "r" + std::to_string(r) + "c" + std::to_string(c);
				root.addChild<Component>(button(id.c_str(), static_cast<float>(100 * c), static_cast<float>(100 * r)));
			}
		}
		context.layout();
		EXPECT_TRUE(context.focusDirection(Direction::Down)); // nothing focused: the first stop
		EXPECT_EQ(focusedId(), "r0c0");
		context.focusDirection(Direction::Right);
		context.focusDirection(Direction::Down);
		EXPECT_EQ(focusedId(), "r1c1");
		context.focusDirection(Direction::Up);
		EXPECT_EQ(focusedId(), "r0c1");
		context.focusDirection(Direction::Right);
		EXPECT_FALSE(context.focusDirection(Direction::Right));
		EXPECT_FALSE(context.focusDirection(Direction::Up));
		EXPECT_EQ(focusedId(), "r0c2");

		Component*	   corner = context.focused().get();
		Ref<Component> farCorner;
		for (const ComponentId id : root.get()->getChildren()) {
			if (context.find(id)->getId() == "r2c0") {
				farCorner = context.find(id)->self();
			}
		}
		corner->setNeighbour(Direction::Left, farCorner);
		context.focusDirection(Direction::Left);
		EXPECT_EQ(focusedId(), "r2c0");
	}

	// before, a vertical group of m0 to m2, after below it, and side to the group's right.
	TEST_F(UiFocusTest, AFocusGroupIsOneTabStopWhoseArrowsMoveInsideIt) {
		root.addChild<Component>(button("before", 0.0F, 0.0F));
		const Ref<Component> list = root.addChild<Component>({
			.id = "list",
			.position = {0.0F, 100.0F},
			.size = {100.0F, 100.0F},
			.focusGroup = FocusGroup{.orientation = FocusGroup::Orientation::Vertical},
		});
		for (int i = 0; i < 3; ++i) {
			const std::string id = "m" + std::to_string(i);
			list.addChild<Component>(button(id.c_str(), 0.0F, static_cast<float>(30 * i)));
		}
		root.addChild<Component>(button("after", 0.0F, 300.0F));
		root.addChild<Component>(button("side", 200.0F, 130.0F));
		context.layout();

		std::vector<std::string> tabs;
		for (int i = 0; i < 4; ++i) {
			context.focusNext();
			tabs.push_back(focusedId());
		}
		EXPECT_EQ(tabs, (std::vector<std::string>{"before", "m0", "after", "side"}));

		context.focusPrevious();
		context.focusPrevious();
		EXPECT_EQ(focusedId(), "m0");
		context.focusDirection(Direction::Down);
		context.focusDirection(Direction::Down);
		EXPECT_EQ(focusedId(), "m2");
		context.focusDirection(Direction::Down); // the end, no wrap: out of the group
		EXPECT_EQ(focusedId(), "after");
		context.focusPrevious();
		EXPECT_EQ(focusedId(), "m2"); // the group's stop is its member focused last

		context.focusDirection(Direction::Right);
		EXPECT_EQ(focusedId(), "side");
		context.focusDirection(Direction::Left); // m1 is nearest, and the group sends it to m2
		EXPECT_EQ(focusedId(), "m2");

		list.get()->setFocusGroup(FocusGroup{.orientation = FocusGroup::Orientation::Vertical, .wrap = true});
		context.focusDirection(Direction::Down);
		EXPECT_EQ(focusedId(), "m0");
	}

	// --- Scopes, removal and fixup ---

	TEST_F(UiFocusTest, AScopeHoldsNavigationAndHandsFocusBackWhenItCloses) {
		const Ref<Component> outside = root.addChild<Component>(button("outside", 0.0F, 0.0F));
		const Ref<Component> dialog = root.addChild<Component>({.id = "dialog", .position = {300.0F, 0.0F}, .size = {200.0F, 200.0F}});
		dialog.addChild<Component>(button("ok", 0.0F, 0.0F));
		dialog.addChild<Component>(button("cancel", 100.0F, 0.0F));
		context.layout();
		outside.get()->focus();

		context.pushScope(dialog);
		EXPECT_EQ(focusedId(), "ok");
		context.focusNext();
		context.focusNext();
		EXPECT_EQ(focusedId(), "ok");
		EXPECT_FALSE(outside.get()->focus());
		EXPECT_FALSE(context.focusDirection(Direction::Left));
		context.popScope();
		EXPECT_EQ(focusedId(), "outside");

		// Removing the scope's root closes it too, once the layout has run.
		context.pushScope(dialog);
		root.get()->removeChild(dialog);
		EXPECT_EQ(focusedId(), "none");
		context.layout();
		EXPECT_EQ(focusedId(), "outside");
	}

	TEST_F(UiFocusTest, RemovalDropsFocusSilentlyAndLayoutMovesItOn) {
		std::vector<std::string>	log;
		std::vector<Ref<Component>> made;
		for (const char* id : {"a", "b", "c"}) {
			Component::Args args = button(id, 0.0F, 0.0F);
			args.onBlur = [&](Component& component) {
				log.push_back(component.getId() + " blur");
			};
			made.push_back(root.addChild<Component>(args));
		}
		context.layout();
		made[1].get()->focus();
		root.get()->removeChild(made[1]);
		context.layout();
		EXPECT_TRUE(log.empty()); // R9.21
		EXPECT_EQ(focusedId(), "a");

		made[0].get()->setVisible(false);
		context.layout();
		EXPECT_EQ(log, (std::vector<std::string>{"a blur"}));
		EXPECT_EQ(focusedId(), "c");
		made[2].get()->setEnabled(false);
		context.layout();
		EXPECT_EQ(focusedId(), "none");
	}

	// --- Players and devices ---

	TEST_F(UiFocusTest, EachPlayersPadMovesOnlyThatPlayersFocus) {
		row();
		const std::vector<DeviceId> first{pad(0)};
		const std::vector<DeviceId> second{pad(1)};
		context.setPlayerDevices(0, first);
		context.setPlayerDevices(1, second);
		context.dispatch(padDown(0, GamepadButton::DpadRight));
		context.dispatch(padDown(1, GamepadButton::DpadRight));
		context.dispatch(padDown(1, GamepadButton::DpadRight));
		EXPECT_EQ(focusedId(0), "a");
		EXPECT_EQ(focusedId(1), "b");
		EXPECT_TRUE(context.focused(0).get()->isFocusedBy(0));
		EXPECT_FALSE(context.focused(0).get()->isFocusedBy(1));
		EXPECT_TRUE(context.focused(1).get()->isFocusVisible());
		EXPECT_FALSE(context.dispatch(keyDown(Key::Right))); // nobody holds the keyboard
		EXPECT_EQ(focusedId(0), "a");
	}

	TEST_F(UiFocusTest, ASplitKeyboardDrivesEachPlayerThroughTheirOwnMap) {
		row();
		const std::vector<DeviceId> keyboard{kKeyboard};
		context.setPlayerDevices(0, keyboard);
		context.setPlayerDevices(1, keyboard);
		foundation::ActionMap wasd;
		const auto			  bind = [&](std::string_view action, Key key) {
			   wasd.bind(action, {.source = foundation::Binding::Source::Key, .control = static_cast<std::uint16_t>(key)});
		};
		bind(actions::kLeft, Key::A);
		bind(actions::kRight, Key::D);
		context.setNavigation(1, wasd);

		context.dispatch(keyDown(Key::Right));
		context.dispatch(keyDown(Key::D));
		context.dispatch(keyDown(Key::D));
		context.dispatch(keyDown(Key::Right));
		context.dispatch(keyDown(Key::D));
		EXPECT_EQ(focusedId(0), "b");
		EXPECT_EQ(focusedId(1), "c");
	}

	// Before any player holds a device, player 0 holds them all; then the pad is player 1's, and its
	// stick moves focus once each time it crosses half travel.
	TEST_F(UiFocusTest, APadThatArrivesLaterDrivesThePlayerItIsGivenTo) {
		row();
		context.dispatch(padDown(0, GamepadButton::DpadRight));
		EXPECT_EQ(focusedId(0), "a");

		const std::vector<DeviceId> keyboard{kKeyboard};
		const std::vector<DeviceId> arrived{pad(0)};
		context.setPlayerDevices(0, keyboard);
		context.setPlayerDevices(1, arrived);
		context.dispatch(stick(0, GamepadAxis::LeftX, 0.3F));
		EXPECT_EQ(focusedId(1), "none");
		context.dispatch(stick(0, GamepadAxis::LeftX, 0.9F));
		context.dispatch(stick(0, GamepadAxis::LeftX, 1.0F));
		EXPECT_EQ(focusedId(1), "a");
		context.dispatch(stick(0, GamepadAxis::LeftX, 0.2F));
		context.dispatch(stick(0, GamepadAxis::LeftX, 0.9F));
		EXPECT_EQ(focusedId(1), "b");
		context.dispatch(keyDown(Key::Right));
		EXPECT_EQ(focusedId(0), "b");
	}

	// --- The pointer ---

	TEST_F(UiFocusTest, APressFocusesTheNearestFocusableWithNoRingAndEmptySpaceClearsIt) {
		const Ref<Component> card = root.addChild<Component>(button("card", 0.0F, 0.0F));
		card.addChild<Component>({.id = "label", .size = {20.0F, 10.0F}, .pointerEvents = PointerEvents::Auto});
		root.addChild<Component>(button("other", 100.0F, 0.0F));
		context.layout();
		const auto press = [&](Foundation::Vec2 at) {
			context.dispatch({.type = InputEvent::Type::PointerDown, .mouseButton = foundation::MouseButton::Left, .position = at});
			context.dispatch({.type = InputEvent::Type::PointerUp, .mouseButton = foundation::MouseButton::Left, .position = at});
		};
		press({5.0F, 5.0F});
		EXPECT_EQ(focusedId(), "card");
		EXPECT_FALSE(card.get()->isFocusVisible());
		EXPECT_EQ(context.inputMode(), InputMode::Pointer);
		context.dispatch(keyDown(Key::Tab));
		EXPECT_EQ(focusedId(), "other");
		EXPECT_TRUE(context.focused().get()->isFocusVisible());
		press({500.0F, 500.0F});
		EXPECT_EQ(focusedId(), "none");
	}

	// --- Scrolling into view ---

	TEST_F(UiFocusTest, FocusByKeysScrollsItIntoView) {
		const Ref<Component> list = root.addChild<Component>({
			.id = "list",
			.size = {100.0F, 100.0F},
			.layout = {.display = Display::Flex, .flexDirection = FlexDirection::Column},
			.overflow = Overflow::Scroll,
		});
		for (int i = 0; i < 10; ++i) {
			const std::string id = "row" + std::to_string(i);
			list.addChild<Component>({.id = id, .size = {100.0F, 40.0F}, .focusable = true});
		}
		context.layout();
		for (int i = 0; i < 4; ++i) {
			context.focusDirection(Direction::Down);
		}
		context.layout();
		EXPECT_EQ(focusedId(), "row3");
		EXPECT_FLOAT_EQ(list.get()->getScroll().y, 60.0F); // row 3's bottom, 160, at the view's 100
		context.focusPrevious();
		context.focusPrevious();
		context.focusPrevious();
		context.layout();
		EXPECT_FLOAT_EQ(list.get()->getScroll().y, 0.0F);
	}

	// --- Keyed children ---

	TEST_F(UiFocusTest, ReconciledChildrenKeepTheirComponentsAndFocus) {
		const Ref<Component> list = root.addChild<Component>({.id = "list", .size = {200.0F, 200.0F}});
		int					 built = 0;
		const auto			 sync = [&](const std::vector<std::string>& keys) {
			  list.get()->reconcileChildren(
				  keys,
				  [](const std::string& key) { return key; },
				  [&](const std::string& key) {
					  ++built;
					  return list.addChild<Component>({.id = key, .size = {40.0F, 20.0F}, .focusable = true});
				  },
				  [](Component& /*child*/, const std::string& /*key*/) {}
			  );
			  context.layout();
		};
		sync({"a", "b", "c"});
		context.focusNext();
		context.focusNext();
		const Component* kept = context.focused().get();
		ASSERT_EQ(kept->getId(), "b");

		sync({"c", "b", "d"});
		EXPECT_EQ(built, 4);
		EXPECT_EQ(context.focused().get(), kept);
		EXPECT_TRUE(kept->isFocused());
		std::vector<std::string> ids;
		for (const ComponentId id : list.get()->getChildren()) {
			ids.push_back(context.find(id)->getId());
		}
		EXPECT_EQ(ids, (std::vector<std::string>{"c", "b", "d"}));
	}

} // namespace ui
