// Drag and drop's state machine: the threshold between a click and a drag, capture, the ghost in the
// drag tier, targets that accept and refuse, every way a drag ends, the keys' and a pad's path, and
// what the tree shows. Headless; events go straight to a context.

#include "ui/Context.h"
#include "ui/debug/TreeSnapshot.h"
#include "ui/shapes/Rectangle.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <any>
#include <string>
#include <utility>
#include <vector>

namespace ui {

	namespace {

		using foundation::DeviceId;
		using foundation::DeviceKind;
		using foundation::GamepadButton;
		using foundation::InputEvent;
		using foundation::Key;
		using foundation::MouseButton;

		constexpr DeviceId kMouse{.kind = DeviceKind::Mouse};
		constexpr DeviceId kKeyboard{.kind = DeviceKind::Keyboard};

		// The card's centre in the hand, and the targets' on the road.
		constexpr Foundation::Vec2 kCard{60.0F, 480.0F};
		constexpr Foundation::Vec2 kLane{160.0F, 140.0F};
		constexpr Foundation::Vec2 kPicky{360.0F, 140.0F};
		constexpr Foundation::Vec2 kWall{560.0F, 140.0F};
		constexpr Foundation::Vec2 kVehicle{150.0F, 300.0F};
		constexpr Foundation::Vec2 kVerge{350.0F, 300.0F};
		constexpr Foundation::Vec2 kEmpty{700.0F, 20.0F};

		InputEvent pointer(InputEvent::Type type, Foundation::Vec2 at, MouseButton button = MouseButton::Left) {
			return {.type = type, .device = kMouse, .mouseButton = button, .position = at};
		}

		InputEvent keyDown(Key key) {
			return {.type = InputEvent::Type::KeyDown, .device = kKeyboard, .key = key};
		}

		InputEvent padDown(GamepadButton button) {
			return {.type = InputEvent::Type::ButtonDown, .device = {.kind = DeviceKind::Gamepad, .index = 0}, .button = button};
		}

		std::string idOf(const Ref<Component>& ref) {
			const Component* component = ref.get();
			return component != nullptr ? component->getId() : std::string("none");
		}

		// A hand that clips, holding one card, under a road of targets: a lane that takes anything, a
		// picky lane that takes only card 7, a wall that refuses, and a verge that takes anything around
		// a vehicle that refuses.
		class UiDragTest : public ::testing::Test {
		  protected:
			void SetUp() override {
				road = root.addChild<Component>({.id = "road", .size = {800.0F, 400.0F}});
				lane = target("lane", road, {100.0F, 100.0F}, {120.0F, 80.0F}, nullptr);
				picky = target("picky", road, {300.0F, 100.0F}, {120.0F, 80.0F}, [](const Drag& drag) {
					return drag.kind == "card" && std::any_cast<int>(drag.value) == 7;
				});
				wall = target("wall", road, {500.0F, 100.0F}, {120.0F, 80.0F}, [](const Drag&) { return false; });
				verge = target("verge", road, {100.0F, 250.0F}, {300.0F, 100.0F}, nullptr);
				vehicle = target("vehicle", verge, {0.0F, 0.0F}, {100.0F, 100.0F}, [](const Drag&) { return false; });
				hand = root.addChild<Component>(
					{.id = "hand", .position = {0.0F, 400.0F}, .size = {400.0F, 150.0F}, .overflow = Overflow::Clip}
				);
				card = hand.addChild<Rectangle>({
					.id = "card",
					.position = {20.0F, 20.0F},
					.size = {80.0F, 120.0F},
					.focusable = true,
					.onClick = [this](PointerEvent&) { journal.push_back("click"); },
				});
				card.get()->setDragSource(source());
				context.layout();
			}

			DragSource source() {
				return {
					.kind = "card",
					.value = 7,
					.onDragStart = [this](const Drag&) { journal.push_back("start"); },
					.onDragEnd =
						[this](const Drag& drag) {
							journal.push_back(std::string(drag.dropped ? "end dropped " : "end not dropped ") + idOf(drag.target));
						},
				};
			}

