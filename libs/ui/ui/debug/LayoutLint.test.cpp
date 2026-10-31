// The lint's four rules, ported from worldsim's libs/ui/debug/LayoutLint.test.cpp (a4226e17) onto
// parent-local boxes under viewport-sized roots.

#include "ui/debug/LayoutLint.h"

#include "ui/Context.h"

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

namespace ui {

	namespace {

		constexpr Foundation::Vec2 kViewport{800.0F, 600.0F};

		Component::Args at(float x, float y, float width, float height) {
			return {.position = {x, y}, .size = {width, height}};
		}

		class UiLintTest : public ::testing::Test {
		  protected:
			LintResult run() {
				context.layout();
				return lint(context, kViewport);
			}

			Context context{kViewport};
		};

	} // namespace

	TEST_F(UiLintTest, CleanLayoutHasNoViolations) {
		Ref<Component> parent = context.root().addChild<Component>(at(10.0F, 10.0F, 200.0F, 300.0F));
		parent.addChild<Component>(at(10.0F, 10.0F, 50.0F, 30.0F));
		parent.addChild<Component>(at(10.0F, 40.0F, 50.0F, 30.0F));
		EXPECT_TRUE(run().clean());
	}

	TEST_F(UiLintTest, OverlappingSiblingsWithEqualZIndexAreFlagged) {
		Ref<Component> parent = context.root().addChild<Component>(at(10.0F, 10.0F, 200.0F, 300.0F));
		parent.addChild<Component>(at(10.0F, 10.0F, 50.0F, 30.0F));
		parent.addChild<Component>(at(30.0F, 20.0F, 50.0F, 30.0F));
		const LintResult result = run();
		ASSERT_EQ(result.violations.size(), 1U);
		EXPECT_EQ(result.violations[0].rule, LintRule::SiblingOverlap);
		EXPECT_EQ(result.violations[0].path, "root/Component[0]/Component[0]");
		EXPECT_EQ(result.violations[0].otherPath, "root/Component[0]/Component[1]");
		EXPECT_FLOAT_EQ(result.violations[0].bounds.x, 20.0F); // in the viewport
	}

	TEST_F(UiLintTest, OverlapIsAllowedWhenTheEffectiveZIndexDiffers) {
		Ref<Component> parent = context.root().addChild<Component>(at(10.0F, 10.0F, 200.0F, 300.0F));
		parent.addChild<Component>(at(10.0F, 10.0F, 50.0F, 30.0F));
		parent.addChild<Component>({.position = {30.0F, 20.0F}, .size = {50.0F, 30.0F}, .zIndex = 1});
		EXPECT_TRUE(run().clean());
	}

	// Two children of a raised parent inherit its zIndex, so they still overlap each other.
	TEST_F(UiLintTest, InheritedZIndexesAreEqual) {
		Ref<Component> raised = context.root().addChild<Component>({.position = {0.0F, 0.0F}, .size = {200.0F, 200.0F}, .zIndex = 2});
		raised.addChild<Component>(at(0.0F, 0.0F, 50.0F, 50.0F));
		raised.addChild<Component>({.position = {10.0F, 10.0F}, .size = {50.0F, 50.0F}, .zIndex = 2});
		const LintResult result = run();
		ASSERT_EQ(result.violations.size(), 1U);
		EXPECT_EQ(result.violations[0].rule, LintRule::SiblingOverlap);
	}

	// pointerEvents None declares decoration: over a sibling, out of its parent or empty on purpose.
	// What it holds is still checked.
	TEST_F(UiLintTest, DecorationMayOverlapAndLeaveItsParent) {
		Ref<Component> parent = context.root().addChild<Component>(at(10.0F, 10.0F, 200.0F, 300.0F));
		parent.addChild<Component>(at(10.0F, 10.0F, 50.0F, 30.0F));
		Ref<Component> ring =
			parent.addChild<Component>({.position = {6.0F, 6.0F}, .size = {58.0F, 38.0F}, .pointerEvents = PointerEvents::None});
		parent.addChild<Component>({.position = {180.0F, 10.0F}, .size = {40.0F, 20.0F}, .pointerEvents = PointerEvents::None});
		parent.addChild<Component>({.position = {10.0F, 60.0F}, .size = {0.0F, 4.0F}, .pointerEvents = PointerEvents::None});
		EXPECT_TRUE(run().clean());

		ring.addChild<Component>(at(0.0F, 0.0F, 20.0F, 20.0F));
		ring.addChild<Component>(at(10.0F, 10.0F, 20.0F, 20.0F));
		const LintResult result = run();
		ASSERT_EQ(result.violations.size(), 1U);
		EXPECT_EQ(result.violations[0].rule, LintRule::SiblingOverlap);
		EXPECT_EQ(result.violations[0].path, "root/Component[0]/Component[1]/Component[0]");
	}

