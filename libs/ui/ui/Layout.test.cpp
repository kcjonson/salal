// The layout pass: Flex's sizing and placement, Block's free placement, absolute children, and
// invalidation to the relayout boundary. Ported from worldsim's LayoutContainer.test.cpp (a4226e17)
// where its cases still apply, with the spec's 10.9 cases; the two freeze tests come first. A fake
// text with fixed word widths stands in for fonts, so it runs headless.

#include "ui/Context.h"
#include "ui/debug/LayoutLint.h"
#include "ui/debug/TreeSnapshot.h"

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace ui {

	namespace {

		constexpr Foundation::Vec2 kViewport{800.0F, 600.0F};
		constexpr float			   kWord = 40.0F; // each fake word's width, no spaces
		constexpr float			   kLine = 20.0F;

		// Words of one width that wrap to whatever width it's given, as a wrapping Text does.
		class FakeText : public Component {
		  public:
			struct Args {
				UI_COMMON_ARGS
				int words = 1;
			};

			explicit FakeText(const Args& args)
				: words(args.words) {}

			void setWords(int count) {
				words = count;
				contentChanged();
			}

		  protected:
			std::optional<Foundation::Vec2> intrinsicSize(std::optional<float> width, float availableWidth) const override {
				std::optional<float> wrapAt = width;
				if (!wrapAt && std::isfinite(availableWidth)) {
					wrapAt = availableWidth;
				}
				const int perLine = wrapAt ? std::max(1, static_cast<int>(*wrapAt / kWord)) : words;
				const int lines = (words + perLine - 1) / perLine;
				return Foundation::Vec2{
					width.value_or(static_cast<float>(std::min(words, perLine)) * kWord), static_cast<float>(lines) * kLine
				};
			}
			float minContentWidth() const override { return kWord; }

		  private:
			int words;
		};

		Component::Args box(float width, float height) {
			return {.size = {width, height}};
		}

		Layout column(float gap = 0.0F, Edges padding = {}) {
			return {.display = Display::Flex, .flexDirection = FlexDirection::Column, .gap = gap, .padding = padding};
		}

		Layout row(float gap = 0.0F, Edges padding = {}) {
			return {.display = Display::Flex, .flexDirection = FlexDirection::Row, .gap = gap, .padding = padding};
		}

		using Box = std::array<float, 4>; // x, y, w, h: Foundation::Rect has no ==

		Box xywh(const Foundation::Rect& rect) {
			return {rect.x, rect.y, rect.width, rect.height};
		}

		Box boundsOf(const Ref<Component>& ref) {
			return xywh(ref.get()->getBounds());
		}

		class UiLayoutTest : public ::testing::Test {
		  protected:
			Context context{kViewport};
		};

	} // namespace

	// --- An assigned size never freezes an auto one (gap 15, R10.5) ---

	TEST_F(UiLayoutTest, ANestedHugContainerGrowsWhenItsTextGrowsAfterTheFirstLayout) {
		Ref<Component> outer = context.root().addChild<Component>({.layout = column()});
		Ref<Component> inner = outer.addChild<Component>({.layout = row()});
		Ref<FakeText>  text = inner.addChild<FakeText>({.words = 2});
		context.layout();
		EXPECT_FLOAT_EQ(inner.get()->getSize().x, 2.0F * kWord);

		text.get()->setWords(5);
		context.layout();
		EXPECT_FLOAT_EQ(inner.get()->getSize().x, 5.0F * kWord);
		EXPECT_FLOAT_EQ(outer.get()->getSize().x, 5.0F * kWord);
	}

	TEST_F(UiLayoutTest, InvalidationReachesAHugAncestorAndStopsAtAFixedBoundary) {
		Ref<Component> fixed = context.root().addChild<Component>({.size = {300.0F, 200.0F}, .layout = column()});
		Ref<Component> hug = fixed.addChild<Component>({.layout = column()});
		Ref<FakeText>  text = hug.addChild<FakeText>({.words = 3});
		context.layout();
		const Box fixedBefore = boundsOf(fixed);
		EXPECT_FLOAT_EQ(hug.get()->getSize().y, kLine);

		// 12 words at the 300 px the column stretches it to are seven to a line: two lines.
		text.get()->setWords(12);
		context.layout();
		EXPECT_FLOAT_EQ(hug.get()->getSize().y, 2.0F * kLine);
		EXPECT_EQ(boundsOf(fixed), fixedBefore);
	}

	// --- Flex ---

	TEST_F(UiLayoutTest, AColumnStacksChildrenWithGapPaddingAndMarginsAndHugsThem) {
		Ref<Component> stack = context.root().addChild<Component>({.position = {10.0F, 20.0F}, .layout = column(5.0F, {8.0F, 4.0F})});
		Ref<Component> a = stack.addChild<Component>(box(50.0F, 30.0F));
		Ref<Component> b = stack.addChild<Component>({.size = {40.0F, 20.0F}, .margin = 2.0F});
		context.layout();

		EXPECT_EQ(boundsOf(a), (Box{4.0F, 8.0F, 50.0F, 30.0F}));
		EXPECT_EQ(boundsOf(b), (Box{4.0F, 43.0F, 44.0F, 24.0F})); // the margin box
		EXPECT_EQ(xywh(b.get()->screenBounds()), (Box{16.0F, 65.0F, 40.0F, 20.0F}));
		// Width: the widest margin box and the padding; height: both, the gap and the padding.
		EXPECT_EQ(stack.get()->getSize(), Foundation::Vec2(58.0F, 8.0F + 30.0F + 5.0F + 24.0F + 8.0F));
	}

	TEST_F(UiLayoutTest, JustifyContentDistributesTheLeftoverOnTopOfTheGap) {
		struct Case {
			JustifyContent	   justify;
			std::vector<float> xs;
		};
		// 300 wide, three 50 px children, gap 10: a leftover of 130.
		const std::vector<Case> cases{
			{JustifyContent::Start, {0.0F, 60.0F, 120.0F}},
			{JustifyContent::Center, {65.0F, 125.0F, 185.0F}},
			{JustifyContent::End, {130.0F, 190.0F, 250.0F}},
			{JustifyContent::SpaceBetween, {0.0F, 125.0F, 250.0F}},
			{JustifyContent::SpaceAround, {130.0F / 6.0F, 130.0F / 6.0F + 60.0F + 130.0F / 3.0F, 130.0F / 6.0F + 120.0F + 260.0F / 3.0F}},
			{JustifyContent::SpaceEvenly, {32.5F, 125.0F, 217.5F}},
		};
		for (const Case& test : cases) {
			Context fresh(kViewport);
			Layout	layout = row(10.0F);
			layout.justifyContent = test.justify;
			Ref<Component>				stack = fresh.root().addChild<Component>({.size = {300.0F, 50.0F}, .layout = layout});
			std::vector<Ref<Component>> children;
			for (int i = 0; i < 3; ++i) {
				children.push_back(stack.addChild<Component>(box(50.0F, 50.0F)));
			}
			fresh.layout();
			for (std::size_t i = 0; i < 3; ++i) {
				EXPECT_NEAR(children[i].get()->getBounds().x, test.xs[i], 1e-3F) << static_cast<int>(test.justify) << " child " << i;
			}
		}
	}

	TEST_F(UiLayoutTest, StretchFillsAnAutoCrossSizeAndLeavesAFixedOneAtTheStart) {
		Ref<Component> stack = context.root().addChild<Component>({.size = {200.0F, 100.0F}, .layout = row()});
		Ref<Component> autoCross = stack.addChild<Component>({.size = {40.0F, kAuto}, .margin = {5.0F, 0.0F}});
		Ref<Component> fixedCross = stack.addChild<Component>(box(40.0F, 30.0F));
		Ref<Component> centred = stack.addChild<Component>({.size = {40.0F, 30.0F}, .layout = {.alignSelf = AlignItems::Center}});
		context.layout();
		EXPECT_EQ(autoCross.get()->getSize(), Foundation::Vec2(40.0F, 90.0F));
		EXPECT_EQ(boundsOf(fixedCross)[1], 0.0F);
		EXPECT_EQ(boundsOf(centred)[1], 35.0F);
	}

	TEST_F(UiLayoutTest, FlexGrowSplitsTheLeftoverByWeightNotTheContainer) {
		Ref<Component>			 stack = context.root().addChild<Component>({.size = {420.0F, 50.0F}, .layout = row(10.0F, {0.0F, 10.0F})});
		const std::vector<float> weights{25.0F, 40.0F, 35.0F};
		std::vector<Ref<Component>> children;
		for (const float weight : weights) {
			children.push_back(stack.addChild<Component>({.size = {0.0F, 50.0F}, .layout = {.flexGrow = weight}}));
		}
		context.layout();
		// 420 less 20 of padding and 20 of gaps leaves 380.
		EXPECT_FLOAT_EQ(children[0].get()->getSize().x, 95.0F);
		EXPECT_FLOAT_EQ(children[1].get()->getSize().x, 152.0F);
		EXPECT_FLOAT_EQ(children[2].get()->getSize().x, 133.0F);
		EXPECT_FLOAT_EQ(boundsOf(children[2])[0], 10.0F + 95.0F + 10.0F + 152.0F + 10.0F);
	}

	TEST_F(UiLayoutTest, AMaxSizeOnAGrowingChildReturnsTheExcessToItsSiblings) {
		Ref<Component> stack = context.root().addChild<Component>({.size = {300.0F, 50.0F}, .layout = row()});
		Ref<Component> capped = stack.addChild<Component>({.size = {0.0F, 50.0F}, .maxSize = {50.0F, kAuto}, .layout = {.flexGrow = 1.0F}});
		Ref<Component> other = stack.addChild<Component>({.size = {0.0F, 50.0F}, .layout = {.flexGrow = 1.0F}});
		context.layout();
		EXPECT_FLOAT_EQ(capped.get()->getSize().x, 50.0F);
		EXPECT_FLOAT_EQ(other.get()->getSize().x, 250.0F);
	}

	TEST_F(UiLayoutTest, AnOverflowingRowShrinksByBasisAndATextKeepsItsLongestWord) {
		Ref<Component> stack = context.root().addChild<Component>({.size = {120.0F, 100.0F}, .layout = row()});
		Ref<Component> wide = stack.addChild<Component>(box(150.0F, 20.0F));
		Ref<Component> narrow = stack.addChild<Component>(box(50.0F, 20.0F));
		context.layout();
		// 80 too many, taken 3:1 by basis.
		EXPECT_FLOAT_EQ(wide.get()->getSize().x, 90.0F);
		EXPECT_FLOAT_EQ(narrow.get()->getSize().x, 30.0F);

		Context		   crowded(kViewport);
		Ref<Component> line = crowded.root().addChild<Component>({
			.size = {60.0F, 100.0F},
			.layout = {.display = Display::Flex, .flexDirection = FlexDirection::Row, .alignItems = AlignItems::Start},
			.overflow = Overflow::Clip,
		});
		line.addChild<Component>(box(200.0F, 20.0F));
		Ref<FakeText> text = line.addChild<FakeText>({.words = 4});
		crowded.layout();
		EXPECT_FLOAT_EQ(text.get()->getSize().x, kWord); // its longest word, so the row overflows
		EXPECT_FLOAT_EQ(text.get()->getSize().y, 4.0F * kLine);
	}

	TEST_F(UiLayoutTest, AHugTextInAStartAlignedFixedColumnWrapsAtTheColumnsWidth) {
		Ref<Component> stack = context.root().addChild<Component>(
			{.size = {100.0F, 300.0F},
			 .layout = {
				 .display = Display::Flex,
				 .flexDirection = FlexDirection::Column,
				 .alignItems = AlignItems::Start,
			 }}
		);
		Ref<FakeText> text = stack.addChild<FakeText>({.words = 5});
		context.layout();
		EXPECT_FLOAT_EQ(text.get()->getSize().x, 2.0F * kWord);
		EXPECT_FLOAT_EQ(text.get()->getSize().y, 3.0F * kLine);
	}

	TEST_F(UiLayoutTest, OnOverflowEveryDistributionStartsAndChildrenRunPastTheEnd) {
		Layout layout = row();
		layout.justifyContent = JustifyContent::Center;
		layout.alignItems = AlignItems::Center;
		Ref<Component> stack = context.root().addChild<Component>({.size = {100.0F, 20.0F}, .layout = layout});
		Ref<Component> first = stack.addChild<Component>({.size = {80.0F, 40.0F}, .layout = {.flexShrink = 0.0F}});
		Ref<Component> second = stack.addChild<Component>({.size = {80.0F, 40.0F}, .layout = {.flexShrink = 0.0F}});
		context.layout();
		EXPECT_EQ(boundsOf(first), (Box{0.0F, 0.0F, 80.0F, 40.0F}));
		EXPECT_EQ(boundsOf(second)[0], 80.0F);
	}

	// --- Block, None and absolute children ---

	TEST_F(UiLayoutTest, ABlockPlacesChildrenWhereTheySayAndHugsTheirBoxes) {
		Ref<Component> group = context.root().addChild<Component>({.position = {50.0F, 50.0F}});
		group.addChild<Component>({.position = {10.0F, 0.0F}, .size = {30.0F, 30.0F}});
		Ref<Component> far = group.addChild<Component>({.position = {100.0F, 40.0F}, .size = {20.0F, 10.0F}, .margin = 5.0F});
		Ref<Component> half =
			group.addChild<Component>({.size = {percent(50.0F), percent(100.0F)}, .layout = {.position = Position::Absolute}});
		context.layout();
		EXPECT_EQ(group.get()->getSize(), Foundation::Vec2(130.0F, 60.0F));
		EXPECT_EQ(xywh(far.get()->screenBounds()), (Box{155.0F, 95.0F, 20.0F, 10.0F}));
		EXPECT_EQ(half.get()->getSize(), Foundation::Vec2(65.0F, 60.0F)); // out of the hug, a share of it
	}

	TEST_F(UiLayoutTest, AnInsetAnchorsAnAbsoluteChildToTheCornerWhateverTheParentsSize) {
		for (const float width : {200.0F, 500.0F}) {
			Context		   fresh(kViewport);
			Ref<Component> card = fresh.root().addChild<Component>({.size = {width, 100.0F}, .layout = row()});
			card.addChild<Component>(box(40.0F, 40.0F));
			Ref<Component> badge = card.addChild<Component>(
				{.size = {20.0F, 20.0F}, .layout = {.position = Position::Absolute, .inset = {.top = 4.0F, .right = 4.0F}}}
			);
			Ref<Component> band = card.addChild<Component>(
				{.size = {kAuto, 10.0F},
				 .layout = {.position = Position::Absolute, .inset = {.right = 10.0F, .bottom = 0.0F, .left = 10.0F}}}
			);
			fresh.layout();
			EXPECT_EQ(boundsOf(badge), (Box{width - 24.0F, 4.0F, 20.0F, 20.0F}));
			EXPECT_EQ(boundsOf(band), (Box{10.0F, 90.0F, width - 20.0F, 10.0F}));
		}
	}

	TEST_F(UiLayoutTest, DisplayNoneTakesNoRoomAndNoPlaceInThePaintOrder) {
		Ref<Component> stack = context.root().addChild<Component>({.layout = column()});
		stack.addChild<Component>(box(30.0F, 30.0F));
		Ref<Component> hidden = stack.addChild<Component>({.size = {30.0F, 30.0F}, .layout = {.display = Display::None}});
		Ref<Component> last = stack.addChild<Component>(box(30.0F, 30.0F));
		context.layout();
		EXPECT_EQ(boundsOf(last)[1], 30.0F);
		EXPECT_EQ(std::ranges::count(context.paintOrder(), hidden.componentId()), 0);
	}

	// --- Boundaries, resizes, callbacks and clips ---

	TEST_F(UiLayoutTest, AViewportResizeLaysAFullViewportStackOutAgain) {
		context.root().get()->setLayout(column());
		Ref<Component> header = context.root().addChild<Component>({.size = {kAuto, 40.0F}});
		Ref<Component> body = context.root().addChild<Component>({.layout = {.flexGrow = 1.0F}});
		context.layout();
		EXPECT_EQ(body.get()->getSize(), Foundation::Vec2(800.0F, 560.0F));
		context.setViewport({1024.0F, 768.0F});
		context.layout();
		EXPECT_EQ(header.get()->getSize(), Foundation::Vec2(1024.0F, 40.0F));
		EXPECT_EQ(body.get()->getSize(), Foundation::Vec2(1024.0F, 728.0F));
	}

	TEST_F(UiLayoutTest, OnLayoutRunsForWhatMovedAndNothingElse) {
		int			   moves = 0;
		Ref<Component> stack = context.root().addChild<Component>({.layout = column()});
		stack.addChild<Component>({.size = {10.0F, 10.0F}, .onLayout = [&](Component& /*moved*/) { ++moves; }});
		Ref<Component> second = stack.addChild<Component>({.size = {10.0F, 10.0F}, .onLayout = [&](Component& /*moved*/) { ++moves; }});
		context.layout();
		EXPECT_EQ(moves, 2);
		context.layout();
		EXPECT_EQ(moves, 2);
		stack.addChild<Component>(box(10.0F, 10.0F));
		context.layout();
		EXPECT_EQ(moves, 2); // added after both, it moves neither
		second.get()->setSize(Foundation::Vec2{20.0F, 10.0F});
		context.layout();
		EXPECT_EQ(moves, 3);
	}

	TEST_F(UiLayoutTest, AClippingParentsBoxIsItsChildrensClip) {
		Ref<Component> panel =
			context.root().addChild<Component>({.position = {100.0F, 100.0F}, .size = {50.0F, 50.0F}, .overflow = Overflow::Clip});
		Ref<Component> inside = panel.addChild<Component>({.position = {40.0F, 40.0F}, .size = {30.0F, 30.0F}});
		context.layout();
		ASSERT_TRUE(inside.get()->getClip().has_value());
		EXPECT_TRUE(lint(context, kViewport).clean()); // a clipping parent declares the overrun
		EXPECT_EQ(xywh(*inside.get()->getClip()), (Box{100.0F, 100.0F, 50.0F, 50.0F}));
		EXPECT_FALSE(panel.get()->getClip().has_value());
	}

	TEST_F(UiLayoutTest, ANegativeGapOverlapsSiblingsAndTheLintAcceptsIt) {
		Ref<Component> stack = context.root().addChild<Component>({.layout = row(-10.0F)});
		stack.addChild<Component>(box(40.0F, 40.0F));
		Ref<Component> second = stack.addChild<Component>(box(40.0F, 40.0F));
		context.layout();
		EXPECT_EQ(boundsOf(second)[0], 30.0F);
		EXPECT_TRUE(lint(context, kViewport).clean());
	}

} // namespace ui