			Ref<Component> target(
				const char*						 id,
				Ref<Component>					 parent,
				Foundation::Vec2				 at,
				Foundation::Vec2				 size,
				std::function<bool(const Drag&)> accepts
			) {
				Ref<Component>	  made = parent.addChild<Rectangle>({.id = id, .position = at, .size = size});
				const std::string name = id;
				made.get()->setDropTarget({
					.accepts = std::move(accepts),
					.onDragEnter = [this, name](const Drag&) { journal.push_back("enter " + name); },
					.onDragLeave = [this, name](const Drag&) { journal.push_back("leave " + name); },
					.onDrop = [this, name](const Drag& drag) { journal.push_back("drop " + name + " " + drag.kind); },
				});
				return made;
			}

			// Presses the card and moves the pointer `to`, past the threshold.
			void pickUpAt(Foundation::Vec2 to) {
				context.dispatch(pointer(InputEvent::Type::PointerDown, kCard));
				context.dispatch(pointer(InputEvent::Type::PointerMove, to));
			}

			std::vector<std::string> take() { return std::exchange(journal, {}); }

			Context					 context{{800.0F, 600.0F}};
			Ref<Component>			 root = context.root();
			Ref<Component>			 road;
			Ref<Component>			 hand;
			Ref<Component>			 card;
			Ref<Component>			 lane;
			Ref<Component>			 picky;
			Ref<Component>			 wall;
			Ref<Component>			 verge;
			Ref<Component>			 vehicle;
			std::vector<std::string> journal;
		};

		using Journal = std::vector<std::string>;

	} // namespace

	// --- The threshold ---

	TEST_F(UiDragTest, APressThatStaysWithinTheThresholdClicks) {
		context.dispatch(pointer(InputEvent::Type::PointerDown, kCard));
		context.dispatch(pointer(InputEvent::Type::PointerMove, kCard + Foundation::Vec2{kDragThreshold, 0.0F}));
		EXPECT_EQ(context.drag(), nullptr);
		context.dispatch(pointer(InputEvent::Type::PointerUp, kCard + Foundation::Vec2{kDragThreshold, 0.0F}));
		EXPECT_EQ(take(), Journal{"click"});
	}

	TEST_F(UiDragTest, PastTheThresholdItDragsCapturesAndNeverClicks) {
		pickUpAt(kCard + Foundation::Vec2{0.0F, -(kDragThreshold + 1.0F)});
		const Drag* drag = context.drag();
		ASSERT_NE(drag, nullptr);
		EXPECT_EQ(drag->via, DragVia::Pointer);
		EXPECT_EQ(drag->kind, "card");
		EXPECT_EQ(std::any_cast<int>(drag->value), 7);
		EXPECT_EQ(drag->source, card);
		EXPECT_TRUE(card.get()->hasPointerCapture());
		EXPECT_TRUE(card.get()->isDragging());
		EXPECT_FALSE(card.get()->isPressed());
		context.dispatch(pointer(InputEvent::Type::PointerUp, kCard));
		EXPECT_EQ(take(), (Journal{"start", "end not dropped none"}));
		EXPECT_EQ(context.drag(), nullptr);
		EXPECT_FALSE(card.get()->hasPointerCapture());
		EXPECT_FALSE(card.get()->isDragging());
	}

	TEST_F(UiDragTest, APressConsumedBelowTheSourceStartsNothing) {
		card.addChild<Rectangle>({
			.id = "button",
			.size = {20.0F, 20.0F},
			.onPointerDown = [](PointerEvent& event) { event.consume(); },
		});
		context.layout();
		context.dispatch(pointer(InputEvent::Type::PointerDown, {30.0F, 430.0F}));
		context.dispatch(pointer(InputEvent::Type::PointerMove, kLane));
		EXPECT_EQ(context.drag(), nullptr);
	}

	// --- The ghost ---

