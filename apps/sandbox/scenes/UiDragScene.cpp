// ui_drag: drag and drop. Three cards in a hand that clips, two their own ghosts and one that leaves a
// reticle as its ghost, over a road of targets: one that takes any card, one that takes only red, one
// that refuses everything, and a verge that takes any card around a vehicle that refuses them. A
// target that would take the drag under way is outlined, one with a drag it takes over it fills, and
// one that refuses it is edged red. Enter picks up the focused card, the arrows and Tab move it
// between targets, Enter drops and Escape (or a right click) cancels. The last thing that happened
// is written under them.

#include "Scenes.h"

#include <application/App.h>
#include <graphics/Color.h>
#include <primitives/Primitives.h>
#include <ui/Context.h>
#include <ui/shapes/Rectangle.h>
#include <ui/shapes/Text.h>

#include <any>
#include <string>

namespace {

	constexpr const char* kSceneName = "ui_drag";

	const Foundation::Color kBackground(0.1F, 0.1F, 0.12F, 1.0F);
	const Foundation::Color kLabel(0.85F, 0.85F, 0.6F, 1.0F);
	const Foundation::Color kHand(0.16F, 0.16F, 0.19F, 1.0F);
	const Foundation::Color kSlot(0.2F, 0.22F, 0.26F, 1.0F);
	const Foundation::Color kSlotEdge(0.32F, 0.34F, 0.4F, 1.0F);
	const Foundation::Color kTakes(0.45F, 0.75F, 0.55F, 1.0F);
	const Foundation::Color kOver(0.22F, 0.42F, 0.3F, 1.0F);
	const Foundation::Color kRefused(0.85F, 0.3F, 0.3F, 1.0F);
	const Foundation::Color kBlue(0.25F, 0.45F, 0.7F, 1.0F);
	const Foundation::Color kRed(0.7F, 0.28F, 0.3F, 1.0F);
	const Foundation::Color kFocusRing(1.0F, 1.0F, 1.0F, 0.9F);

	using ui::Component;
	using ui::Drag;
	using ui::Rectangle;
	using ui::Ref;
	using ui::Text;

	// What a card carries: its name and colour.
	struct CardValue {
		std::string name;
		bool		red = false;
	};

	std::string nameOf(const Drag& drag) {
		return std::any_cast<CardValue>(drag.value).name;
	}

	std::string idOf(const Ref<Component>& ref) {
		const Component* component = ref.get();
		return component != nullptr ? component->getId() : std::string("nothing");
	}

	// A drop target drawn from the drag's state: outlined while it would take the drag under way, filled
	// while one it takes is over it, edged red while one it refuses is.
	class Slot final : public Rectangle {
	  protected:
		void render() override {
			const Drag*				drag = getContext()->drag();
			const bool				refused = drag != nullptr && drag->target == self() && !drag->accepted;
			const bool				takes = acceptsDrag();
			const Foundation::Color edge = refused ? kRefused : takes ? kTakes : kSlotEdge;
			Renderer::Primitives::drawRect({
				.bounds = {0.0F, 0.0F, size.x, size.y},
				.style =
					{.fill = isDropActive() ? kOver : kSlot,
					 .border = Foundation::BorderStyle{.color = edge, .width = takes || refused ? 3.0F : 1.0F, .cornerRadius = 8.0F}},
				.key = drawKey(),
			});
		}
	};

	// A card: its colour, dimmed while it's dragged and something else is its ghost, ringed while focus
	// shows on it.
	class Card final : public Rectangle {
	  protected:
		void render() override {
			const Drag*		  drag = getContext()->drag();
			const bool		  leftBehind = isDragging() && drag != nullptr && drag->ghost != self();
			Foundation::Color fill = getStyle().backgroundColor.value_or(kBlue);
			fill.a = leftBehind ? 0.4F : 1.0F;
			Renderer::Primitives::drawRect({
				.bounds = {0.0F, 0.0F, size.x, size.y},
				.style =
					{.fill = fill,
					 .border =
						 Foundation::BorderStyle{
							 .color = isFocusVisible() ? kFocusRing : Foundation::Color(0.0F, 0.0F, 0.0F, 0.0F),
							 .width = 2.0F,
							 .cornerRadius = 8.0F,
						 }},
				.key = drawKey(),
			});
		}
	};

	class UiDragScene final : public app::Scene {
	  public:
		const char* name() const override { return kSceneName; }

		void onEnter(app::App& /*app*/) override {
			root().get()->setLayout(
				{.display = ui::Display::Flex,
				 .flexDirection = ui::FlexDirection::Column,
				 .gap = 20.0F,
				 .padding = 24.0F,
				 .alignItems = ui::AlignItems::Start}
			);
			root().addChild<Text>({
				.style = {.color = kLabel, .fontSize = 14.0F},
				.text = "Drag a card to the road. Keys: Enter picks up and drops, arrows and Tab move, Escape cancels.",
			});
			road();
			hand();
			status = root().addChild<Text>({.id = "status", .style = {.color = kLabel, .fontSize = 14.0F}, .text = "Nothing yet"});
		}

