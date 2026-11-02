// ui_controls: ui's controls, after worldsim's ui-sandbox button, tab bar, text input and tree view
// scenes (apps/ui-sandbox/scenes/, a4226e17): buttons in each variant and size, with an icon and
// disabled; a checkbox, a toggle and a radio group; a segmented control and a tab bar; sliders; text
// inputs; a list of rows; and a tree. What each last did shows in a status line, and
// /api/state?what=scene has it too.

#include "Scenes.h"

#include <application/App.h>
#include <primitives/Primitives.h>
#include <sandbox_icons/Icons.h>
#include <ui/components/Button.h>
#include <ui/components/Checkbox.h>
#include <ui/components/ListRow.h>
#include <ui/components/RadioGroup.h>
#include <ui/components/SegmentedControl.h>
#include <ui/components/Slider.h>
#include <ui/components/TabBar.h>
#include <ui/components/TextInput.h>
#include <ui/components/Toggle.h>
#include <ui/components/TreeView.h>
#include <ui/theme/Tokens.h>

#include <nlohmann/json.hpp>

#include <string>

namespace {

	constexpr const char* kSceneName = "ui_controls";

	using namespace ui;

	Layout flex(FlexDirection direction, float gap) {
		return {.display = Display::Flex, .flexDirection = direction, .gap = gap, .alignItems = AlignItems::Start};
	}

	class UiControlsScene final : public app::Scene {
	  public:
		const char* name() const override { return kSceneName; }

		void onEnter(app::App& /*app*/) override {
			ui().icons().add(sandbox_icons::iconAtlas());
			Layout stage = flex(FlexDirection::Row, 32.0F);
			stage.padding = 24.0F;
			root().get()->setLayout(stage);
			Ref<Component> left = root().addChild<Component>({.layout = flex(FlexDirection::Column, 16.0F)});
			Ref<Component> right = root().addChild<Component>({.layout = flex(FlexDirection::Column, 16.0F)});
			left.addChild<Text>({.id = "title", .style = styles::text::display, .text = "Controls"});
			status = left.addChild<Text>({.id = "status", .style = styles::text::caption, .text = "Nothing yet"});
			buttons(left);
			choices(left);
			sliders(left);
			inputs(left);
			lists(right);
		}

		void render(float /*alpha*/) override { Renderer::Primitives::clear(ui().getTheme().get(tokens::color::surface::base)); }

		std::string exportState() const override { return nlohmann::json{{"last", last}}.dump(); }

	  private:
		void did(std::string what) {
			last = std::move(what);
			if (Text* shown = status.get()) {
				shown->setText(last);
			}
		}

		void buttons(Ref<Component> column) {
			Ref<Component> row = column.addChild<Component>({.layout = flex(FlexDirection::Row, 8.0F)});
			row.addChild<Button>({.id = "primary", .label = "Launch", .variant = ButtonVariant::Primary, .onPress = [this] {
									  did("pressed launch");
								  }});
			row.addChild<Button>({.id = "secondary", .label = "Options", .icon = "check", .onPress = [this] { did("pressed options"); }});
			row.addChild<Button>({.id = "ghost", .label = "Skip", .variant = ButtonVariant::Ghost, .onPress = [this] {
									  did("pressed skip");
								  }});
			row.addChild<Button>({.id = "danger", .label = "Abandon", .variant = ButtonVariant::Danger, .onPress = [this] {
									  did("pressed abandon");
								  }});
			row.addChild<Button>({.id = "disabled", .enabled = false, .label = "Locked"});
			Ref<Component> sizes = column.addChild<Component>({.layout = flex(FlexDirection::Row, 8.0F)});
			sizes.get()->setLayout({.display = Display::Flex, .gap = 8.0F, .alignItems = AlignItems::Center});
			sizes.addChild<Button>({.label = "Small", .controlSize = ControlSize::Sm});
			sizes.addChild<Button>({.label = "Medium"});
			sizes.addChild<Button>({.label = "Large", .controlSize = ControlSize::Lg});
		}

