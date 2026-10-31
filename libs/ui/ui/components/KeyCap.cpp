#include "ui/components/KeyCap.h"

#include "ui/theme/Tokens.h"

namespace ui {

	namespace {
		constexpr float kHeight = 18.0F;
		constexpr float kPadX = 5.0F;
		constexpr float kEdge = 2.0F; // the bottom edge that reads as a raised key
	} // namespace

	KeyCap::KeyCap(const Args& args)
		: label(args.label) {}

	StyleId KeyCap::themeStyle() const {
		return styles::keycap.theme;
	}

	void KeyCap::onMount() {
		Layout row = getLayout();
		row.display = Display::Flex;
		row.flexDirection = FlexDirection::Row;
		row.alignItems = AlignItems::Center;
		row.justifyContent = JustifyContent::Center;
		row.padding = {0.0F, kPadX};
		setLayout(row);
		if (getSizeSpec().height.isAuto()) {
			setSize({getSizeSpec().width, kHeight});
		}
		setMinSize({kHeight, kAuto});
		addChild<Rectangle>({
			.size = {kAuto, kEdge},
			.layout = {.position = Position::Absolute, .inset = {.right = 0.0F, .bottom = 0.0F, .left = 0.0F}},
			.style = styles::keycapEdge,
			.pointerEvents = PointerEvents::None,
		});
		text = addChild<Text>({.style = styles::label.extend({.textTransform = Foundation::TextTransform::Uppercase}), .text = label});
	}

	void KeyCap::setLabel(std::string value) {
		label = std::move(value);
		if (Text* shown = text.get()) {
			shown->setText(label);
		}
	}

} // namespace ui