		void render(float /*alpha*/) override { Renderer::Primitives::clear(kBackground); }

	  private:
		void show(const std::string& what) {
			if (Text* line = status.get()) {
				line->setText(what);
			}
		}

		void road() {
			Ref<Component> row = root().addChild<Component>({.id = "road", .layout = {.display = ui::Display::Flex, .gap = 16.0F}});
			slot(row, "any", "Any card", {180.0F, 110.0F}, nullptr);
			slot(row, "red_only", "Red only", {180.0F, 110.0F}, [](const Drag& drag) { return std::any_cast<CardValue>(drag.value).red; });
			slot(row, "refuses", "Refuses", {180.0F, 110.0F}, [](const Drag&) { return false; });
			Ref<Component> verge = slot(row, "verge", "Verge", {300.0F, 110.0F}, nullptr);
			slot(verge, "vehicle", "Vehicle (refuses)", {130.0F, 90.0F}, [](const Drag&) { return false; }, {10.0F, 10.0F});
		}

		Ref<Component> slot(
			Ref<Component>					 parent,
			const char*						 id,
			const char*						 label,
			Foundation::Vec2				 size,
			std::function<bool(const Drag&)> accepts,
			Foundation::Vec2				 at = {0.0F, 0.0F}
		) {
			Ref<Slot>	made = parent.addChild<Slot>({.id = id, .position = at, .size = size});
			const float labelLeft = std::string(id) == "verge" ? 150.0F : 0.0F;
			made.addChild<Text>({
				.position = {labelLeft, 0.0F},
				.size = {size.x - labelLeft, size.y},
				.style =
					{.color = kLabel,
					 .fontSize = 13.0F,
					 .textAlign = Foundation::HorizontalAlign::Center,
					 .verticalAlign = Foundation::VerticalAlign::Middle},
				.pointerEvents = ui::PointerEvents::None,
				.text = label,
				.wrap = true,
			});
			const std::string name = label;
			made.get()->setDropTarget({
				.accepts = std::move(accepts),
				.onDrop = [this, name](const Drag& drag) { show("Dropped " + nameOf(drag) + " on " + name); },
			});
			return made;
		}

		void hand() {
			Ref<Component> row = root().addChild<Component>({
				.id = "hand",
				.size = {420.0F, 170.0F},
				.layout = {.display = ui::Display::Flex, .gap = 16.0F, .padding = 10.0F},
				.style = {.backgroundColor = kHand, .borderRadius = 8.0F},
				.overflow = ui::Overflow::Clip,
			});
			card(row, "card_blue", {.name = "Blue 1"}, false);
			card(row, "card_red", {.name = "Red 2", .red = true}, false);
			card(row, "card_reticle", {.name = "Blue 3"}, true);
		}

		// `reticle`: it stays in the hand, dimmed, and a reticle under the press is its ghost.
		void card(Ref<Component> row, const char* id, const CardValue& value, bool reticle) {
			Ref<Card> made = row.addChild<Card>({
				.id = id,
				.size = {120.0F, 150.0F},
				.style = {.backgroundColor = value.red ? kRed : kBlue},
				.focusable = true,
				.cursor = ui::Cursor::Pointer,
			});
			made.addChild<Text>({
				.size = {120.0F, 150.0F},
				.style = {.textAlign = Foundation::HorizontalAlign::Center, .verticalAlign = Foundation::VerticalAlign::Middle},
				.pointerEvents = ui::PointerEvents::None,
				.text = value.name + (reticle ? " (reticle)" : ""),
				.wrap = true,
			});
			ui::DragSource source{
				.kind = "card",
				.value = value,
				.onDragStart = [this](const Drag& drag) { show("Picked up " + nameOf(drag)); },
				.onDragEnd =
					[this](const Drag& drag) {
						if (!drag.dropped) {
							show(
								nameOf(drag) + " back in the hand" +
								(drag.target.get() != nullptr ? ", refused by " + idOf(drag.target) : "")
							);
						}
					},
			};
			if (reticle) {
				source.ghost = [](Component& from, Foundation::Vec2 at) -> Ref<Component> {
					return from.addChild<Rectangle>({
						.id = "reticle",
						.position = at - Foundation::Vec2{14.0F, 14.0F},
						.size = {28.0F, 28.0F},
						.style =
							{.backgroundColor = Foundation::Color(1.0F, 1.0F, 1.0F, 0.15F),
							 .borderColor = kTakes,
							 .borderWidth = 3.0F,
							 .borderRadius = 14.0F},
						.pointerEvents = ui::PointerEvents::None,
					});
				};
			}
			made.get()->setDragSource(std::move(source));
		}

		Ref<Text> status;
	};

} // namespace

namespace sandbox {
	const SceneInfo kUiDragScene{.name = kSceneName, .make = makeScene<UiDragScene>};
} // namespace sandbox
