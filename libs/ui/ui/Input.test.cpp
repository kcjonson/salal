// Pointer dispatch: the hit winner is the topmost painted component, bubbling, the click's target,
// enter and leave, capture, the wheel and the scrollbar. Headless; events go straight to a context.

#include "ui/Context.h"
#include "ui/shapes/Rectangle.h"

#include <gtest/gtest.h>

#include <string>
#include <utility>
#include <vector>

namespace ui {

	namespace {

		constexpr Foundation::Vec2 kViewport{800.0F, 600.0F};

		using foundation::InputEvent;

		InputEvent pointerAt(InputEvent::Type type, Foundation::Vec2 at, double time = 0.0) {
			return {.type = type, .time = time, .mouseButton = foundation::MouseButton::Left, .position = at};
		}

		InputEvent wheelBy(float notches, double time) {
			return {.type = InputEvent::Type::Scroll, .time = time, .scroll = {0.0F, -notches}};
		}

		Component::Args box(const char* id, float x, float y, float width, float height) {
			return {.id = id, .position = {x, y}, .size = {width, height}};
		}

		std::string idAt(Context& context, Foundation::Vec2 point) {
			context.layout();
			const Component* hit = context.find(context.hitTest(point));
			return hit != nullptr ? hit->getId() : std::string("none");
		}

		// Records what reaches it as "<id> <event>", and from which target.
		class Journal {
		  public:
			std::function<void(PointerEvent&)> record(const char* what) {
				return [this, what](PointerEvent& event) {
					entries.push_back(
						event.currentTarget->getId() + " " + what +
						(event.target != event.currentTarget ? " from " + event.target->getId() : "")
					);
				};
			}
			std::vector<std::string> take() { return std::exchange(entries, {}); }

		  private:
			std::vector<std::string> entries;
		};

		const auto grab = [](PointerEvent& event) {
			event.currentTarget->capturePointer();
		};

		// A cancel reaches a component's handlePointer, not a callback.
		class CancelSpy : public Rectangle {
		  public:
			int cancels = 0;

		  protected:
			void handlePointer(PointerEvent& event) override {
				if (event.type == PointerEvent::Type::Cancel) {
					++cancels;
				}
			}
		};

		class UiInputTest : public ::testing::Test {
		  protected:
			Context context{kViewport};
			Journal journal;
		};

	} // namespace

	// --- The hit winner is the topmost painted component ---

	TEST_F(UiInputTest, TheHitIsTheTopmostPaintedInEachOrderingCase) {
		Ref<Component> hand = context.root().addChild<Component>(box("hand", 0.0F, 0.0F, 200.0F, 100.0F));
		hand.addChild<Rectangle>(box("card1", 0.0F, 0.0F, 50.0F, 100.0F));
		hand.addChild<Rectangle>({.id = "card2", .position = {150.0F, 0.0F}, .size = {100.0F, 100.0F}, .zIndex = 1});
		context.root().addChild<Rectangle>(box("resources", 200.0F, 0.0F, 100.0F, 100.0F));
		EXPECT_EQ(idAt(context, {220.0F, 50.0F}), "card2");
		EXPECT_EQ(idAt(context, {280.0F, 50.0F}), "resources");

		Ref<Component> board = context.root().addChild<Component>(box("board", 0.0F, 200.0F, 300.0F, 100.0F));
		board.addChild<Rectangle>(box("tile", 0.0F, 0.0F, 100.0F, 100.0F));
		board.addChild<Rectangle>({.id = "target", .position = {100.0F, 0.0F}, .size = {100.0F, 100.0F}, .zIndex = 2});
		context.root().addChild<Rectangle>({.id = "scrim", .position = {0.0F, 200.0F}, .size = {300.0F, 100.0F}, .zIndex = 1});
		EXPECT_EQ(idAt(context, {50.0F, 250.0F}), "scrim");
		EXPECT_EQ(idAt(context, {150.0F, 250.0F}), "target");

		context.root().addChild<Rectangle>({.id = "high", .position = {400.0F, 400.0F}, .size = {100.0F, 100.0F}, .zIndex = 100});
		context.addRoot<Component>({}).addChild<Rectangle>(box("over", 450.0F, 450.0F, 100.0F, 100.0F));
		EXPECT_EQ(idAt(context, {475.0F, 475.0F}), "over");
	}

