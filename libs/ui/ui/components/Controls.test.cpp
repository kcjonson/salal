// The controls through the dispatcher, where they hold logic: what a press, a key or a drag does to
// each, and TextEditor's editing on its own. Headless; nothing draws.

#include "ui/Context.h"
#include "ui/components/Button.h"
#include "ui/components/Checkbox.h"
#include "ui/components/RadioGroup.h"
#include "ui/components/SegmentedControl.h"
#include "ui/components/Slider.h"
#include "ui/components/TabBar.h"
#include "ui/components/TextEditor.h"
#include "ui/components/TextInput.h"
#include "ui/components/Toggle.h"
#include "ui/components/TreeView.h"

#include <gtest/gtest.h>

namespace ui {

	namespace {

		using foundation::InputEvent;
		using foundation::Key;

		constexpr foundation::DeviceId kKeyboard{.kind = foundation::DeviceKind::Keyboard};

		class UiControlsTest : public ::testing::Test {
		  protected:
			// A press and a release at a point in the viewport.
			void click(Foundation::Vec2 at) {
				context.layout();
				context.dispatch({.type = InputEvent::Type::PointerDown, .mouseButton = foundation::MouseButton::Left, .position = at});
				context.dispatch({.type = InputEvent::Type::PointerUp, .mouseButton = foundation::MouseButton::Left, .position = at});
				context.layout();
			}

			void key(Key pressed, std::uint8_t mods = 0) {
				context.dispatch({.type = InputEvent::Type::KeyDown, .device = kKeyboard, .mods = mods, .key = pressed});
				context.dispatch({.type = InputEvent::Type::KeyUp, .device = kKeyboard, .mods = mods, .key = pressed});
				context.layout();
			}

			void type(std::string_view text) {
				for (const char c : text) {
					context.dispatch({.type = InputEvent::Type::Char, .device = kKeyboard, .codepoint = static_cast<char32_t>(c)});
				}
				context.layout();
			}

			// The middle of a component's box on screen.
			Foundation::Vec2 middleOf(const Component& component) {
				context.layout();
				const Foundation::Rect box = component.screenBounds();
				return {box.x + box.width * 0.5F, box.y + box.height * 0.5F};
			}

			Context		   context{{800.0F, 600.0F}};
			Ref<Component> root = context.root();
		};

	} // namespace

	TEST_F(UiControlsTest, AButtonPressesByPointerAndByKeyButNotDisabled) {
		int			presses = 0;
		Ref<Button> button = root.addChild<Button>({.label = "Go", .onPress = [&] { ++presses; }});
		click(middleOf(*button.get()));
		EXPECT_EQ(presses, 1);
		ASSERT_TRUE(button.get()->isFocused()); // the press took focus
		key(Key::Enter);
		key(Key::Space);
		EXPECT_EQ(presses, 3);
		button.get()->setEnabled(false);
		click(middleOf(*button.get()));
		EXPECT_EQ(presses, 3);
	}

	TEST_F(UiControlsTest, ACheckboxAndAToggleFlipOnAPress) {
		std::vector<bool> changes;
		Ref<Checkbox>	  box = root.addChild<Checkbox>({.label = "Sound", .onChange = [&](bool on) { changes.push_back(on); }});
		Ref<Toggle> toggle = root.addChild<Toggle>({.position = {0.0F, 40.0F}, .label = "Music", .on = true, .onChange = [&](bool on) {
														changes.push_back(on);
													}});
		click(middleOf(*box.get()));
		click(middleOf(*toggle.get()));
		EXPECT_TRUE(box.get()->isChecked());
		EXPECT_FALSE(toggle.get()->isOn());
		EXPECT_EQ(changes, (std::vector<bool>{true, false}));
	}

	TEST_F(UiControlsTest, ExclusiveChoicesSelectOneAndReportOnlyAChange) {
		std::vector<int>		 radios;
		std::vector<int>		 segments;
		std::vector<std::string> tabs;
		Ref<RadioGroup>			 group =
			root.addChild<RadioGroup>({.options = {"Easy", "Hard"}, .onChange = [&](int index) { radios.push_back(index); }});
		Ref<SegmentedControl> segmented = root.addChild<SegmentedControl>(
			{.position = {0.0F, 100.0F}, .size = {200.0F, kAuto}, .options = {"A", "B"}, .onChange = [&](int index) {
				 segments.push_back(index);
			 }}
		);
		Ref<TabBar> bar = root.addChild<TabBar>({
			.position = {0.0F, 200.0F},
			.tabs = {{.id = "crew", .label = "Crew"}, {.id = "cargo", .label = "Cargo"}},
			.onSelect = [&](const std::string& id) { tabs.push_back(id); },
		});
		context.layout();
		const auto childOf = [&](const Component& parent, std::size_t index) -> const Component& {
			return *context.find(parent.getChildren().at(index));
		};
		click(middleOf(childOf(*group.get(), 1)));
		click(middleOf(childOf(*group.get(), 1))); // already selected: no change
		click(middleOf(childOf(*segmented.get(), 1)));
		click(middleOf(childOf(*bar.get(), 2))); // after the bar's hairline
		EXPECT_EQ(group.get()->getSelected(), 1);
		EXPECT_EQ(radios, (std::vector<int>{1}));
		EXPECT_EQ(segments, (std::vector<int>{1}));
		EXPECT_EQ(tabs, (std::vector<std::string>{"cargo"}));
		EXPECT_EQ(bar.get()->getSelectedId(), "cargo");
	}

