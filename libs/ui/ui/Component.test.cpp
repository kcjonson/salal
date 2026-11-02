// The tree and its storage: building through addChild, the slot map's ids, stale refs, and removal
// inside a dispatch. Headless; nothing draws.

#include "ui/Context.h"
#include "ui/shapes/Rectangle.h"

#include <gtest/gtest.h>

#include <vector>

namespace ui {

	namespace {

		constexpr Foundation::Vec2 kViewport{800.0F, 600.0F};

		// Counts the lives of every Tracked, so a test can see when the registry destroys one.
		int& liveTracked() {
			static int count = 0;
			return count;
		}

		class Tracked : public Component {
		  public:
			struct Args {
				UI_COMMON_ARGS
				int children = 0;
			};

			explicit Tracked(const Args& args)
				: childCount(args.children) {
				++liveTracked();
			}
			~Tracked() override { --liveTracked(); }

			[[nodiscard]] const char* typeName() const override { return "Tracked"; }

			int						  childCount;
			std::vector<Ref<Tracked>> built;
			Foundation::Vec2		  sizeSeenInMount{-1.0F, -1.0F};

		  protected:
			void onMount() override {
				sizeSeenInMount = size;
				for (int i = 0; i < childCount; ++i) {
					built.push_back(addChild<Tracked>({.size = {10.0F, 10.0F}}));
				}
			}
		};

		// Adds a child in its constructor, which it can't.
		class EagerChild : public Component {
		  public:
			EagerChild() { early = addChild<Rectangle>({}); }
			Ref<Rectangle> early;
		};

		class UiTreeTest : public ::testing::Test {
		  protected:
			void SetUp() override { liveTracked() = 0; }

			Context context{kViewport};
		};

	} // namespace

	TEST_F(UiTreeTest, AddChildBuildsFromArgsAndMountsAfterTheCommonAttributes) {
		Ref<Tracked> parent = context.root().addChild<Tracked>({.id = "parent", .size = {100.0F, 50.0F}, .children = 2});
		Tracked*	 built = parent.get();
		ASSERT_NE(built, nullptr);
		EXPECT_EQ(built->getId(), "parent");
		EXPECT_EQ(built->sizeSeenInMount, Foundation::Vec2(100.0F, 50.0F));
		ASSERT_EQ(built->built.size(), 2U);
		EXPECT_EQ(built->getChildren().size(), 2U);
		EXPECT_EQ(built->built[0].get()->parent().get(), built);
		EXPECT_EQ(built->parent().get(), context.root().get());
		EXPECT_EQ(liveTracked(), 3);
		EXPECT_EQ(context.getRegistry().size(), 4U); // the root too
	}

	TEST_F(UiTreeTest, AChildAddedInAConstructorIsRefusedAndNothingIsFiled) {
		Ref<EagerChild> eager = context.root().addChild<EagerChild>({});
		ASSERT_NE(eager.get(), nullptr);
		EXPECT_EQ(eager.get()->early.get(), nullptr);
		EXPECT_TRUE(eager.get()->getChildren().empty());
	}

	TEST_F(UiTreeTest, RootsTakeTheViewportAndFollowIt) {
		Ref<Component> second = context.addRoot<Component>({.position = {5.0F, 5.0F}, .size = {1.0F, 1.0F}});
		EXPECT_EQ(context.root().get()->getSize(), kViewport);
		EXPECT_EQ(second.get()->getPosition(), Foundation::Vec2(0.0F, 0.0F));
		EXPECT_EQ(second.get()->getSize(), kViewport);
		EXPECT_TRUE(second.get()->getIsolation());
		context.setViewport({1024.0F, 768.0F});
		EXPECT_EQ(context.root().get()->getSize(), Foundation::Vec2(1024.0F, 768.0F));
		EXPECT_EQ(second.get()->getSize(), Foundation::Vec2(1024.0F, 768.0F));
		EXPECT_EQ(context.getRoots().size(), 2U);
	}

	TEST_F(UiTreeTest, RemovingASubtreeStalesEveryRefToItAtOnce) {
		Ref<Tracked> row = context.root().addChild<Tracked>({.children = 2});
		Ref<Tracked> cell = row.get()->built[1];
		context.root().get()->removeChild(row);
		EXPECT_EQ(row.get(), nullptr);
		EXPECT_EQ(cell.get(), nullptr);
		EXPECT_TRUE(context.root().get()->getChildren().empty());
		EXPECT_EQ(liveTracked(), 0); // outside a dispatch they go at once
		EXPECT_EQ(context.getRegistry().size(), 1U);
	}

	TEST_F(UiTreeTest, AFreedSlotIsReusedUnderANewGeneration) {
		Ref<Tracked>	  first = context.root().addChild<Tracked>({});
		const ComponentId firstId = first.componentId();
		context.root().get()->removeChild(first);
		Ref<Tracked> second = context.root().addChild<Tracked>({});
		EXPECT_EQ(second.componentId().index, firstId.index);
		EXPECT_NE(second.componentId().gen, firstId.gen);
		EXPECT_EQ(first.get(), nullptr);
		EXPECT_NE(second.get(), nullptr);
	}