	TEST_F(UiInputTest, AHitIsInTheBoxHalfOpenThroughItsTransformAndInsideEveryClip) {
		context.root().addChild<Rectangle>(box("square", 10.0F, 10.0F, 20.0F, 20.0F));
		EXPECT_EQ(idAt(context, {10.0F, 10.0F}), "square");
		EXPECT_EQ(idAt(context, {29.9F, 29.9F}), "square");
		EXPECT_EQ(idAt(context, {30.0F, 20.0F}), "none");

		// 40 x 10 turned a quarter about its centre (220, 105) stands 10 x 40.
		context.root().addChild<Rectangle>(
			{.id = "turned", .position = {200.0F, 100.0F}, .size = {40.0F, 10.0F}, .transform = {.rotate = 1.5707963F}}
		);
		EXPECT_EQ(idAt(context, {220.0F, 120.0F}), "turned");
		EXPECT_EQ(idAt(context, {235.0F, 105.0F}), "none");

		Ref<Component> window =
			context.root().addChild<Component>({.position = {400.0F, 0.0F}, .size = {50.0F, 50.0F}, .overflow = Overflow::Clip});
		window.addChild<Rectangle>(box("past", 40.0F, 40.0F, 30.0F, 30.0F));
		EXPECT_EQ(idAt(context, {445.0F, 45.0F}), "past");
		EXPECT_EQ(idAt(context, {460.0F, 60.0F}), "none");
	}

	TEST_F(UiInputTest, PointerEventsDecideWhatCanBeHitAndADisabledComponentStillOccludes) {
		Ref<Component> group = context.root().addChild<Component>(box("group", 0.0F, 0.0F, 100.0F, 100.0F));
		group.addChild<Rectangle>(box("inGroup", 0.0F, 0.0F, 50.0F, 50.0F));
		EXPECT_EQ(idAt(context, {75.0F, 75.0F}), "none"); // a container passes through
		EXPECT_EQ(idAt(context, {25.0F, 25.0F}), "inGroup");

		Ref<Rectangle> unit = context.root().addChild<Rectangle>(
			{.id = "unit", .position = {200.0F, 0.0F}, .size = {100.0F, 100.0F}, .pointerEvents = PointerEvents::Unit}
		);
		unit.addChild<Rectangle>(box("part", 0.0F, 0.0F, 50.0F, 50.0F));
		EXPECT_EQ(idAt(context, {225.0F, 25.0F}), "unit");

		Ref<Rectangle> ghost = context.root().addChild<Rectangle>(
			{.id = "ghost", .position = {400.0F, 0.0F}, .size = {100.0F, 100.0F}, .pointerEvents = PointerEvents::None}
		);
		ghost.addChild<Rectangle>(box("ghostChild", 0.0F, 0.0F, 50.0F, 50.0F));
		EXPECT_EQ(idAt(context, {425.0F, 25.0F}), "none");

		context.root().addChild<Rectangle>(box("under", 600.0F, 0.0F, 100.0F, 100.0F));
		context.root().addChild<Rectangle>({.id = "disabled", .enabled = false, .position = {600.0F, 0.0F}, .size = {100.0F, 100.0F}});
		EXPECT_EQ(idAt(context, {650.0F, 50.0F}), "disabled");
	}

	// --- Bubbling and the click ---

	TEST_F(UiInputTest, AnEventBubblesUntilConsumedAndAClickLandsOnTheCommonAncestor) {
		Ref<Rectangle> card = context.root().addChild<Rectangle>({
			.id = "card",
			.position = {0.0F, 0.0F},
			.size = {200.0F, 100.0F},
			.onClick = journal.record("click"),
			.onPointerDown = journal.record("down"),
		});
		card.addChild<Rectangle>({.id = "left", .size = {100.0F, 100.0F}, .onClick = journal.record("click")});
		card.addChild<Rectangle>(
			{.id = "right", .position = {100.0F, 0.0F}, .size = {100.0F, 100.0F}, .onPointerDown = [](PointerEvent& event) {
				 event.consume();
			 }}
		);

		EXPECT_TRUE(context.dispatch(pointerAt(InputEvent::Type::PointerDown, {50.0F, 50.0F})));
		EXPECT_TRUE(context.dispatch(pointerAt(InputEvent::Type::PointerUp, {52.0F, 50.0F})));
		EXPECT_EQ(journal.take(), (std::vector<std::string>{"card down from left", "left click", "card click from left"}));

		// Pressed on one half, released on the other: the click is the card's.
		context.dispatch(pointerAt(InputEvent::Type::PointerDown, {98.0F, 50.0F}));
		context.dispatch(pointerAt(InputEvent::Type::PointerUp, {103.0F, 50.0F}));
		EXPECT_EQ(journal.take(), (std::vector<std::string>{"card down from left", "card click"}));

		// A consumed press doesn't bubble, and still clicks.
		context.dispatch(pointerAt(InputEvent::Type::PointerDown, {150.0F, 50.0F}));
		context.dispatch(pointerAt(InputEvent::Type::PointerUp, {150.0F, 55.0F}));
		EXPECT_EQ(journal.take(), (std::vector<std::string>{"card click from right"}));

		// A press that travels past the threshold doesn't click.
		context.dispatch(pointerAt(InputEvent::Type::PointerDown, {20.0F, 50.0F}));
		context.dispatch(pointerAt(InputEvent::Type::PointerUp, {40.0F, 50.0F}));
		EXPECT_EQ(journal.take(), (std::vector<std::string>{"card down from left"}));
	}

