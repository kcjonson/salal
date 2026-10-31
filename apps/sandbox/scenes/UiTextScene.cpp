// ui_text: ui::Text, after worldsim's ui-sandbox text-shapes and text-wrap scenes
// (apps/ui-sandbox/scenes/TextShapesScene.cpp and TextWrapScene.cpp, a4226e17). Sizes and colours
// hugging their text; a 3 x 3 grid of the box alignments, each label the size of its box; word
// wrapping at a given width with the height hugging, explicit newlines, per-line centring; letter
// spacing and upper case. Worldsim's point-anchored alignment is gone: a Text's position is always
// its box's top-left.

#include "Scenes.h"

#include <application/App.h>
#include <graphics/Color.h>
#include <primitives/Primitives.h>
#include <ui/shapes/Rectangle.h>
#include <ui/shapes/Text.h>

#include <array>
#include <string>

namespace {

	constexpr const char* kSceneName = "ui_text";

	const Foundation::Color kBackground(0.1F, 0.1F, 0.12F, 1.0F);
	const Foundation::Color kLabel(0.7F, 0.7F, 0.7F, 1.0F);
	const Foundation::Color kBox(0.2F, 0.2F, 0.25F, 1.0F);
	const Foundation::Color kEdge(0.5F, 0.5F, 0.5F, 1.0F);

	using Foundation::Color;
	using Foundation::HorizontalAlign;
	using Foundation::VerticalAlign;
	using ui::Component;
	using ui::Rectangle;
	using ui::Ref;
	using ui::Text;

	void heading(Ref<Component> parent, const char* id, Foundation::Vec2 position, const char* text) {
		parent.addChild<Text>({.id = id, .position = position, .style = {.color = kLabel, .fontSize = 18.0F}, .text = text});
	}

	class UiTextScene final : public app::Scene {
	  public:
		const char* name() const override { return kSceneName; }

		void onEnter(app::App& /*app*/) override {
			Ref<Component> stage = root().addChild<Component>({.id = "stage", .position = {20.0F, 20.0F}, .size = {1000.0F, 660.0F}});
			stage.addChild<Text>({.id = "title", .style = {.color = Color::white(), .fontSize = 28.0F}, .text = "Text"});
			sizesAndColours(stage);
			alignment(stage);
			wrapping(stage);
			spacing(stage);
		}

		void render(float /*alpha*/) override { Renderer::Primitives::clear(kBackground); }

	  private:
		static void sizesAndColours(Ref<Component> stage) {
			heading(stage, "sizes_label", {0.0F, 50.0F}, "Font sizes");
			float y = 82.0F;
			for (const float size : {12.0F, 16.0F, 20.0F, 24.0F, 32.0F}) {
				stage.addChild<Text>({
					.position = {0.0F, y},
					.style = {.color = Color::white(), .fontSize = size},
					.text = "Text at " + std::to_string(static_cast<int>(size)) + "px",
				});
				y += size + 12.0F;
			}

			heading(stage, "colours_label", {250.0F, 50.0F}, "Colours");
			const std::array<std::pair<const char*, Color>, 4> colours{{
				{"Red text", Color::red()},
				{"Green text", Color::green()},
				{"Blue text", Color(0.35F, 0.5F, 1.0F)},
				{"Yellow text", Color::yellow()},
			}};
			for (std::size_t i = 0; i < colours.size(); ++i) {
				stage.addChild<Text>({
					.position = {250.0F, 82.0F + (static_cast<float>(i) * 30.0F)},
					.style = {.color = colours[i].second, .fontSize = 18.0F},
					.text = colours[i].first,
				});
			}
		}