		void choices(Ref<Component> column) {
			Ref<Component> row = column.addChild<Component>({.layout = flex(FlexDirection::Row, 24.0F)});
			Ref<Component> checks = row.addChild<Component>({.layout = flex(FlexDirection::Column, 8.0F)});
			checks.addChild<Checkbox>({.id = "sound", .label = "Sound", .checked = true, .onChange = [this](bool on) {
										   did(std::string("sound ") + (on ? "on" : "off"));
									   }});
			checks.addChild<Toggle>({.id = "music", .label = "Music", .onChange = [this](bool on) {
										 did(std::string("music ") + (on ? "on" : "off"));
									 }});
			row.addChild<RadioGroup>(
				{.id = "difficulty", .options = {"Easy", "Normal", "Hard"}, .selected = 1, .onChange = [this](int index) {
					 did("difficulty " + std::to_string(index));
				 }}
			);
			column.addChild<SegmentedControl>({.id = "view", .options = {"Map", "List", "Grid"}, .onChange = [this](int index) {
												   did("view " + std::to_string(index));
											   }});
			column.addChild<TabBar>({
				.id = "tabs",
				.size = {360.0F, kAuto},
				.tabs = {{.id = "crew", .label = "Crew"}, {.id = "cargo", .label = "Cargo"}, {.id = "log", .label = "Log"}},
				.onSelect = [this](const std::string& id) { did("tab " + id); },
			});
		}

		void sliders(Ref<Component> column) {
			column.addChild<Slider>({.id = "volume", .size = {320.0F, kAuto}, .value = 0.6F, .label = "Volume", .showValue = true});
			column.addChild<Slider>({
				.id = "crew_size",
				.size = {320.0F, kAuto},
				.min = 1.0F,
				.max = 8.0F,
				.step = 1.0F,
				.value = 4.0F,
				.label = "Crew",
				.showValue = true,
				.format = [](float value) { return std::to_string(static_cast<int>(value)); },
				.onChange = [this](float value) { did("crew " + std::to_string(static_cast<int>(value))); },
			});
		}

		void inputs(Ref<Component> column) {
			column.addChild<TextInput>({
				.id = "name",
				.size = {260.0F, kAuto},
				.placeholder = "Ship name",
				.onSubmit = [this](const std::string& text) { did("named " + text); },
			});
			column.addChild<TextInput>({.id = "callsign", .size = {260.0F, kAuto}, .text = "Kestrel", .maxLength = 12});
		}

		void lists(Ref<Component> column) {
			Ref<Rectangle> list = column.addChild<Rectangle>({
				.id = "list",
				.size = {260.0F, kAuto},
				.layout = {.display = Display::Flex, .flexDirection = FlexDirection::Column, .padding = 4.0F},
				.style = styles::inset,
				.focusGroup = FocusGroup{.orientation = FocusGroup::Orientation::Vertical},
			});
			const char*	   names[] = {"Ada", "Brook", "Cass", "Dov"};
			for (int i = 0; i < 4; ++i) {
				list.addChild<ListRow>({
					.label = names[i],
					.trailing = std::to_string(20 + i * 7) + " yrs",
					.icon = i == 0 ? "check" : "",
					.selected = i == 1,
					.dim = i == 3,
					.onPress = [this, name = std::string(names[i])] { did("picked " + name); },
				});
			}
			column.addChild<TreeView>({
				.id = "tree",
				.size = {260.0F, kAuto},
				.nodes =
					{{.id = "food",
					  .label = "Food",
					  .count = 45,
					  .children = {{.id = "bread", .label = "Bread", .count = 30}, {.id = "fish", .label = "Fish", .count = 15}},
					  .expanded = true},
					 {.id = "tools", .label = "Tools", .count = 3, .children = {{.id = "saw", .label = "Saw", .count = 3}}},
					 {.id = "fuel", .label = "Fuel"}},
				.onSelect = [this](const std::string& id) { did("tree " + id); },
			});
		}

		Ref<Text>	status;
		std::string last;
	};

} // namespace

namespace sandbox {
	const SceneInfo kUiControlsScene{.name = kSceneName, .make = makeScene<UiControlsScene>};
} // namespace sandbox