	TEST_F(UiTreeTest, ARemovalInsideADispatchKeepsTheComponentAliveUntilItEnds) {
		Ref<Tracked> row = context.root().addChild<Tracked>({.children = 1});
		Tracked*	 cell = row.get()->built[0].get();
		{
			const Registry::DispatchScope outer(context.getRegistry());
			{
				const Registry::DispatchScope inner(context.getRegistry());
				// The cell's handler removes its own parent, and with it the cell.
				context.root().get()->removeChild(cell->parent());
				EXPECT_EQ(row.get(), nullptr);
				EXPECT_EQ(cell->self().get(), nullptr);
				EXPECT_EQ(cell->childCount, 0); // `this` is still good for the rest of the handler
			}
			EXPECT_EQ(liveTracked(), 2); // the inner scope isn't the dispatch
		}
		EXPECT_EQ(liveTracked(), 0);
	}

	TEST_F(UiTreeTest, RemoveChildOfANonChildIsIgnored) {
		Ref<Tracked> a = context.root().addChild<Tracked>({.children = 1});
		Ref<Tracked> b = context.root().addChild<Tracked>({});
		b.get()->removeChild(a.get()->built[0]);
		EXPECT_NE(a.get()->built[0].get(), nullptr);
		EXPECT_EQ(liveTracked(), 3);
	}

	TEST_F(UiTreeTest, AddChildThroughAStaleRefAddsNothing) {
		Ref<Tracked> gone = context.root().addChild<Tracked>({});
		context.root().get()->removeChild(gone);
		EXPECT_EQ(gone.addChild<Tracked>({}).get(), nullptr);
		EXPECT_EQ(liveTracked(), 0);
	}

	TEST_F(UiTreeTest, ARemovedRootTakesItsSubtree) {
		Ref<Component> overlay = context.addRoot<Component>({});
		Ref<Tracked>   inside = overlay.addChild<Tracked>({.children = 3});
		context.removeRoot(overlay);
		EXPECT_EQ(inside.get(), nullptr);
		EXPECT_EQ(liveTracked(), 0);
		EXPECT_EQ(context.getRoots().size(), 0U);
	}

	TEST_F(UiTreeTest, TheContextDestroysEverythingItHolds) {
		{
			Context scoped(kViewport);
			scoped.root().addChild<Tracked>({.children = 4});
			EXPECT_EQ(liveTracked(), 5);
		}
		EXPECT_EQ(liveTracked(), 0);
	}

	TEST_F(UiTreeTest, ScreenMapsComposeParentPositionsAndTransforms) {
		Ref<Component> panel = context.root().addChild<Component>({.position = {100.0F, 50.0F}, .size = {200.0F, 100.0F}});
		Ref<Component> badge = panel.addChild<Component>({.position = {10.0F, 20.0F}, .size = {30.0F, 10.0F}});
		Ref<Component> spun = panel.addChild<Component>({
			.position = {0.0F, 0.0F},
			.size = {20.0F, 10.0F},
			.transform = {.rotate = 1.5707963F},
		});
		context.layout();
		const Foundation::Rect badgeScreen = badge.get()->screenBounds();
		EXPECT_FLOAT_EQ(badgeScreen.x, 110.0F);
		EXPECT_FLOAT_EQ(badgeScreen.y, 70.0F);
		EXPECT_FLOAT_EQ(badgeScreen.width, 30.0F);
		const Foundation::Vec2 back = badge.get()->screenToLocal({115.0F, 75.0F});
		EXPECT_FLOAT_EQ(back.x, 5.0F);
		EXPECT_FLOAT_EQ(back.y, 5.0F);
		// A quarter turn about its centre (110, 55): the 20 x 10 box stands 10 x 20.
		const Foundation::Rect turned = spun.get()->screenBounds();
		EXPECT_NEAR(turned.x, 105.0F, 1e-3F);
		EXPECT_NEAR(turned.y, 45.0F, 1e-3F);
		EXPECT_NEAR(turned.width, 10.0F, 1e-3F);
		EXPECT_NEAR(turned.height, 20.0F, 1e-3F);
		const Foundation::Vec2 corner = spun.get()->localToScreen({0.0F, 0.0F});
		EXPECT_NEAR(corner.x, 115.0F, 1e-3F);
		EXPECT_NEAR(corner.y, 45.0F, 1e-3F);
	}

	TEST(UiStyleTest, ExtendLaysTheOverridesSetFieldsOnTop) {
		const Style pane{.backgroundColor = Foundation::Color::black(), .borderWidth = 1.0F, .fontSize = 14.0F};
		const Style extended = pane.extend({.backgroundColor = Foundation::Color::red(), .opacity = 0.5F});
		EXPECT_EQ(extended.backgroundColor->r, 1.0F);
		EXPECT_EQ(extended.borderWidth, 1.0F);
		EXPECT_EQ(extended.fontSize, 14.0F);
		EXPECT_EQ(extended.opacity, 0.5F);
		EXPECT_FALSE(extended.borderColor.has_value());
	}

} // namespace ui
