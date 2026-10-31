// ui_icons: ui::Icon from the sandbox's own icons (apps/sandbox/icons/, built by salal_icons()):
// every icon at the sizes a UI uses, tinted in the theme's tones, an icon beside text in one
// glyph run, and a name the atlas doesn't have, which draws nothing.

#include "Scenes.h"

#include <application/App.h>
#include <primitives/Primitives.h>
#include <sandbox_icons/Icons.h>
#include <ui/shapes/Icon.h>
#include <ui/shapes/Text.h>
#include <ui/theme/Tokens.h>
#include <ui/theme/Variants.h>

#include <array>
#include <string>

namespace {

	constexpr const char* kSceneName = "ui_icons";

	using ui::Component;
	using ui::Icon;
	using ui::Ref;
	using ui::Text;
	namespace styles = ui::styles;

	constexpr std::array<const char*, 6> kNames{"arrow-up", "caret-down", "check", "close", "frame", "heart"};

	class UiIconsScene final : public app::Scene {
	  public:
		const char* name() const override { return kSceneName; }

		void onEnter(app::App& /*app*/) override {
			ui().icons().add(sandbox_icons::iconAtlas());
			Ref<Component> stage = root().addChild<Component>({.id = "stage", .position = {20.0F, 20.0F}, .size = {1000.0F, 660.0F}});
			stage.addChild<Text>({.id = "title", .style = styles::text::display, .text = "Icons"});
			sizes(stage);
			tones(stage);
			besideText(stage);
		}

		void render(float /*alpha*/) override { Renderer::Primitives::clear(ui().getTheme().get(ui::tokens::color::surface::base)); }

	  private:
		static void sizes(Ref<Component> stage) {
			constexpr std::array<float, 6> kSizes{12.0F, 16.0F, 24.0F, 32.0F, 48.0F, 96.0F};
			float						   y = 70.0F;
			for (const char* name : kNames) {
				stage.addChild<Text>({.position = {0.0F, y + 8.0F}, .style = styles::text::caption, .text = name});
				float x = 100.0F;
				for (const float size : kSizes) {
					stage.addChild<Icon>(
						{.id = std::string(name) + "_" + std::to_string(static_cast<int>(size)),
						 .position = {x, y},
						 .size = {size, size},
						 .name = name}
					);
					x += size + 16.0F;
				}
				y += 52.0F;
			}
		}

		void tones(Ref<Component> stage) {
			const std::array<ui::Tone, 6> kTones{
				ui::Tone::Default, ui::Tone::Accent, ui::Tone::Info, ui::Tone::Positive, ui::Tone::Warning, ui::Tone::Danger
			};
			for (std::size_t i = 0; i < kTones.size(); ++i) {
				stage.addChild<Icon>({
					.position = {static_cast<float>(i) * 44.0F, 400.0F},
					.size = {32.0F, 32.0F},
					.style = {.color = ui::toneColors(ui().getTheme(), kTones[i]).color},
					.name = kNames[i],
				});
			}
		}

		static void besideText(Ref<Component> stage) {
			Ref<Component> row = stage.addChild<Component>({.id = "inline", .position = {0.0F, 460.0F}, .size = {400.0F, 24.0F}});
			row.addChild<Icon>({.position = {0.0F, 2.0F}, .size = {20.0F, 20.0F}, .name = "check"});
			row.addChild<Text>({.position = {26.0F, 2.0F}, .style = styles::text::body, .text = "Saved, an icon beside its label"});
			stage.addChild<Text>(
				{.position = {0.0F, 500.0F}, .style = styles::text::caption, .text = "Below: \"missing\", which the atlas lacks"}
			);
			stage.addChild<Icon>({.id = "missing", .position = {0.0F, 520.0F}, .size = {32.0F, 32.0F}, .name = "missing"});
		}
	};

} // namespace

namespace sandbox {
	const SceneInfo kUiIconsScene{.name = kSceneName, .make = makeScene<UiIconsScene>};
} // namespace sandbox