	TEST_F(UiDragTest, TheSourceIsItsOwnGhostInTheDragTierOutOfItsClipAndNeverHit) {
		pickUpAt(kLane);
		context.layout();
		Component* ghost = card.get();
		EXPECT_TRUE(ghost->isLifted());
		EXPECT_EQ(ghost->paintSlot(), kDragSlot);
		EXPECT_FALSE(ghost->getClip().has_value());
		// Moved by the travel: its centre under the pointer, as the press was.
		const Foundation::Rect box = ghost->screenBounds();
		EXPECT_FLOAT_EQ(box.x + (box.width * 0.5F), kLane.x);
		EXPECT_FLOAT_EQ(box.y + (box.height * 0.5F), kLane.y);
		// Its box in layout is where it was, so the hand doesn't close up.
		EXPECT_FLOAT_EQ(ghost->getBounds().x, 20.0F);
		// A hit test sees through it to the target under it.
		EXPECT_EQ(context.find(context.hitTest(kLane))->getId(), "lane");

		context.dispatch(pointer(InputEvent::Type::PointerUp, kLane));
		context.layout();
		EXPECT_FALSE(ghost->isLifted());
		EXPECT_EQ(ghost->paintSlot(), 0);
		EXPECT_FLOAT_EQ(ghost->screenBounds().x, 20.0F);
		EXPECT_TRUE(ghost->getClip().has_value());
	}

	TEST_F(UiDragTest, ABuiltGhostStartsAtThePressAndGoesWithTheDrag) {
		DragSource		 withGhost = source();
		Foundation::Vec2 handed{-1.0F, -1.0F};
		withGhost.ghost = [&](Component& from, Foundation::Vec2 at) -> Ref<Component> {
			handed = at;
			return from.addChild<Rectangle>({.id = "reticle", .position = at - Foundation::Vec2{8.0F, 8.0F}, .size = {16.0F, 16.0F}});
		};
		card.get()->setDragSource(withGhost);
		pickUpAt(kLane);
		EXPECT_FLOAT_EQ(handed.x, 40.0F);
		EXPECT_FLOAT_EQ(handed.y, 60.0F);
		context.layout();
		const Drag* drag = context.drag();
		ASSERT_NE(drag, nullptr);
		EXPECT_EQ(idOf(drag->ghost), "reticle");
		const Component* reticle = drag->ghost.get();
		EXPECT_EQ(reticle->paintSlot(), kDragSlot);
		EXPECT_FALSE(card.get()->isLifted()); // the card stays in the hand
		const Foundation::Rect box = reticle->screenBounds();
		EXPECT_FLOAT_EQ(box.x + 8.0F, kLane.x);
		EXPECT_FLOAT_EQ(box.y + 8.0F, kLane.y);
		const Ref<Component> ghostRef = drag->ghost;
		context.dispatch(pointer(InputEvent::Type::PointerUp, kLane));
		EXPECT_EQ(ghostRef.get(), nullptr);
		EXPECT_TRUE(card.get()->getChildren().empty());
	}

	// --- Targets ---

	TEST_F(UiDragTest, DroppedOnATargetThatAccepts) {
		pickUpAt(kLane);
		EXPECT_EQ(idOf(context.drag()->target), "lane");
		EXPECT_TRUE(context.drag()->accepted);
		EXPECT_TRUE(lane.get()->isDropActive());
		context.dispatch(pointer(InputEvent::Type::PointerMove, kPicky));
		EXPECT_FALSE(lane.get()->isDropActive());
		EXPECT_TRUE(picky.get()->isDropActive());
		context.dispatch(pointer(InputEvent::Type::PointerUp, kPicky));
		EXPECT_EQ(take(), (Journal{"start", "enter lane", "leave lane", "enter picky", "drop picky card", "end dropped picky"}));
		EXPECT_FALSE(picky.get()->isDropActive());
	}