	TEST_F(UiLintTest, TouchingEdgesAreNotOverlap) {
		Ref<Component> parent = context.root().addChild<Component>(at(0.0F, 0.0F, 200.0F, 300.0F));
		parent.addChild<Component>(at(0.0F, 0.0F, 50.0F, 30.0F));
		parent.addChild<Component>(at(50.0F, 0.0F, 50.0F, 30.0F));
		EXPECT_TRUE(run().clean());
	}

	TEST_F(UiLintTest, ChildOutsideParentIsFlagged) {
		Ref<Component> parent = context.root().addChild<Component>({.id = "panel", .position = {50.0F, 50.0F}, .size = {100.0F, 100.0F}});
		parent.addChild<Component>(at(10.0F, 80.0F, 50.0F, 30.0F));
		const LintResult result = run();
		ASSERT_EQ(result.violations.size(), 1U);
		EXPECT_EQ(result.violations[0].rule, LintRule::ChildOutsideParent);
		EXPECT_EQ(result.violations[0].otherPath, "root/panel");
		EXPECT_FLOAT_EQ(result.violations[0].bounds.x, 60.0F);
		EXPECT_FLOAT_EQ(result.violations[0].bounds.y, 130.0F);
	}

	TEST_F(UiLintTest, AChildWithinEpsilonOfItsParentsEdgeIsClean) {
		Ref<Component> parent = context.root().addChild<Component>(at(50.0F, 50.0F, 100.0F, 100.0F));
		parent.addChild<Component>(at(50.25F, 70.0F, 50.0F, 30.0F));
		EXPECT_TRUE(run().clean());
	}

	TEST_F(UiLintTest, AnElementOutsideTheViewportIsFlagged) {
		context.root().addChild<Component>(at(790.0F, 10.0F, 50.0F, 30.0F));
		const LintResult result = run();
		ASSERT_EQ(result.violations.size(), 2U); // the root holds it too
		EXPECT_EQ(result.violations[0].rule, LintRule::ChildOutsideParent);
		EXPECT_EQ(result.violations[1].rule, LintRule::OutsideViewport);
		EXPECT_EQ(result.violations[1].otherPath, "viewport");
	}

	TEST_F(UiLintTest, ItChecksAtTheViewportItIsGiven) {
		context.root().addChild<Component>(at(600.0F, 10.0F, 50.0F, 30.0F));
		context.layout();
		const LintResult narrow = lint(context, {640.0F, 480.0F});
		ASSERT_FALSE(narrow.clean());
		EXPECT_EQ(narrow.violations.back().rule, LintRule::OutsideViewport);
	}

	TEST_F(UiLintTest, AZeroSizeVisibleElementIsFlagged) {
		context.root().addChild<Component>(at(10.0F, 10.0F, 0.0F, 30.0F));
		const LintResult result = run();
		ASSERT_EQ(result.violations.size(), 1U);
		EXPECT_EQ(result.violations[0].rule, LintRule::ZeroOrNegativeSize);
		EXPECT_EQ(result.violations[0].path, "root/Component[0]");
	}

	TEST_F(UiLintTest, AnInvisibleSubtreeIsSkipped) {
		Ref<Component> parent = context.root().addChild<Component>({.visible = false, .size = {100.0F, 100.0F}});
		parent.addChild<Component>(at(200.0F, 0.0F, 50.0F, 30.0F));
		parent.addChild<Component>(at(200.0F, 0.0F, 50.0F, 30.0F));
		EXPECT_TRUE(run().clean());
	}

	TEST_F(UiLintTest, RootsDoNotOverlapEachOther) {
		context.root();
		context.addRoot<Component>({.id = "hud"});
		EXPECT_TRUE(run().clean());
	}

	TEST_F(UiLintTest, TheJsonMatchesTheDirectResult) {
		context.root().addChild<Component>(at(10.0F, 10.0F, 0.0F, 30.0F));
		context.layout();
		const nlohmann::json json = nlohmann::json::parse(lintJson(&context, kViewport));
		EXPECT_EQ(json["count"].get<std::size_t>(), 1U);
		ASSERT_EQ(json["violations"].size(), 1U);
		EXPECT_EQ(json["violations"][0]["rule"], "zero-or-negative-size");
		EXPECT_EQ(json["violations"][0]["path"], "root/Component[0]");
		EXPECT_EQ(nlohmann::json::parse(lintJson(nullptr, kViewport)), nlohmann::json::parse(R"({"count":0,"violations":[]})"));
	}

} // namespace ui