		static void alignment(Ref<Component> stage) {
			heading(stage, "align_label", {440.0F, 50.0F}, "Alignment in the box");
			constexpr std::array<HorizontalAlign, 3> kAcross{HorizontalAlign::Left, HorizontalAlign::Center, HorizontalAlign::Right};
			constexpr std::array<VerticalAlign, 3>	 kDown{VerticalAlign::Top, VerticalAlign::Middle, VerticalAlign::Bottom};
			constexpr Foundation::Vec2				 kCell{170.0F, 90.0F};
			for (std::size_t row = 0; row < 3; ++row) {
				for (std::size_t column = 0; column < 3; ++column) {
					Ref<Rectangle> box = stage.addChild<Rectangle>({
						.position =
							{440.0F + (static_cast<float>(column) * (kCell.x + 15.0F)),
							 82.0F + (static_cast<float>(row) * (kCell.y + 15.0F))},
						.size = kCell,
						.style = {.backgroundColor = kBox, .borderColor = kEdge, .borderWidth = 2.0F},
					});
					box.addChild<Text>({
						.size = kCell,
						.style = {.color = Color::white(), .fontSize = 24.0F, .textAlign = kAcross[column], .verticalAlign = kDown[row]},
						.text = "TEXT",
					});
				}
			}
		}

		static void wrapping(Ref<Component> stage) {
			heading(stage, "wrap_label", {0.0F, 400.0F}, "Wrapped, height hugging");
			Ref<Rectangle> wrapBox = stage.addChild<Rectangle>(
				{.id = "wrap_box", .position = {0.0F, 432.0F}, .size = {300.0F, 170.0F}, .style = {.backgroundColor = kBox}}
			);
			wrapBox.addChild<Text>({
				.id = "wrapped",
				.position = {10.0F, 10.0F},
				.size = {280.0F, ui::kAuto},
				.style = {.color = Color::white(), .fontSize = 14.0F},
				.text = "This is a longer paragraph of text that wraps to fit the width it is given. Word-based wrapping keeps "
						"words whole and breaks only at spaces.",
				.wrap = true,
			});

			heading(stage, "newline_label", {320.0F, 400.0F}, "Explicit newlines");
			Ref<Rectangle> newlineBox = stage.addChild<Rectangle>(
				{.id = "newline_box",
				 .position = {320.0F, 432.0F},
				 .size = {260.0F, 100.0F},
				 .style = {.backgroundColor = Color(0.2F, 0.25F, 0.2F)}}
			);
			newlineBox.addChild<Text>({
				.id = "newlines",
				.position = {10.0F, 10.0F},
				.style = {.color = Color::white(), .fontSize = 14.0F},
				.text = "Line one\nLine two\nLine three with more words",
			});

			heading(stage, "centred_label", {600.0F, 400.0F}, "Each line centred");
			Ref<Rectangle> centredBox = stage.addChild<Rectangle>(
				{.id = "centred_box",
				 .position = {600.0F, 432.0F},
				 .size = {360.0F, 110.0F},
				 .style = {.backgroundColor = Color(0.25F, 0.2F, 0.25F)}}
			);
			centredBox.addChild<Text>({
				.id = "centred",
				.position = {0.0F, 10.0F},
				.size = {360.0F, ui::kAuto},
				.style = {.color = Color::white(), .fontSize = 14.0F, .textAlign = HorizontalAlign::Center},
				.text = "This wrapped text is centred. Each line is centred on its own within the width.",
				.wrap = true,
			});
		}

		static void spacing(Ref<Component> stage) {
			stage.addChild<Text>({
				.id = "spaced",
				.position = {320.0F, 560.0F},
				.style = {.color = Color::cyan(), .fontSize = 16.0F, .letterSpacing = 4.0F},
				.text = "Letter spacing",
			});
			stage.addChild<Text>({
				.id = "upper",
				.position = {320.0F, 600.0F},
				.style = {.color = Color(1.0F, 0.75F, 0.5F), .fontSize = 16.0F, .textTransform = Foundation::TextTransform::Uppercase},
				.text = "upper case at draw time",
			});
		}
	};

} // namespace

namespace sandbox {
	const SceneInfo kUiTextScene{.name = kSceneName, .make = makeScene<UiTextScene>};
} // namespace sandbox