	TEST_F(UiDragTest, ATargetThatRefusesIsLeftAndShowsNotAllowed) {
		pickUpAt(kWall);
		EXPECT_EQ(idOf(context.drag()->target), "wall");
		EXPECT_FALSE(context.drag()->accepted);
		EXPECT_FALSE(wall.get()->isDropActive());
		EXPECT_EQ(context.cursor(), Cursor::NotAllowed);
		context.dispatch(pointer(InputEvent::Type::PointerUp, kWall));
		EXPECT_EQ(take(), (Journal{"start", "enter wall", "leave wall", "end not dropped wall"}));
	}

	TEST_F(UiDragTest, WhatATargetTakesCanDependOnTheDrag) {
		DragSource other = source();
		other.value = 8;
		card.get()->setDragSource(other);
		pickUpAt(kPicky);
		EXPECT_FALSE(context.drag()->accepted);
		EXPECT_TRUE(lane.get()->acceptsDrag());
		EXPECT_FALSE(picky.get()->acceptsDrag());
		EXPECT_FALSE(wall.get()->acceptsDrag());
	}

	TEST_F(UiDragTest, TheInnermostTargetDecidesAndARefusalIsntPassedUp) {
		pickUpAt(kVehicle);
		EXPECT_EQ(idOf(context.drag()->target), "vehicle");
		context.dispatch(pointer(InputEvent::Type::PointerUp, kVehicle));
		EXPECT_EQ(take(), (Journal{"start", "enter vehicle", "leave vehicle", "end not dropped vehicle"}));
		pickUpAt(kVerge);
		context.dispatch(pointer(InputEvent::Type::PointerUp, kVerge));
		EXPECT_EQ(take(), (Journal{"start", "enter verge", "drop verge card", "end dropped verge"}));
	}

	TEST_F(UiDragTest, ADisabledTargetRefuses) {
		lane.get()->setEnabled(false);
		pickUpAt(kLane);
		EXPECT_EQ(idOf(context.drag()->target), "lane");
		EXPECT_FALSE(context.drag()->accepted);
	}

	// --- Ending ---

	TEST_F(UiDragTest, EscapeCancelsAndTheReleaseClicksNothing) {
		pickUpAt(kLane);
		EXPECT_TRUE(context.dispatch(keyDown(Key::Escape)));
		EXPECT_EQ(context.drag(), nullptr);
		EXPECT_FALSE(lane.get()->isDropActive());
		context.dispatch(pointer(InputEvent::Type::PointerUp, kLane));
		EXPECT_EQ(take(), (Journal{"start", "enter lane", "leave lane", "end not dropped lane"}));
	}

	TEST_F(UiDragTest, ASecondButtonCancels) {
		pickUpAt(kLane);
		context.dispatch(pointer(InputEvent::Type::PointerDown, kLane, MouseButton::Right));
		EXPECT_EQ(context.drag(), nullptr);
		context.dispatch(pointer(InputEvent::Type::PointerUp, kLane, MouseButton::Right));
		context.dispatch(pointer(InputEvent::Type::PointerUp, kLane));
		EXPECT_EQ(take(), (Journal{"start", "enter lane", "leave lane", "end not dropped lane"}));
	}

	TEST_F(UiDragTest, LosingTheWindowOrTheSceneCancels) {
		pickUpAt(kLane);
		context.cancelInteraction();
		EXPECT_EQ(context.drag(), nullptr);
		EXPECT_FALSE(card.get()->hasPointerCapture());
		EXPECT_EQ(take(), (Journal{"start", "enter lane", "leave lane", "end not dropped lane"}));
	}

	TEST_F(UiDragTest, TheSourceGoingEndsItAndItStillHearsTheEnd) {
		pickUpAt(kLane);
		hand.get()->removeChild(card);
		EXPECT_EQ(context.drag(), nullptr);
		EXPECT_EQ(take(), (Journal{"start", "enter lane", "leave lane", "end not dropped lane"}));
		context.dispatch(pointer(InputEvent::Type::PointerUp, kLane));
		EXPECT_TRUE(take().empty());
	}

	TEST_F(UiDragTest, ATargetGoingUnderTheDragIsDroppedQuietly) {
		pickUpAt(kLane);
		road.get()->removeChild(lane);
		EXPECT_EQ(context.drag()->target.get(), nullptr);
		context.dispatch(pointer(InputEvent::Type::PointerUp, kLane));
		EXPECT_EQ(take(), (Journal{"start", "enter lane", "end not dropped none"}));
	}

