// ui_clip: Overflow::Clip, after worldsim's ui-sandbox clip scene (apps/ui-sandbox/scenes/
// ClipScene.cpp, a4226e17). A box that clips rows running past its edges, a clip nested in another
// so only their overlap shows, and a list sliding inside a clipped viewport, moved with the tick
// (scrolling itself comes with input). C toggles every clip off and on.

#include "Scenes.h"

#include <application/App.h>
#include <graphics/Color.h>
#include <primitives/Primitives.h>
#include <ui/shapes/Rectangle.h>
#include <ui/shapes/Text.h>

#include <cmath>
#include <string>
#include <vector>

namespace {

	constexpr const char* kSceneName = "ui_clip";
	constexpr int		  kRows = 12;
	constexpr float		  kRowHeight = 34.0F;

	const Foundation::Color kBackground(0.1F, 0.1F, 0.12F, 1.0F);
	const Foundation::Color kFrame(0.2F, 0.2F, 0.26F, 1.0F);
	const Foundation::Color kEdge(1.0F, 0.8F, 0.3F, 1.0F);

	using ui::Component;
	using ui::Overflow;
	using ui::Rectangle;
	using ui::Ref;
	using ui::Text;

	Foundation::Color rowColour(int index) {
		const float hue = static_cast<float>(index) / static_cast<float>(kRows);
		return {0.3F + (0.5F * hue), 0.4F, 0.8F - (0.5F * hue), 1.0F};
	}

	class UiClipScene final : public app::Scene {
	  public:
		const char* name() const override { return kSceneName; }

		void onEnter(app::App& /*app*/) override {
			root().addChild<Text>(
				{.id = "title",
				 .position = {30.0F, 24.0F},
				 .style = {.color = Foundation::Color::white(), .fontSize = 20.0F},
				 .text = "Clipping (C toggles)"}
			);

			Ref<Rectangle>						basic = clipped("basic", {40.0F, 90.0F}, {300.0F, 80.0F});
			const std::vector<Foundation::Vec2> past{{-40.0F, 4.0F}, {180.0F, 4.0F}, {60.0F, 60.0F}}; // the left, right and bottom edges
			for (int i = 0; i < 3; ++i) {
				Ref<Rectangle> row = basic.addChild<Rectangle>({
					.position = past[static_cast<std::size_t>(i)],
					.size = {180.0F, 36.0F},
					.style = {.backgroundColor = rowColour(i * 3)},
				});
				row.addChild<Text>(
					{.position = {8.0F, 8.0F}, .style = {.fontSize = 14.0F}, .text = "past the edge " + std::to_string(i + 1)}
				);
			}

			Ref<Rectangle> outer = clipped("outer", {420.0F, 90.0F}, {220.0F, 160.0F});
			Ref<Rectangle> inner = outer.addChild<Rectangle>({
				.id = "inner",
				.position = {110.0F, 80.0F},
				.size = {200.0F, 140.0F},
				.style = {.backgroundColor = Foundation::Color(0.35F, 0.25F, 0.3F)},
				.overflow = Overflow::Clip,
			});
			clips.push_back(inner);
			inner.addChild<Rectangle>(
				{.position = {-150.0F, -100.0F}, .size = {400.0F, 400.0F}, .style = {.backgroundColor = rowColour(9)}}
			);

			Ref<Rectangle> viewport = clipped("viewport", {40.0F, 300.0F}, {280.0F, 120.0F});
			list = viewport.addChild<Component>({
				.id = "list",
				.position = {0.0F, 0.0F},
				.size = {ui::percent(100.0F), ui::kAuto},
				.layout = {.display = ui::Display::Flex, .flexDirection = ui::FlexDirection::Column, .gap = 2.0F, .padding = 4.0F},
			});
			for (int i = 0; i < kRows; ++i) {
				Ref<Rectangle> row = list.addChild<Rectangle>(
					{.size = {ui::kAuto, kRowHeight}, .style = {.backgroundColor = rowColour(i), .borderRadius = 4.0F}}
				);
				row.addChild<Text>({.position = {10.0F, 9.0F}, .style = {.fontSize = 14.0F}, .text = "Row " + std::to_string(i + 1)});
			}
		}

		bool handleEvent(const foundation::InputEvent& event) override {
			if (event.type != foundation::InputEvent::Type::KeyDown || event.key != foundation::Key::C) {
				return false;
			}
			clipping = !clipping;
			for (const Ref<Rectangle>& box : clips) {
				if (Rectangle* clipper = box.get()) {
					clipper->setOverflow(clipping ? Overflow::Clip : Overflow::Visible);
				}
			}
			return true;
		}

		void fixedUpdate(const app::TickContext& tick) override {
			const float travel = (kRows * (kRowHeight + 2.0F)) - 120.0F;
			const float phase = 0.5F - (0.5F * std::cos(static_cast<float>(tick.tick) * 0.02F));
			if (Component* moving = list.get()) {
				moving->setPosition({0.0F, -travel * phase});
			}
		}

		void render(float /*alpha*/) override { Renderer::Primitives::clear(kBackground); }

	  private:
		// A framed box that clips what's in it, kept for the toggle.
		Ref<Rectangle> clipped(const char* id, Foundation::Vec2 position, Foundation::Vec2 size) {
			Ref<Rectangle> box = root().addChild<Rectangle>({
				.id = id,
				.position = position,
				.size = size,
				.style = {.backgroundColor = kFrame, .borderColor = kEdge, .borderWidth = 1.0F},
				.overflow = Overflow::Clip,
			});
			clips.push_back(box);
			return box;
		}

		std::vector<Ref<Rectangle>> clips;
		Ref<Component>				list;
		bool						clipping = true;
	};

} // namespace

namespace sandbox {
	const SceneInfo kUiClipScene{.name = kSceneName, .make = makeScene<UiClipScene>};
} // namespace sandbox
