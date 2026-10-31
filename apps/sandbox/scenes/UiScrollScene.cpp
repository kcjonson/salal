// ui_scroll: Overflow::Scroll, after worldsim's ui-sandbox scroll scene. A list of forty rows that
// light up under the pointer, so hover follows rows scrolled under a still pointer; a scroller inside
// another, so the wheel stays with the inner one to its end and only a new sequence moves the outer;
// and a long wrapped text. Each has a thumb to drag and a track that pages.

#include "Scenes.h"

#include <application/App.h>
#include <graphics/Color.h>
#include <primitives/Primitives.h>
#include <ui/shapes/Rectangle.h>
#include <ui/shapes/Text.h>

#include <string>

namespace {

	constexpr const char* kSceneName = "ui_scroll";

	const Foundation::Color kBackground(0.1F, 0.1F, 0.12F, 1.0F);
	const Foundation::Color kFrame(0.17F, 0.17F, 0.22F, 1.0F);
	const Foundation::Color kRow(0.24F, 0.26F, 0.32F, 1.0F);
	const Foundation::Color kRowHot(0.35F, 0.5F, 0.75F, 1.0F);
	const Foundation::Color kLabel(0.85F, 0.85F, 0.6F, 1.0F);

	using ui::Component;
	using ui::Rectangle;
	using ui::Ref;
	using ui::Text;

	ui::Layout column(float gap, ui::Edges padding = {}) {
		return {.display = ui::Display::Flex, .flexDirection = ui::FlexDirection::Column, .gap = gap, .padding = padding};
	}

	// A row that lights while the pointer is over it.
	void hotRow(Ref<Component> list, const std::string& label, float height) {
		Ref<Rectangle> row = list.addChild<Rectangle>({
			.size = {ui::kAuto, height},
			.style = {.backgroundColor = kRow, .borderRadius = 4.0F},
			.onPointerEnter =
				[](ui::PointerEvent& event) { event.currentTarget->setStyle({.backgroundColor = kRowHot, .borderRadius = 4.0F}); },
			.onPointerLeave =
				[](ui::PointerEvent& event) { event.currentTarget->setStyle({.backgroundColor = kRow, .borderRadius = 4.0F}); },
		});
		row.addChild<Text>({.position = {10.0F, (height - 16.0F) * 0.5F}, .style = {.fontSize = 13.0F}, .text = label});
	}

	class UiScrollScene final : public app::Scene {
	  public:
		const char* name() const override { return kSceneName; }

		void onEnter(app::App& /*app*/) override {
			ui::Layout stage{
				.display = ui::Display::Flex,
				.flexDirection = ui::FlexDirection::Row,
				.gap = 24.0F,
				.padding = 24.0F,
				.alignItems = ui::AlignItems::Start
			};
			root().get()->setLayout(stage);

			Ref<Component> list = captioned("Forty rows, hover follows the scroll", {260.0F, 360.0F});
			for (int i = 0; i < 40; ++i) {
				hotRow(list, "Row " + std::to_string(i + 1), 32.0F);
			}

			Ref<Component> outer = captioned("A scroller in a scroller", {260.0F, 360.0F});
			outer.addChild<Text>(
				{.style = {.color = kLabel, .fontSize = 12.0F}, .text = "The inner list keeps the wheel to its end.", .wrap = true}
			);
			Ref<Component> inner = outer.addChild<Rectangle>({
				.id = "inner",
				.size = {ui::kAuto, 160.0F},
				.layout = column(2.0F, 4.0F),
				.style = {.backgroundColor = Foundation::Color(0.12F, 0.12F, 0.16F)},
				.overflow = ui::Overflow::Scroll,
			});
			for (int i = 0; i < 12; ++i) {
				hotRow(inner, "Inner " + std::to_string(i + 1), 28.0F);
			}
			for (int i = 0; i < 12; ++i) {
				hotRow(outer, "Outer " + std::to_string(i + 1), 32.0F);
			}

			Ref<Component> reading = captioned("A long text", {260.0F, 360.0F});
			std::string	   passage;
			for (int i = 0; i < 12; ++i) {
				passage += "Paragraph " + std::to_string(i + 1) +
						   ": a scroller measures its children, scrolls over their union and its padding, and its flex children don't "
						   "shrink to fit, so the text keeps the height its wrapping gives it.\n";
			}
			reading.addChild<Text>({.id = "passage", .style = {.fontSize = 13.0F}, .text = passage, .wrap = true});
		}

		void render(float /*alpha*/) override { Renderer::Primitives::clear(kBackground); }

	  private:
		// A caption over a framed scroller, which it returns.
		Ref<Component> captioned(const char* caption, ui::Size size) {
			Ref<Component> cell = root().addChild<Component>({.layout = column(6.0F)});
			cell.addChild<Text>({.style = {.color = kLabel, .fontSize = 14.0F}, .text = caption});
			return cell.addChild<Rectangle>({
				.id = caption,
				.size = size,
				.layout = column(4.0F, {6.0F, 14.0F, 6.0F, 6.0F}),
				.style = {.backgroundColor = kFrame},
				.overflow = ui::Overflow::Scroll,
			});
		}
	};

} // namespace

namespace sandbox {
	const SceneInfo kUiScrollScene{.name = kSceneName, .make = makeScene<UiScrollScene>};
} // namespace sandbox
