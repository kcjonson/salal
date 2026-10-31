// ui_display: ui's display components, after worldsim's ui-sandbox Salvage scene (apps/ui-sandbox/
// scenes/SalvageScene.cpp, a4226e17) less worldsim's own: badges in every tone, with and without a
// dot; stats at each control size; key caps in a shortcut legend; dividers bare and labelled;
// progress bars with a header, inline, and on Auto at a spread of values; and a panel with a title,
// an action and a body.

#include "Scenes.h"

#include <application/App.h>
#include <primitives/Primitives.h>
#include <ui/components/Badge.h>
#include <ui/components/Divider.h>
#include <ui/components/KeyCap.h>
#include <ui/components/Panel.h>
#include <ui/components/ProgressBar.h>
#include <ui/components/Stat.h>
#include <ui/theme/Tokens.h>

#include <array>
#include <string>

namespace {

	constexpr const char* kSceneName = "ui_display";

	using namespace ui;

	Layout flex(FlexDirection direction, float gap) {
		return {.display = Display::Flex, .flexDirection = direction, .gap = gap, .alignItems = AlignItems::Start};
	}

	class UiDisplayScene final : public app::Scene {
	  public:
		const char* name() const override { return kSceneName; }

		void onEnter(app::App& /*app*/) override {
			Layout stage = flex(FlexDirection::Column, 20.0F);
			stage.padding = 24.0F;
			root().get()->setLayout(stage);
			root().addChild<Text>({.id = "title", .style = styles::text::display, .text = "Display"});
			badges(root().addChild<Component>({.id = "badges", .layout = flex(FlexDirection::Row, 8.0F)}));
			stats(root().addChild<Component>({.id = "stats", .layout = flex(FlexDirection::Row, 32.0F)}));
			keys(root().addChild<Component>({.id = "keys", .layout = flex(FlexDirection::Row, 6.0F)}));
			Ref<Component> lower = root().addChild<Component>({.id = "lower", .layout = flex(FlexDirection::Row, 32.0F)});
			bars(lower.addChild<Component>({.id = "bars", .size = {320.0F, kAuto}, .layout = flex(FlexDirection::Column, 14.0F)}));
			panel(lower);
		}

		void render(float /*alpha*/) override { Renderer::Primitives::clear(ui().getTheme().get(tokens::color::surface::base)); }

	  private:
		static void badges(Ref<Component> row) {
			constexpr std::array<std::pair<Tone, const char*>, 6> kTones{{
				{Tone::Default, "default"},
				{Tone::Accent, "accent"},
				{Tone::Info, "info"},
				{Tone::Positive, "online"},
				{Tone::Warning, "low"},
				{Tone::Danger, "critical"},
			}};
			for (const auto& [tone, label] : kTones) {
				row.addChild<Badge>({.label = label, .tone = tone, .dot = tone != Tone::Default});
			}
		}

		static void stats(Ref<Component> row) {
			row.addChild<Stat>({.label = "Crew", .value = "12", .controlSize = ControlSize::Sm});
			row.addChild<Stat>({.label = "Power", .value = "84", .unit = "%", .tone = Tone::Positive});
			row.addChild<Stat>({.label = "Hull", .value = "1,240", .unit = "hp", .tone = Tone::Warning, .controlSize = ControlSize::Lg});
		}

		static void keys(Ref<Component> row) {
			for (const char* key : {"W", "A", "S", "D", "Shift", "Esc"}) {
				row.addChild<KeyCap>({.label = key});
			}
			row.addChild<Text>({.style = styles::text::caption, .text = "move, run, menu"});
		}

		static void bars(Ref<Component> column) {
			column.addChild<Divider>({});
			column.addChild<ProgressBar>({.value = 0.85F, .tone = Tone::Info, .label = "Power", .valueText = "85%"});
			column.addChild<ProgressBar>({.value = 0.4F, .label = "Oxygen", .valueText = "40%", .controlSize = ControlSize::Sm});
			column.addChild<ProgressBar>(
				{.value = 0.62F, .tone = Tone::Accent, .label = "Cargo", .valueText = "62 / 100", .inlineLabel = true}
			);
			column.addChild<Divider>({.label = "Auto by value"});
			for (const float value : {0.1F, 0.35F, 0.9F}) {
				column.addChild<ProgressBar>({.value = value});
			}
		}

		static void panel(Ref<Component> row) {
			Ref<Panel> crew = row.addChild<Panel>({.id = "panel", .size = {300.0F, 220.0F}, .title = "Crew", .actions = true});
			crew.get()->actions().addChild<Badge>({.label = "3 idle", .tone = Tone::Warning});
			Ref<Component> body = crew.get()->body();
			body.addChild<Stat>({.label = "On shift", .value = "9", .controlSize = ControlSize::Sm});
			body.addChild<Divider>({.label = "Morale"});
			body.addChild<ProgressBar>({.size = {260.0F, kAuto}, .value = 0.7F, .valueText = "70%"});
		}
	};

} // namespace

namespace sandbox {
	const SceneInfo kUiDisplayScene{.name = kSceneName, .make = makeScene<UiDisplayScene>};
} // namespace sandbox
