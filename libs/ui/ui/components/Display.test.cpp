// The display components where they hold logic: a progress bar's value, fill and texts, and a badge's tint
// under its own style and the theme. Headless; nothing draws.

#include "ui/Context.h"
#include "ui/components/Badge.h"
#include "ui/components/Panel.h"
#include "ui/components/ProgressBar.h"
#include "ui/theme/Tokens.h"

#include <gtest/gtest.h>

namespace ui {

	namespace {

		Component& child(const Context& context, const Component& parent, std::size_t index) {
			return *context.find(parent.getChildren().at(index));
		}

	} // namespace

	TEST(UiProgressBar, ClampsItsValueAndFillsThatShareOfTheTrack) {
		Context			 context({800.0F, 600.0F});
		Ref<ProgressBar> bar = context.root().addChild<ProgressBar>({.size = {200.0F, kAuto}, .value = 1.5F});
		EXPECT_FLOAT_EQ(bar.get()->getValue(), 1.0F);
		bar.get()->setValue(-1.0F);
		EXPECT_FLOAT_EQ(bar.get()->getValue(), 0.0F);

		bar.get()->setValue(0.25F);
		context.layout();
		Component& track = child(context, *bar.get(), 0); // no header: the track is its only child
		EXPECT_FLOAT_EQ(track.getSize().x, 200.0F);
		EXPECT_FLOAT_EQ(child(context, track, 0).getSize().x, 50.0F);

		// Auto's tone follows the value: danger under a quarter, then the fill takes positive's colour.
		const Theme&			theme = context.getTheme();
		const Component&		fill = child(context, track, 0);
		const Foundation::Color danger = toneColors(theme, Tone::Danger).color;
		bar.get()->setValue(0.1F);
		EXPECT_FLOAT_EQ(fill.getStyle().backgroundColor->r, danger.r);
		bar.get()->setValue(0.9F);
		EXPECT_FLOAT_EQ(fill.getStyle().backgroundColor->r, toneColors(theme, Tone::Positive).color.r);
	}

	TEST(UiProgressBar, ShowsOnlyTheTextsItHasAndKeepsTheValueAtTheEnd) {
		Context			 context({800.0F, 600.0F});
		Ref<ProgressBar> bar = context.root().addChild<ProgressBar>({.size = {200.0F, kAuto}, .valueText = "70%"});
		context.layout();
		Component& header = child(context, *bar.get(), 0);
		Component& label = child(context, header, 0);
		Component& value = child(context, header, 1);
		EXPECT_FALSE(label.isShown());
		EXPECT_FLOAT_EQ(value.getBounds().right(), 200.0F);

		bar.get()->setLabel("Morale");
		context.layout();
		EXPECT_TRUE(label.isShown());
		EXPECT_FLOAT_EQ(label.getBounds().x, 0.0F);
		EXPECT_FLOAT_EQ(value.getBounds().right(), 200.0F);
	}

	TEST(UiBadge, TakesItsTonesTintUnderItsOwnStyle) {
		Context	   context({800.0F, 600.0F});
		Ref<Badge> warning = context.root().addChild<Badge>({.label = "low", .tone = Tone::Warning});
		Ref<Badge> custom = context.root().addChild<Badge>(
			{.style = {.backgroundColor = Foundation::Color(0.1F, 0.2F, 0.3F, 1.0F)}, .label = "mine", .tone = Tone::Warning}
		);
		const ToneColors colors = toneColors(context.getTheme(), Tone::Warning);
		EXPECT_FLOAT_EQ(warning.get()->getStyle().backgroundColor->a, colors.subtle.a);
		EXPECT_FLOAT_EQ(custom.get()->getStyle().backgroundColor->b, 0.3F); // its own background wins
		EXPECT_FLOAT_EQ(custom.get()->getStyle().borderColor->r, colors.color.r);
		EXPECT_FLOAT_EQ(warning.get()->getStyle().borderRadius.value_or(-1.0F), context.getTheme().get(tokens::radius::sm));
		context.layout();
		EXPECT_FLOAT_EQ(warning.get()->getSize().y, 20.0F);
	}

	TEST(UiPanel, FillsItsSlots) {
		Context	   context({800.0F, 600.0F});
		Ref<Panel> panel = context.root().addChild<Panel>({.size = {300.0F, 200.0F}, .title = "Crew", .actions = true});
		ASSERT_NE(panel.get()->actions().get(), nullptr);
		panel.get()->body().addChild<Rectangle>({.size = {40.0F, 40.0F}});
		context.layout();
		const Component& body = *panel.get()->body().get();
		EXPECT_FLOAT_EQ(body.getSize().x, 300.0F);
		EXPECT_GT(body.getBounds().y, 0.0F);							// under the header
		EXPECT_FLOAT_EQ(body.getBounds().y + body.getSize().y, 200.0F); // grown to the panel's foot
		EXPECT_EQ(context.root().get()->getChildren().size(), 1U);
		EXPECT_EQ(context.find(panel.get()->getChildren().front())->getChildren().size(), 2U); // title and actions
	}

} // namespace ui