	TEST_F(UiInputTest, EmptySpaceLeavesTheEventToTheSceneAndAPressKeepsItsOwner) {
		context.root().addChild<Rectangle>(box("button", 0.0F, 0.0F, 100.0F, 100.0F));
		EXPECT_FALSE(context.dispatch(pointerAt(InputEvent::Type::PointerDown, {300.0F, 300.0F})));
		EXPECT_FALSE(context.dispatch(pointerAt(InputEvent::Type::PointerMove, {50.0F, 50.0F}))); // the scene's drag
		EXPECT_FALSE(context.dispatch(pointerAt(InputEvent::Type::PointerUp, {50.0F, 50.0F})));
		EXPECT_TRUE(context.dispatch(pointerAt(InputEvent::Type::PointerMove, {50.0F, 50.0F})));
	}

	// --- Hover ---

	TEST_F(UiInputTest, EnterAndLeaveComeInOrderAndFollowContentScrolledUnderAStillPointer) {
		Ref<Rectangle> outer = context.root().addChild<Rectangle>({
			.id = "outer",
			.size = {200.0F, 100.0F},
			.onPointerEnter = journal.record("enter"),
			.onPointerLeave = journal.record("leave"),
		});
		outer.addChild<Rectangle>(
			{.id = "inner", .size = {100.0F, 100.0F}, .onPointerEnter = journal.record("enter"), .onPointerLeave = journal.record("leave")}
		);

		context.dispatch(pointerAt(InputEvent::Type::PointerMove, {50.0F, 50.0F}));
		EXPECT_EQ(journal.take(), (std::vector<std::string>{"outer enter", "inner enter"}));
		EXPECT_TRUE(outer.get()->isHovered());
		context.dispatch(pointerAt(InputEvent::Type::PointerMove, {500.0F, 50.0F}));
		EXPECT_EQ(journal.take(), (std::vector<std::string>{"inner leave", "outer leave"}));

		Ref<Component> list = context.root().addChild<Component>({
			.position = {0.0F, 200.0F},
			.size = {100.0F, 50.0F},
			.layout = {.display = Display::Flex, .flexDirection = FlexDirection::Column},
			.overflow = Overflow::Scroll,
		});
		for (const char* id : {"row0", "row1", "row2"}) {
			list.addChild<Rectangle>(
				{.id = id, .size = {ui::kAuto, 50.0F}, .onPointerEnter = journal.record("enter"), .onPointerLeave = journal.record("leave")}
			);
		}
		context.dispatch(pointerAt(InputEvent::Type::PointerMove, {20.0F, 220.0F}));
		EXPECT_EQ(journal.take(), (std::vector<std::string>{"row0 enter"}));
		list.get()->scrollBy({0.0F, 50.0F});
		context.layout(); // the pointer hasn't moved
		EXPECT_EQ(journal.take(), (std::vector<std::string>{"row0 leave", "row1 enter"}));
	}

	// --- Capture ---

	TEST_F(UiInputTest, ACaptorGetsEveryEventUntilTheRelease) {
		Ref<Rectangle> knob = context.root().addChild<Rectangle>({
			.id = "knob",
			.size = {20.0F, 20.0F},
			.onPointerDown = grab,
			.onPointerUp = journal.record("up"),
			.onPointerMove = journal.record("move"),
		});
		context.dispatch(pointerAt(InputEvent::Type::PointerDown, {10.0F, 10.0F}));
		EXPECT_TRUE(knob.get()->hasPointerCapture());
		context.dispatch(pointerAt(InputEvent::Type::PointerMove, {300.0F, 300.0F}));
		context.dispatch(pointerAt(InputEvent::Type::PointerUp, {300.0F, 300.0F}));
		EXPECT_EQ(journal.take(), (std::vector<std::string>{"knob move", "knob up"}));
		EXPECT_FALSE(knob.get()->hasPointerCapture());
	}

