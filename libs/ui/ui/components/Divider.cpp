#include "ui/components/Divider.h"

#include "ui/Context.h"
#include "ui/shapes/Rectangle.h"
#include "ui/theme/Tokens.h"

namespace ui {

	namespace {
		constexpr float kLabelGap = 12.0F;
	} // namespace

	Divider::Divider(const Args& args)
		: label(args.label) {}

	void Divider::onMount() {
		Layout row = getLayout();
		row.display = Display::Flex;
		row.flexDirection = FlexDirection::Row;
		row.alignItems = AlignItems::Center;
		row.gap = kLabelGap;
		setLayout(row);
		if (getSizeSpec().width.isAuto()) {
			setSize({percent(100.0F), getSizeSpec().height});
		}
		const float hairline = getContext()->getTheme().get(tokens::lineWidth::thin);
		const auto	line = [&] {
			 addChild<Rectangle>({.size = {kAuto, hairline}, .layout = {.flexGrow = 1.0F}, .style = styles::divider});
		};
		line();
		if (!label.empty()) {
			addChild<Text>({.style = styles::label.extend({.textTransform = Foundation::TextTransform::Uppercase}), .text = label});
			line();
		}
	}

} // namespace ui
