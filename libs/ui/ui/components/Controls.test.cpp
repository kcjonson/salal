// The controls through the dispatcher, where they hold logic: what a press, a key or a drag does to
// each, and TextEditor's editing on its own; then a theme's text transform reaching the labels the
// controls build from their styles, measured on the null backend. Headless; nothing draws.

#include "ui/Context.h"
#include "ui/components/Button.h"
#include "ui/components/Checkbox.h"
#include "ui/components/RadioGroup.h"
#include "ui/components/SegmentedControl.h"
#include "ui/components/Select.h"
#include "ui/components/Slider.h"
#include "ui/components/TabBar.h"
#include "ui/components/TextEditor.h"
#include "ui/components/TextInput.h"
#include "ui/components/Toggle.h"
#include "ui/components/TreeView.h"
#include "ui/debug/TreeSnapshot.h"

#include "BaseShader.h"
#include "backend/NullBackend.h"
#include "font/FontRenderer.h"
#include "primitives/Primitives.h"

#include <theme_test/Tokens.h>

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <memory>
#include <random>
#include <vector>

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

			// The root's children as the tree snapshot reports them.
			nlohmann::json snapshot() {
				context.layout();
				return nlohmann::json::parse(treeJson(&context, "", {800.0F, 600.0F}))["roots"][0]["children"];
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
		const nlohmann::json nodes = snapshot();
		EXPECT_EQ(nodes[0]["value"], true);
		EXPECT_EQ(nodes[0]["state"]["selected"], true);
		EXPECT_EQ(nodes[1]["value"], false);
		EXPECT_EQ(nodes[1]["state"]["selected"], false);
	}

	// The knob eases over motion.duration.fast, 100 ms in ui's tokens, and snaps under reduced motion.
	TEST_F(UiControlsTest, AToggleKnobSlidesThroughTheAnimator) {
		Ref<Toggle> toggle = root.addChild<Toggle>({.label = "Music"});
		context.layout();
		const Component& track = *context.find(toggle.get()->getChildren().front());
		const Component& knob = *context.find(track.getChildren().front());
		click(middleOf(*toggle.get()));
		EXPECT_FLOAT_EQ(knob.getPosition().x, 2.0F); // no time has passed
		context.update(0.05);
		EXPECT_GT(knob.getPosition().x, 2.0F);
		EXPECT_LT(knob.getPosition().x, 16.0F);
		context.update(1.0);
		EXPECT_FLOAT_EQ(knob.getPosition().x, 16.0F);

		context.setReducedMotion(true);
		click(middleOf(*toggle.get()));
		context.update(0.016);
		EXPECT_FLOAT_EQ(knob.getPosition().x, 2.0F);
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
		const nlohmann::json nodes = snapshot();
		EXPECT_EQ(nodes[0]["value"], 1);
		EXPECT_EQ(nodes[1]["value"], 1);
		EXPECT_EQ(nodes[1]["children"][1]["state"]["active"], true);
		EXPECT_EQ(nodes[2]["value"], "cargo");
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

	TEST(UiTextEditor, ACaretNeverSitsInsideACharacter) {
		// a, é, € and 😀, one to four bytes. A pointer landing on any byte puts the caret at the start
		// of the character holding it, and the arrows, backspace and delete take a character at a time.
		TextEditor					   editor("a\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80");
		const std::vector<std::size_t> boundaries{0, 1, 3, 6, 10};
		for (std::size_t offset = 0; offset <= editor.text().size(); ++offset) {
			editor.moveTo(offset, false);
			EXPECT_EQ(editor.caret(), *(std::ranges::upper_bound(boundaries, offset) - 1)) << "offset " << offset;
		}
		editor.home(false);
		for (std::size_t i = 1; i < boundaries.size(); ++i) {
			editor.right(false, false);
			EXPECT_EQ(editor.caret(), boundaries[i]);
		}
		editor.moveTo(4, true); // a drag into the € selects from its start
		EXPECT_EQ(editor.selectedText(), "\xE2\x82\xAC\xF0\x9F\x98\x80");
		editor.end(false);
		EXPECT_TRUE(editor.backspace());
		EXPECT_EQ(editor.text(), "a\xC3\xA9\xE2\x82\xAC");
		editor.home(false);
		editor.right(false, false);
		EXPECT_TRUE(editor.deleteForward());
		EXPECT_EQ(editor.text(), "a\xE2\x82\xAC");

		// Ill-formed bytes, set or pasted, are kept as U+FFFD, so no edit joins them into a character
		// around the caret: deleting the A from E2 82 A AC would otherwise leave it inside a €.
		const std::string replacement = "\xEF\xBF\xBD";
		TextEditor		  joined(
			   "\xE2\x82"
				   "A\xAC"
		   );
		EXPECT_EQ(joined.text(), replacement + "A" + replacement);
		joined.moveTo(4, false);
		EXPECT_TRUE(joined.backspace());
		EXPECT_EQ(joined.text(), replacement + replacement);
		EXPECT_EQ(joined.caret(), 3U);
		EXPECT_TRUE(joined.insert("\xF0\x9F")); // a paste of four bytes cut short, between the two
		EXPECT_EQ(joined.text(), replacement + replacement + replacement);
		EXPECT_EQ(joined.caret(), 6U);
	}

	// --- Themed labels ---

	namespace {

		constexpr Renderer::FontFamily kBody{0};

		// Advances only, capitals wider than small letters, so a measured width says which were measured.
		constexpr const char* kAtlasJson = R"({
			"atlas": { "type": "msdf", "distanceRange": 8, "size": 32, "width": 1, "height": 1, "yOrigin": "bottom" },
			"metrics": { "emSize": 1, "ascender": 0.9, "descender": -0.25, "lineHeight": 1.2 },
			"glyphs": [
				{ "unicode": 32, "advance": 0.3 }, { "unicode": 63, "advance": 0.5 },
				{ "unicode": 71, "advance": 0.8 }, { "unicode": 79, "advance": 0.8 },
				{ "unicode": 103, "advance": 0.5 }, { "unicode": 111, "advance": 0.5 }
			],
			"kerning": []
		})";

		// stbi_load needs a real image; the null backend keeps none of its pixels.
		constexpr unsigned char kOneTexelPng[] = {
			0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00,
			0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x02, 0x00, 0x00, 0x00, 0x90, 0x77, 0x53, 0xde, 0x00, 0x00, 0x00,
			0x0c, 0x49, 0x44, 0x41, 0x54, 0x78, 0xda, 0x63, 0x60, 0x60, 0x60, 0x00, 0x00, 0x00, 0x04, 0x00, 0x01, 0xc8,
			0xea, 0xeb, 0xf9, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
		};

		// The test theme, which puts capitals on ui's button, tab, segment and select, over a font on the
		// null backend so labels measure.
		class UiThemedLabelsTest : public UiControlsTest {
		  protected:
			void SetUp() override {
				Renderer::Primitives::init(std::make_unique<Renderer::backend::NullBackend>(), base_shader::source());
				std::filesystem::create_directories(folder);
				const Renderer::FontAtlasSource body{
					.family = kBody, .pngPath = (folder / "body.png").string(), .jsonPath = (folder / "body.json").string()
				};
				std::ofstream(body.jsonPath, std::ios::binary) << kAtlasJson;
				std::ofstream(body.pngPath, std::ios::binary)
					.write(reinterpret_cast<const char*>(kOneTexelPng), static_cast<std::streamsize>(sizeof(kOneTexelPng)));
				fonts = std::make_unique<Renderer::FontRenderer>();
				ASSERT_TRUE(fonts->Initialize(std::array{body}, kBody));
				Renderer::Primitives::setFontRenderer(fonts.get());
				ThemeLoad load = Theme::load({.tokens = &theme_test::tokenTable()});
				ASSERT_TRUE(load.ok());
				context.setTheme(std::move(load.theme));
			}

			void TearDown() override {
				Renderer::Primitives::setFontRenderer(nullptr);
				fonts.reset(); // its atlas goes while the backend is there
				Renderer::Primitives::shutdown();
				std::error_code ignored;
				std::filesystem::remove_all(folder, ignored);
			}

			// The first Text under a control, depth first.
			const Text* labelOf(const Component& control) const {
				for (const ComponentId id : control.getChildren()) {
					const Component* child = context.find(id);
					if (const auto* text = dynamic_cast<const Text*>(child)) {
						return text;
					}
					if (const Text* inner = child != nullptr ? labelOf(*child) : nullptr) {
						return inner;
					}
				}
				return nullptr;
			}

			std::filesystem::path folder =
				std::filesystem::temp_directory_path() / ("salal-ui-labels-" + std::to_string(std::random_device{}()));
			std::unique_ptr<Renderer::FontRenderer> fonts;
		};

	} // namespace

	TEST_F(UiThemedLabelsTest, AThemesTextTransformReachesEachControlsLabelUnlessCodeSetsOne) {
		const Ref<Button> themed = root.addChild<Button>({.label = "go"});
		const Ref<Button> plain =
			root.addChild<Button>({.position = {0.0F, 40.0F}, .style = {.textTransform = Foundation::TextTransform::None}, .label = "go"});
		const Ref<TabBar>			bar = root.addChild<TabBar>({.position = {0.0F, 80.0F}, .tabs = {{.id = "crew", .label = "Crew"}}});
		const Ref<SegmentedControl> segmented = root.addChild<SegmentedControl>({.position = {0.0F, 120.0F}, .options = {"A", "B"}});
		const Ref<Select>			select =
			root.addChild<Select>({.position = {0.0F, 160.0F}, .options = {{.label = "go", .value = "go"}}, .value = "go"});
		const std::array<const Component*, 4> themedControls{themed.get(), bar.get(), segmented.get(), select.get()};
		for (const Component* control : themedControls) {
			const Text* label = labelOf(*control);
			ASSERT_NE(label, nullptr) << control->typeName();
			EXPECT_EQ(label->getStyle().textTransform, Foundation::TextTransform::Uppercase) << control->typeName();
		}
		EXPECT_EQ(labelOf(*plain.get())->getStyle().textTransform, Foundation::TextTransform::None);

		// Each button's label measures what it draws, as the tree snapshot reports it.
		const Style&			   labelStyle = labelOf(*themed.get())->getStyle();
		const float				   scale = labelStyle.fontSize.value_or(16.0F) / 16.0F;
		const Renderer::FontFamily family = labelStyle.fontFamily.value_or(Renderer::FontFamily::Default);
		const float				   capitals = fonts->MeasureText("GO", scale, family).x;
		const float				   smalls = fonts->MeasureText("go", scale, family).x;
		ASSERT_GT(capitals, smalls);
		const nlohmann::json nodes = snapshot();
		const auto			 measuredLabel = [](const nlohmann::json& button) {
			  for (const nlohmann::json& child : button["children"]) {
				  if (child["type"] == "Text") {
					  return child["text"]["measured"]["w"].get<float>();
				  }
			  }
			  return -1.0F;
		};
		EXPECT_FLOAT_EQ(measuredLabel(nodes[0]), capitals);
		EXPECT_FLOAT_EQ(measuredLabel(nodes[1]), smalls);

		// A theme without it takes it off again.
		context.setTheme(Theme::load({}).theme);
		for (const Component* control : themedControls) {
			EXPECT_FALSE(labelOf(*control)->getStyle().textTransform.has_value()) << control->typeName();
		}
		EXPECT_EQ(labelOf(*plain.get())->getStyle().textTransform, Foundation::TextTransform::None);
	}

} // namespace ui