	TEST_F(UiInputTest, RemovingTheCaptorCancelsItFirst) {
		Ref<Component> holder = context.root().addChild<Component>(box("holder", 0.0F, 0.0F, 400.0F, 400.0F));
		Ref<CancelSpy> spy = holder.addChild<CancelSpy>({.id = "spy", .size = {20.0F, 20.0F}, .onPointerDown = grab});
		context.dispatch(pointerAt(InputEvent::Type::PointerDown, {10.0F, 10.0F}));
		ASSERT_TRUE(spy.get()->hasPointerCapture());
		const Registry::DispatchScope scope(context.getRegistry());
		CancelSpy*					  removed = spy.get();
		holder.get()->removeChild(spy);
		EXPECT_EQ(removed->cancels, 1);
		EXPECT_FALSE(removed->hasPointerCapture());
	}

	// --- The wheel and the scrollbar ---

	TEST_F(UiInputTest, TheWheelScrollsTheInnermostScrollerThatCanMoveAndStaysLatched) {
		Ref<Component> outer = context.root().addChild<Component>({.id = "outer", .size = {200.0F, 200.0F}, .overflow = Overflow::Scroll});
		Ref<Component> inner = outer.addChild<Component>({.id = "inner", .size = {200.0F, 100.0F}, .overflow = Overflow::Scroll});
		inner.addChild<Rectangle>(box("innerContent", 0.0F, 0.0F, 200.0F, 150.0F));
		outer.addChild<Rectangle>(box("outerContent", 0.0F, 100.0F, 200.0F, 400.0F));
		context.layout();
		ASSERT_EQ(inner.get()->getScrollMax(), Foundation::Vec2(0.0F, 50.0F));
		ASSERT_EQ(outer.get()->getScrollMax(), Foundation::Vec2(0.0F, 300.0F));

		context.dispatch(pointerAt(InputEvent::Type::PointerMove, {50.0F, 50.0F}));
		context.dispatch(wheelBy(1.0F, 1.0));
		EXPECT_EQ(inner.get()->getScroll().y, 48.0F); // a notch is 48 px
		context.dispatch(wheelBy(1.0F, 1.1));		  // latched: it stays with inner at its end
		EXPECT_EQ(inner.get()->getScroll().y, 50.0F);
		EXPECT_EQ(outer.get()->getScroll().y, 0.0F);
		context.dispatch(wheelBy(1.0F, 2.0)); // a new sequence goes to the scroller that can move
		EXPECT_EQ(outer.get()->getScroll().y, 48.0F);
	}

	TEST_F(UiInputTest, AThumbDragScrollsAndTheTrackPages) {
		Ref<Component> list = context.root().addChild<Component>({.size = {100.0F, 100.0F}, .overflow = Overflow::Scroll});
		list.addChild<Rectangle>(box("tall", 0.0F, 0.0F, 100.0F, 400.0F));
		context.layout();
		bool			  onBar = false;
		const ComponentId hit = context.hitTest({95.0F, 10.0F}, &onBar);
		EXPECT_TRUE(onBar);
		EXPECT_EQ(hit, list.componentId());

		// The thumb is 24 px of a 96 px track (100 of 400 is under the minimum), so 72 px of travel.
		context.dispatch(pointerAt(InputEvent::Type::PointerDown, {95.0F, 10.0F}));
		context.dispatch(pointerAt(InputEvent::Type::PointerMove, {95.0F, 46.0F}));
		EXPECT_FLOAT_EQ(list.get()->getScroll().y, 150.0F);
		context.dispatch(pointerAt(InputEvent::Type::PointerUp, {95.0F, 46.0F}));

		list.get()->scrollTo({0.0F, 0.0F});
		context.layout();
		context.dispatch(pointerAt(InputEvent::Type::PointerDown, {95.0F, 90.0F}));
		context.dispatch(pointerAt(InputEvent::Type::PointerUp, {95.0F, 90.0F}));
		EXPECT_FLOAT_EQ(list.get()->getScroll().y, 84.0F); // a page: the view less 16
	}

	TEST_F(UiInputTest, TheUiScaleScalesTheViewportAndThePointer) {
		context.setScale(2.0F);
		context.root().addChild<Rectangle>(box("button", 40.0F, 40.0F, 20.0F, 20.0F));
		EXPECT_EQ(context.getViewport(), Foundation::Vec2(400.0F, 300.0F));
		EXPECT_TRUE(context.dispatch(pointerAt(InputEvent::Type::PointerMove, {100.0F, 100.0F})));
		EXPECT_FALSE(context.dispatch(pointerAt(InputEvent::Type::PointerMove, {50.0F, 50.0F})));
	}

} // namespace ui
