// ui_input: pointer dispatch. Three buttons count their clicks, one with a label that is its own hit
// so the click comes up from it; a knob dragged along a track through capture; a card whose parts
// can't be hit (pointerEvents: Unit) beside a container the pointer passes through; a disabled button
// that blocks what's under it without clicking; and a cursor for each. The last event is written
// under them all.

#include "Scenes.h"

#include <application/App.h>
#include <graphics/Color.h>
#include <primitives/Primitives.h>
#include <ui/shapes/Rectangle.h>
#include <ui/shapes/Text.h>

#include <algorithm>
#include <string>

namespace {

	constexpr const char* kSceneName = "ui_input";
	constexpr float		  kTrack = 300.0F;
	constexpr float		  kKnob = 24.0F;

	const Foundation::Color kBackground(0.1F, 0.1F, 0.12F, 1.0F);
	const Foundation::Color kButton(0.25F, 0.45F, 0.7F, 1.0F);
	const Foundation::Color kButtonDown(0.18F, 0.32F, 0.5F, 1.0F);
	const Foundation::Color kMuted(0.3F, 0.3F, 0.34F, 1.0F);
	const Foundation::Color kLabel(0.85F, 0.85F, 0.6F, 1.0F);

	using ui::Component;
	using ui::PointerEvent;
	using ui::Rectangle;
	using ui::Ref;
	using ui::Text;

	class UiInputScene final : public app::Scene {
	  public:
		const char* name() const override { return kSceneName; }

		void onEnter(app::App& /*app*/) override {
			root().get()->setLayout(
				{.display = ui::Display::Flex,
				 .flexDirection = ui::FlexDirection::Column,
				 .gap = 24.0F,
				 .padding = 24.0F,
				 .alignItems = ui::AlignItems::Start}
			);
			Ref<Component> buttons = root().addChild<Component>({.layout = {.display = ui::Display::Flex, .gap = 16.0F}});
			for (int i = 0; i < 3; ++i) {
				button(buttons, i);
			}
			slider();
			hitModes();
			status = root().addChild<Text>({.id = "status", .style = {.color = kLabel, .fontSize = 14.0F}, .text = "Nothing yet"});
		}

		void render(float /*alpha*/) override { Renderer::Primitives::clear(kBackground); }

	  private:
		void show(const std::string& what) {
			if (Text* line = status.get()) {
				line->setText(what);
			}
		}

		void button(Ref<Component> row, int index) {
			const std::string name = "Button " + std::to_string(index + 1);
			Ref<Rectangle>	  made = row.addChild<Rectangle>({
				   .id = "button_" + std::to_string(index + 1),
				   .size = {140.0F, 44.0F},
				   .style = {.backgroundColor = kButton, .borderRadius = 6.0F},
				   .cursor = ui::Cursor::Pointer,
				   .onClick =
					   [this, name, count = 0](PointerEvent& event) mutable {
						   ++count;
						   show(name + " clicked " + std::to_string(count) + (event.target != event.currentTarget ? " (from its label)" : ""));
					   },
				   .onPointerDown =
					   [](PointerEvent& event) { event.currentTarget->setStyle({.backgroundColor = kButtonDown, .borderRadius = 6.0F}); },
				   .onPointerUp =
					   [](PointerEvent& event) { event.currentTarget->setStyle({.backgroundColor = kButton, .borderRadius = 6.0F}); },
			   });
			made.addChild<Text>({
				.size = {140.0F, 44.0F},
				.style = {.textAlign = Foundation::HorizontalAlign::Center, .verticalAlign = Foundation::VerticalAlign::Middle},
				.text = name,
			});
		}

		// The knob takes the pointer on its press and follows it anywhere until the release.
		void slider() {
			Ref<Rectangle> track = root().addChild<Rectangle>(
				{.id = "track", .size = {kTrack, 8.0F}, .margin = {12.0F, 0.0F}, .style = {.backgroundColor = kMuted, .borderRadius = 4.0F}}
			);
			track.addChild<Rectangle>({
				.id = "knob",
				.position = {0.0F, (8.0F - kKnob) * 0.5F},
				.size = {kKnob, kKnob},
				.style = {.backgroundColor = Foundation::Color::white(), .borderRadius = kKnob * 0.5F},
				.cursor = ui::Cursor::ResizeHorizontal,
				.onPointerDown = [](PointerEvent& event) { event.currentTarget->capturePointer(); },
				.onPointerMove =
					[this](PointerEvent& event) {
						Component* knob = event.currentTarget;
						if (!knob->hasPointerCapture()) {
							return;
						}
						const float along = knob->parent().get()->screenToLocal(event.screen).x - (kKnob * 0.5F);
						knob->setPosition({std::clamp(along, 0.0F, kTrack - kKnob), knob->getPosition().y});
						show("Knob at " + std::to_string(static_cast<int>(100.0F * knob->getPosition().x / (kTrack - kKnob))) + "%");
					},
			});
		}

		void hitModes() {
			Ref<Component> row = root().addChild<Component>({.layout = {.display = ui::Display::Flex, .gap = 16.0F}});
			Ref<Rectangle> card = row.addChild<Rectangle>({
				.id = "unit_card",
				.size = {180.0F, 80.0F},
				.style = {.backgroundColor = kMuted, .borderRadius = 6.0F},
				.pointerEvents = ui::PointerEvents::Unit,
				.onClick = [this](PointerEvent& event) { show("Card clicked; target " + event.target->getId()); },
			});
			card.addChild<Text>({.position = {10.0F, 10.0F}, .style = {.fontSize = 13.0F}, .text = "Unit: the card is the hit"});

			Ref<Component> pass = row.addChild<Component>({.id = "passthrough", .size = {180.0F, 80.0F}});
			pass.addChild<Rectangle>({
				.id = "pass_child",
				.position = {20.0F, 20.0F},
				.size = {140.0F, 40.0F},
				.style = {.backgroundColor = kButton, .borderRadius = 4.0F},
				.cursor = ui::Cursor::Crosshair,
				.onClick = [this](PointerEvent& /*event*/) { show("Through the container to its child"); },
			});

			Ref<Component> stack = row.addChild<Component>({.size = {180.0F, 80.0F}});
			stack.addChild<Rectangle>({
				.id = "under_disabled",
				.size = {180.0F, 80.0F},
				.style = {.backgroundColor = kButton},
				.onClick = [this](PointerEvent& /*event*/) { show("Under the disabled button: shouldn't happen"); },
			});
			stack
				.addChild<Rectangle>({
					.id = "disabled",
					.enabled = false,
					.size = {180.0F, 80.0F},
					.style = {.backgroundColor = kMuted, .opacity = 0.9F},
					.zIndex = 1,
					.cursor = ui::Cursor::NotAllowed,
					.onClick = [this](PointerEvent& /*event*/) { show("Disabled clicked: shouldn't happen"); },
				})
				.addChild<Text>({.position = {10.0F, 30.0F}, .style = {.fontSize = 13.0F}, .text = "Disabled, and in the way"});
		}

		Ref<Text> status;
	};

} // namespace

namespace sandbox {
	const SceneInfo kUiInputScene{.name = kSceneName, .make = makeScene<UiInputScene>};
} // namespace sandbox
