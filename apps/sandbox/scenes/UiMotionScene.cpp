// ui_motion: the animator. Three cards lift and wash on hover, eased through a state colour and a
// retargeted tween, so moving quickly across them reverses mid-flight instead of snapping; a meter
// fills to a new value on each click over motion.duration.slow; a dot pulses on a timer. R toggles
// reduced motion, under which every tween lands on its first frame.

#include "Scenes.h"

#include <application/App.h>
#include <graphics/Color.h>
#include <primitives/Primitives.h>
#include <ui/shapes/Rectangle.h>
#include <ui/shapes/Text.h>
#include <ui/theme/StateColor.h>
#include <ui/theme/Tokens.h>

#include <string>

namespace {

	constexpr const char* kSceneName = "ui_motion";
	constexpr float		  kLift = 8.0F;
	constexpr float		  kMeterWidth = 320.0F;

	const Foundation::Color kBackground(0.1F, 0.1F, 0.12F, 1.0F);
	const Foundation::Color kCard(0.22F, 0.3F, 0.42F, 1.0F);
	const Foundation::Color kTrack(0.25F, 0.25F, 0.28F, 1.0F);
	const Foundation::Color kFill(0.4F, 0.75F, 0.5F, 1.0F);
	const Foundation::Color kLabel(0.85F, 0.85F, 0.6F, 1.0F);

	using ui::Component;
	using ui::PointerEvent;
	using ui::Ref;
	using ui::Text;

	// Its fill follows hover and press through a StateColor, and it rises while hovered.
	class Card final : public ui::Rectangle {
	  protected:
		void onMount() override {
			color.attach(*this, kCard);
			lift = tween<float>({.onUpdate = [this](const float& px) { setTransform({.translate = {0.0F, -px}}); }});
		}

		void handlePointer(PointerEvent& event) override {
			if (event.type == PointerEvent::Type::Enter || event.type == PointerEvent::Type::Leave ||
				event.type == PointerEvent::Type::Down || event.type == PointerEvent::Type::Up) {
				color.setState({.hovered = isHovered(), .pressed = isPressed()});
				lift.retarget(isHovered() ? kLift : 0.0F, getContext()->getTheme().get(ui::tokens::motion::duration::fast));
			}
		}

		void render() override {
			Renderer::Primitives::drawRect({
				.bounds = {0.0F, 0.0F, getSize().x, getSize().y},
				.style =
					{.fill = color.value(),
					 .border = Foundation::BorderStyle{.color = {0.0F, 0.0F, 0.0F, 0.0F}, .width = 0.0F, .cornerRadius = 8.0F}},
				.key = drawKey(),
			});
		}

	  private:
		ui::StateColor	 color;
		ui::Tween<float> lift;
	};

	class UiMotionScene final : public app::Scene {
	  public:
		const char* name() const override { return kSceneName; }

		void onEnter(app::App& /*app*/) override {
			root().get()->setLayout(
				{.display = ui::Display::Flex, .flexDirection = ui::FlexDirection::Column, .gap = 28.0F, .padding = 32.0F}
			);
			Ref<Component> cards = root().addChild<Component>({.layout = {.display = ui::Display::Flex, .gap = 20.0F}});
			for (int i = 0; i < 3; ++i) {
				Ref<Card> card =
					cards.addChild<Card>({.id = "card_" + std::to_string(i + 1), .size = {160.0F, 100.0F}, .cursor = ui::Cursor::Pointer});
				card.addChild<Text>(
					{.position = {12.0F, 12.0F}, .style = {.fontSize = 14.0F}, .pointerEvents = ui::PointerEvents::None, .text = "Hover me"}
				);
			}
			meter();
			pulse();
			status = root().addChild<Text>({.id = "status", .style = {.color = kLabel, .fontSize = 14.0F}});
			showMotion();
		}

		bool handleEvent(const foundation::InputEvent& event) override {
			if (event.type == foundation::InputEvent::Type::KeyDown && event.key == foundation::Key::R && !event.repeat) {
				ui().setReducedMotion(!ui().isReducedMotion());
				showMotion();
				return true;
			}
			return false;
		}

		void render(float /*alpha*/) override { Renderer::Primitives::clear(kBackground); }

	  private:
		void showMotion() {
			if (Text* line = status.get()) {
				line->setText(std::string("R toggles reduced motion: ") + (ui().isReducedMotion() ? "on" : "off"));
			}
		}

		// Each click sends the fill to the next of five values, from wherever it is.
		void meter() {
			Ref<ui::Rectangle> track = root().addChild<ui::Rectangle>({
				.id = "meter",
				.size = {kMeterWidth, 16.0F},
				.style = {.backgroundColor = kTrack, .borderRadius = 8.0F},
				.cursor = ui::Cursor::Pointer,
				.onClick = [this](PointerEvent& /*event*/) {
					step = (step + 1) % 5;
					const ui::Theme& theme = ui().getTheme();
					fillTween.retarget(
						kMeterWidth * static_cast<float>(step + 1) / 5.0F,
						theme.get(ui::tokens::motion::duration::slow),
						ui::Easing(theme.get(ui::tokens::motion::easing::enter))
					);
				},
			});
			fill = track.addChild<ui::Rectangle>(
				{.id = "meter_fill",
				 .size = {kMeterWidth / 5.0F, 16.0F},
				 .style = {.backgroundColor = kFill, .borderRadius = 8.0F},
				 .pointerEvents = ui::PointerEvents::None}
			);
			fillTween = fill.get()->tween<float>({
				.from = kMeterWidth / 5.0F,
				.to = kMeterWidth / 5.0F,
				.onUpdate = [this](const float& width) {
					if (ui::Rectangle* bar = fill.get()) {
						bar->setSize({width, 16.0F});
					}
				},
			});
		}

		// A timer flips the dot's target every 600 ms; the tween eases it there.
		void pulse() {
			Ref<ui::Rectangle> dot = root().addChild<ui::Rectangle>(
				{.id = "pulse", .size = {24.0F, 24.0F}, .style = {.backgroundColor = kLabel, .borderRadius = 12.0F}}
			);
			pulseTween = dot.get()->tween<float>({
				.from = 1.0F,
				.to = 1.0F,
				.onUpdate = [dot](const float& opacity) {
					if (ui::Rectangle* shown = dot.get()) {
						shown->setStyle({.backgroundColor = kLabel, .borderRadius = 12.0F, .opacity = opacity});
					}
				},
			});
			dot.get()->every(600.0, [this] {
				bright = !bright;
				pulseTween.retarget(bright ? 1.0F : 0.25F);
			});
		}

		Ref<Text>		   status;
		Ref<ui::Rectangle> fill;
		ui::Tween<float>   fillTween;
		ui::Tween<float>   pulseTween;
		int				   step = 0;
		bool			   bright = true;
	};

} // namespace

namespace sandbox {
	const SceneInfo kUiMotionScene{.name = kSceneName, .make = makeScene<UiMotionScene>};
} // namespace sandbox