	TEST_F(UiDragTest, ReleasingTheCaptureCancels) {
		pickUpAt(kLane);
		card.get()->releasePointer();
		EXPECT_EQ(context.drag(), nullptr);
	}

	TEST_F(UiDragTest, ADropThatRemovesTheSourceStillEndsOnce) {
		lane.get()->setDropTarget({.onDrop = [this](const Drag&) {
			journal.push_back("drop");
			hand.get()->removeChild(card);
		}});
		pickUpAt(kLane);
		context.dispatch(pointer(InputEvent::Type::PointerUp, kLane));
		EXPECT_EQ(take(), (Journal{"start", "drop", "end dropped lane"}));
		EXPECT_EQ(card.get(), nullptr);
	}

	TEST_F(UiDragTest, NoTooltipOpensDuringADrag) {
		int opened = 0;
		lane.get()->setTooltip([&](Component& owner, Foundation::Vec2) {
			++opened;
			return owner.openOverlay<Component>({.size = {10.0F, 10.0F}}, {.kind = OverlayKind::Tooltip});
		});
		pickUpAt(kLane);
		context.update(1.0);
		context.update(1.0);
		EXPECT_EQ(opened, 0);
	}

	// --- Keys and pads ---

	TEST_F(UiDragTest, KeysPickUpMoveBetweenAcceptingTargetsAndDrop) {
		ASSERT_TRUE(context.focus(card));
		EXPECT_TRUE(context.dispatch(keyDown(Key::Enter)));
		const Drag* drag = context.drag();
		ASSERT_NE(drag, nullptr);
		EXPECT_EQ(drag->via, DragVia::Keys);
		// The accepting target nearest the card: the verge, not the vehicle that refuses.
		EXPECT_EQ(idOf(drag->target), "verge");
		context.layout();
		const Foundation::Rect box = card.get()->screenBounds();
		EXPECT_FLOAT_EQ(box.x + (box.width * 0.5F), 250.0F); // centred on the verge
		EXPECT_EQ(card.get()->paintSlot(), kDragSlot);

		context.dispatch(keyDown(Key::Up));
		EXPECT_EQ(idOf(context.drag()->target), "lane");
		context.dispatch(keyDown(Key::Right));
		EXPECT_EQ(idOf(context.drag()->target), "picky");
		context.dispatch(keyDown(Key::Right)); // the wall refuses, so there's nothing that way
		EXPECT_EQ(idOf(context.drag()->target), "picky");
		context.dispatch(keyDown(Key::Tab));
		EXPECT_EQ(idOf(context.drag()->target), "verge");
		EXPECT_TRUE(context.focused().get() == card.get()); // focus never left the card

		context.dispatch(keyDown(Key::Enter));
		EXPECT_EQ(context.drag(), nullptr);
		EXPECT_EQ(
			take(),
			(Journal{
				"start",
				"enter verge",
				"leave verge",
				"enter lane",
				"leave lane",
				"enter picky",
				"leave picky",
				"enter verge",
				"drop verge card",
				"end dropped verge"
			})
		);
	}

	TEST_F(UiDragTest, APadCancelsAKeyDragAndFocusStays) {
		ASSERT_TRUE(context.focus(card));
		context.dispatch(padDown(GamepadButton::A));
		ASSERT_NE(context.drag(), nullptr);
		context.dispatch(padDown(GamepadButton::DpadUp));
		EXPECT_EQ(idOf(context.drag()->target), "lane");
		context.dispatch(padDown(GamepadButton::B));
		EXPECT_EQ(context.drag(), nullptr);
		EXPECT_EQ(context.focused().get(), card.get());
		EXPECT_EQ(take(), (Journal{"start", "enter verge", "leave verge", "enter lane", "leave lane", "end not dropped lane"}));
	}