	TEST_F(UiControlsTest, ASliderFollowsADragSnapsAndStepsByKey) {
		std::vector<float> values;
		Ref<Slider>		   slider = root.addChild<Slider>(
			   {.size = {200.0F, 20.0F}, .min = 0.0F, .max = 10.0F, .step = 1.0F, .value = 2.0F, .onChange = [&](float v) {
					values.push_back(v);
				}}
		   );
		context.layout();
		const Foundation::Rect box = slider.get()->screenBounds();
		context.dispatch(
			{.type = InputEvent::Type::PointerDown, .mouseButton = foundation::MouseButton::Left, .position = {box.x + 150.0F, 10.0F}}
		);
		context.dispatch({.type = InputEvent::Type::PointerMove, .position = {box.x + 400.0F, 10.0F}}); // captured, past the end
		context.dispatch(
			{.type = InputEvent::Type::PointerUp, .mouseButton = foundation::MouseButton::Left, .position = {box.x + 400.0F, 10.0F}}
		);
		EXPECT_FLOAT_EQ(slider.get()->getValue(), 10.0F);
		key(Key::Left);
		EXPECT_FLOAT_EQ(slider.get()->getValue(), 9.0F);
		EXPECT_EQ(values, (std::vector<float>{8.0F, 10.0F, 9.0F})); // 150 of 200 px is 7.5, snapped to 8
		slider.get()->setValue(4.4F);
		EXPECT_FLOAT_EQ(slider.get()->getValue(), 4.0F);
	}

	TEST_F(UiControlsTest, ATextInputTypesEditsCopiesPastesAndSubmits) {
		std::string	   clipboard = "pasted";
		std::string	   submitted;
		int			   changes = 0;
		Ref<TextInput> input = root.addChild<TextInput>({
			.placeholder = "Name",
			.onChange = [&](const std::string&) { ++changes; },
			.onSubmit = [&](const std::string& text) { submitted = text; },
		});
		context.setClipboard({.read = [&] { return clipboard; }, .write = [&](const std::string& text) { clipboard = text; }});
		click(middleOf(*input.get()));
		type("hello");
		key(Key::Backspace);
		EXPECT_EQ(input.get()->getText(), "hell");
		key(Key::A, foundation::ModControl);
		key(Key::C, foundation::ModControl);
		EXPECT_EQ(clipboard, "hell");
		key(Key::End);
		key(Key::V, foundation::ModControl);
		key(Key::V, foundation::ModControl);
		EXPECT_EQ(input.get()->getText(), "hellhellhell");
		type(" x"); // Space types a space rather than activating
		key(Key::Enter);
		EXPECT_EQ(submitted, "hellhellhell x");
		EXPECT_EQ(changes, 5 + 1 + 2 + 2);
	}

	TEST_F(UiControlsTest, ATreeOpensByPressAndByKeyAndKeepsItsRows) {
		std::vector<std::string> selected;
		Ref<TreeView>			 tree = root.addChild<TreeView>({
					   .size = {200.0F, kAuto},
					   .nodes =
				   {{.id = "food", .label = "Food", .children = {{.id = "bread", .label = "Bread"}, {.id = "fish", .label = "Fish"}}},
							{.id = "tools", .label = "Tools"}},
					   .onSelect = [&](const std::string& id) { selected.push_back(id); },
		   });
		context.layout();
		EXPECT_EQ(tree.get()->getChildren().size(), 2U);
		const ComponentId toolsRow = tree.get()->getChildren().back();
		click(middleOf(*context.find(tree.get()->getChildren().front())));
		ASSERT_EQ(tree.get()->getChildren().size(), 4U);
		EXPECT_EQ(tree.get()->getChildren().back(), toolsRow); // kept across the rebuild
		key(Key::Left);										   // the focused branch closes
		EXPECT_EQ(tree.get()->getChildren().size(), 2U);
		key(Key::Right);
		EXPECT_EQ(tree.get()->getChildren().size(), 4U);
		EXPECT_EQ(selected, (std::vector<std::string>{"food"}));
	}

	TEST(UiTextEditor, EditsByCharacterWordAndSelection) {
		TextEditor editor("caf\xC3\xA9 bar"); // café bar
		editor.left(false, true);			  // before "bar"
		editor.left(false, false);
		EXPECT_TRUE(editor.backspace()); // the é, both its bytes
		EXPECT_EQ(editor.text(), "caf bar");
		editor.home(false);
		editor.right(true, true); // selects "caf"
		EXPECT_EQ(editor.selectedText(), "caf");
		EXPECT_TRUE(editor.insert("dog"));
		EXPECT_EQ(editor.text(), "dog bar");
		std::string removed;
		editor.selectAll();
		EXPECT_TRUE(editor.cut(removed));
		EXPECT_EQ(removed, "dog bar");
		EXPECT_FALSE(editor.backspace());

		TextEditor limited("", 3);
		limited.insert("abcdef");
		EXPECT_EQ(limited.text(), "abc");
		limited.moveTo(1, false);
		limited.moveTo(3, true);
		EXPECT_TRUE(limited.insert("Z"));
		EXPECT_EQ(limited.text(), "aZ");
	}

} // namespace ui
