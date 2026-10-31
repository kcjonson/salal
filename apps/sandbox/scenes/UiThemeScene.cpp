// ui_theme: the theme through ui's components (docs/ui-design.md, "Theme"). ui's theme styles on
// Rectangles and Texts, which restyle in place when the theme changes, and swatches computed from the
// theme: the tone set, the state layers over a control's colour, and the focus ring. T switches the
// sandbox's tokens (apps/sandbox/tokens/) between dark and light; /api/state?what=scene has the mode.

#include "Scenes.h"

#include <application/App.h>
#include <primitives/Primitives.h>
#include <sandbox_theme/Tokens.h>
#include <ui/shapes/Rectangle.h>
#include <ui/shapes/Text.h>
#include <ui/theme/Tokens.h>
#include <ui/theme/Variants.h>

#include <array>
#include <string>
#include <utility>

namespace {

	constexpr const char* kSceneName = "ui_theme";

	using ui::Component;
	using ui::Rectangle;
	using ui::Ref;
	using ui::Text;
	namespace styles = ui::styles;

	class UiThemeScene final : public app::Scene {
	  public:
		const char* name() const override { return kSceneName; }

		void onEnter(app::App& /*app*/) override {
			applyMode();
			Ref<Component> stage = root().addChild<Component>({.id = "stage", .position = {20.0F, 20.0F}, .size = {1000.0F, 660.0F}});
			stage.addChild<Text>({.id = "title", .style = styles::text::display, .text = "Theme"});
			stage.addChild<Text>(
				{.id = "hint", .position = {0.0F, 56.0F}, .style = styles::text::caption, .text = "T switches dark and light"}
			);
			surfaces(stage);
			type(stage);
			swatches = stage.addChild<Component>({.id = "swatches", .position = {0.0F, 420.0F}, .size = {1000.0F, 240.0F}});
			buildSwatches();
		}

		bool handleEvent(const foundation::InputEvent& event) override {
			if (event.type != foundation::InputEvent::Type::KeyDown || event.repeat || event.key != foundation::Key::T) {
				return false;
			}
			light = !light;
			applyMode();
			// The styled components restyled in place; the swatches are colours read from the theme.
			if (Component* old = swatches.get()) {
				Ref<Component> stage = old->parent();
				stage.get()->removeChild(swatches);
				swatches = stage.addChild<Component>({.id = "swatches", .position = {0.0F, 420.0F}, .size = {1000.0F, 240.0F}});
				buildSwatches();
			}
			return true;
		}

		void render(float /*alpha*/) override { Renderer::Primitives::clear(ui().getTheme().get(ui::tokens::color::surface::base)); }

		std::string exportState() const override { return std::string(R"({"mode":")") + (light ? "light" : "dark") + R"("})"; }

	  private:
		void applyMode() {
			ui().setTheme(ui::Theme::load({.tokens = &sandbox_theme::tokenTable(), .inputs = {{"theme", light ? "light" : "dark"}}}).theme);
		}

		static void surfaces(Ref<Component> stage) {
			const std::array<std::pair<const char*, ui::Style>, 5> panes{{
				{"pane", styles::pane},
				{"inset", styles::inset},
				{"overlay", styles::overlay},
				{"button", styles::button},
				{"tooltip", styles::tooltip},
			}};
			for (std::size_t i = 0; i < panes.size(); ++i) {
				const float x = static_cast<float>(i) * 190.0F;
				stage
					.addChild<Rectangle>(
						{.id = panes[i].first, .position = {x, 100.0F}, .size = {170.0F, 110.0F}, .style = panes[i].second}
					)
					.addChild<Text>({.position = {12.0F, 12.0F}, .style = styles::text::secondary, .text = panes[i].first});
			}
			// A theme style extended with a field of its own.
			stage.addChild<Rectangle>({
				.id = "pane_extended",
				.position = {0.0F, 230.0F},
				.size = {170.0F, 40.0F},
				.style = styles::pane.extend({.backgroundColor = Foundation::Color(0.55F, 0.2F, 0.6F), .borderRadius = 12.0F}),
			});
		}

		static void type(Ref<Component> stage) {
			const std::array<std::pair<const char*, ui::Style>, 5> roles{{
				{"Title", styles::text::title},
				{"Body", styles::text::body},
				{"Secondary", styles::text::secondary},
				{"Caption", styles::text::caption},
				{"Mono 0123456789", styles::text::mono},
			}};

			const ui::Layout stack{.display = ui::Display::Flex, .flexDirection = ui::FlexDirection::Column, .gap = 4.0F};
			Ref<Component>	 column = stage.addChild<Component>({.position = {0.0F, 290.0F}, .layout = stack});
			for (const auto& [label, style] : roles) {
				column.addChild<Text>({.style = style, .text = label});
			}
		}

		void buildSwatches() {
			const ui::Theme& theme = ui().getTheme();
			const auto		 swatch = [&](Foundation::Vec2 at, Foundation::Color color, const char* label) {
				  swatches.addChild<Rectangle>(
					  {.position = at, .size = {64.0F, 40.0F}, .style = {.backgroundColor = color, .borderRadius = 4.0F}}
				  );
				  swatches.addChild<Text>({.position = {at.x, at.y + 46.0F}, .style = styles::text::caption, .text = label});
			};
			const std::array<std::pair<ui::Tone, const char*>, 6> tones{{
				{ui::Tone::Default, "default"},
				{ui::Tone::Accent, "accent"},
				{ui::Tone::Info, "info"},
				{ui::Tone::Positive, "positive"},
				{ui::Tone::Warning, "warning"},
				{ui::Tone::Danger, "danger"},
			}};
			for (std::size_t i = 0; i < tones.size(); ++i) {
				const ui::ToneColors colors = ui::toneColors(theme, tones[i].first);
				const float			 x = static_cast<float>(i) * 80.0F;
				swatch({x, 0.0F}, colors.color, tones[i].second);
				swatch({x, 70.0F}, colors.subtle, "subtle");
			}

			const Foundation::Color											  control = theme.get(ui::tokens::color::surface::control);
			const std::array<std::pair<ui::InteractionState, const char*>, 5> states{{
				{{}, "base"},
				{{.hovered = true}, "hover"},
				{{.hovered = true, .pressed = true}, "pressed"},
				{{.active = true}, "active"},
				{{.disabled = true}, "disabled"},
			}};
			for (std::size_t i = 0; i < states.size(); ++i) {
				swatch(
					{520.0F + static_cast<float>(i) * 80.0F, 0.0F}, ui::applyStateLayers(theme, control, states[i].first), states[i].second
				);
			}

			// The focus ring, drawn outside a control's box as its own outline.
			const ui::FocusRing ring = ui::focusRing(theme);
			swatches.addChild<Rectangle>({
				.position = {520.0F - ring.offset - ring.width, 70.0F - ring.offset - ring.width},
				.size = {64.0F + 2.0F * (ring.offset + ring.width), 40.0F + 2.0F * (ring.offset + ring.width)},
				.style =
					{.backgroundColor = Foundation::Color(0.0F, 0.0F, 0.0F, 0.0F), .borderColor = ring.color, .borderWidth = ring.width},
				.pointerEvents = ui::PointerEvents::None,
			});
			swatch({520.0F, 70.0F}, control, "focus ring");
		}

		Ref<Component> swatches;
		bool		   light = false;
	};

} // namespace

namespace sandbox {
	const SceneInfo kUiThemeScene{.name = kSceneName, .make = makeScene<UiThemeScene>};
} // namespace sandbox