	TEST_F(UiDragTest, AnActivateTheSourceConsumesPicksNothingUp) {
		card.get()->setOnKeyDown([](KeyEvent& event) {
			if (event.type == KeyEvent::Type::Activate) {
				event.consume();
			}
		});
		ASSERT_TRUE(context.focus(card));
		context.dispatch(keyDown(Key::Enter));
		EXPECT_EQ(context.drag(), nullptr);
	}

	TEST_F(UiDragTest, WithNothingThatAcceptsAKeyDragDropsNowhere) {
		DragSource other = source();
		other.kind = "token";
		card.get()->setDragSource(other);
		for (const Ref<Component>& each : {lane, verge}) {
			each.get()->setDropTarget({.accepts = [](const Drag& drag) { return drag.kind == "card"; }});
		}
		ASSERT_TRUE(context.focus(card));
		context.dispatch(keyDown(Key::Enter));
		ASSERT_NE(context.drag(), nullptr);
		EXPECT_EQ(context.drag()->target.get(), nullptr);
		context.dispatch(keyDown(Key::Enter));
		EXPECT_EQ(take(), (Journal{"start", "end not dropped none"}));
	}

	TEST_F(UiDragTest, APointerPressCancelsAKeyDrag) {
		ASSERT_TRUE(context.focus(card));
		context.dispatch(keyDown(Key::Enter));
		context.dispatch(pointer(InputEvent::Type::PointerDown, kEmpty));
		EXPECT_EQ(context.drag(), nullptr);
	}

	// --- The tree ---

	TEST_F(UiDragTest, TheTreeShowsTheDrag) {
		const auto findNode = [](const nlohmann::json& node, const std::string& id, const auto& self) -> const nlohmann::json* {
			if (node["id"] == id) {
				return &node;
			}
			for (const nlohmann::json& child : node["children"]) {
				if (const nlohmann::json* found = self(child, id, self)) {
					return found;
				}
			}
			return nullptr;
		};
		nlohmann::json tree = nlohmann::json::parse(treeJson(&context, "drag", context.getViewport()));
		EXPECT_TRUE(tree["drag"].is_null());
		const nlohmann::json* cardNode = findNode(tree["roots"][0], "card", findNode);
		ASSERT_NE(cardNode, nullptr);
		EXPECT_EQ((*cardNode)["dragSource"], "card");
		EXPECT_EQ((*cardNode)["state"]["dragging"], false);
		EXPECT_TRUE((*cardNode)["state"]["dropActive"].is_null());
		EXPECT_TRUE(findNode(tree["roots"][0], "lane", findNode)->at("state")["dropAccepts"].is_null());

		pickUpAt(kLane);
		context.layout();
		tree = nlohmann::json::parse(treeJson(&context, "drag", context.getViewport()));
		const nlohmann::json& drag = tree["drag"];
		EXPECT_EQ(drag["via"], "pointer");
		EXPECT_EQ(drag["kind"], "card");
		EXPECT_EQ(drag["source"]["id"], "card");
		EXPECT_EQ(drag["ghost"]["id"], "card");
		EXPECT_EQ(drag["target"]["id"], "lane");
		EXPECT_EQ(drag["accepted"], true);
		EXPECT_EQ(drag["at"]["x"], kLane.x);
		const nlohmann::json& roots = tree["roots"][0];
		EXPECT_EQ(findNode(roots, "card", findNode)->at("state")["dragging"], true);
		EXPECT_EQ(findNode(roots, "card", findNode)->at("slot"), kDragSlot);
		EXPECT_EQ(findNode(roots, "lane", findNode)->at("state")["dropActive"], true);
		EXPECT_EQ(findNode(roots, "picky", findNode)->at("state")["dropAccepts"], true);
		EXPECT_EQ(findNode(roots, "wall", findNode)->at("state")["dropAccepts"], false);
		EXPECT_EQ(findNode(roots, "wall", findNode)->at("dropTarget"), true);
		EXPECT_TRUE(findNode(roots, "road", findNode)->at("state")["dropActive"].is_null());
	}

} // namespace ui
