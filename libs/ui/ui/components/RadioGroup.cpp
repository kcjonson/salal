#include "ui/components/RadioGroup.h"

#include "ui/Context.h"
#include "ui/shapes/Circle.h"
#include "ui/shapes/Text.h"
#include "ui/theme/Tokens.h"
#include "ui/theme/Variants.h"

namespace ui {

	namespace {

		constexpr float kRing = 16.0F;
		constexpr float kDot = 8.0F;
		constexpr float kGap = 8.0F;
		constexpr float kOptionGap = 6.0F;

		class RadioOption : public Control {
		  public:
			struct Args {
				UI_COMMON_ARGS
				std::string label;
				int			index = 0;
			};

			explicit RadioOption(const Args& args)
				: label(args.label),
				  index(args.index) {}

			[[nodiscard]] const char* typeName() const override { return "RadioOption"; }
			[[nodiscard]] StyleId	  themeStyle() const override { return styles::listRow.theme; }

			void show(bool chosen, const Theme& theme) {
				setActive(chosen);
				const ToneColors accent = toneColors(theme, Tone::Accent);
				if (Circle* shown = ring.get()) {
					shown->setStyle(styles::check.extend({.borderColor = chosen ? accent.color : theme.get(tokens::color::line::strong)}));
				}
				if (Circle* shown = dot.get()) {
					shown->setVisible(chosen);
					shown->setStyle({.backgroundColor = accent.color});
				}
			}

		  protected:
			void onMount() override {
				Control::onMount();
				setLayout({.display = Display::Flex, .gap = kGap, .alignItems = AlignItems::Center});
				ring = addChild<Circle>({.size = {kRing, kRing}});
				const float inset = (kRing - kDot) * 0.5F;
				dot = ring.addChild<Circle>({.position = {inset, inset}, .size = {kDot, kDot}});
				addChild<Text>({.style = styles::text::body, .text = label});
			}

			void activate() override {
				if (auto* group = dynamic_cast<RadioGroup*>(parent().get())) {
					group->choose(index);
				}
			}

		  private:
			std::string label;
			int			index;
			Ref<Circle> ring;
			Ref<Circle> dot;
		};

	} // namespace

	RadioGroup::RadioGroup(const Args& args)
		: labels(args.options),
		  selected(args.selected),
		  direction(args.direction),
		  onChange(args.onChange) {}

	void RadioGroup::onMount() {
		Layout layout = getLayout();
		layout.display = Display::Flex;
		layout.flexDirection = direction;
		layout.alignItems = AlignItems::Start;
		layout.gap = kOptionGap;
		setLayout(layout);
		setFocusGroup(
			FocusGroup{
				.orientation = direction == FlexDirection::Column ? FocusGroup::Orientation::Vertical : FocusGroup::Orientation::Horizontal,
				.wrap = true,
			}
		);
		for (int i = 0; i < static_cast<int>(labels.size()); ++i) {
			options.push_back(addChild<RadioOption>({.label = labels[static_cast<size_t>(i)], .index = i}));
		}
		show();
	}

	void RadioGroup::setStyle(const Style& declared) {
		Component::setStyle(declared);
		show();
	}

	void RadioGroup::setSelected(int index) {
		selected = index;
		show();
	}

	void RadioGroup::choose(int index) {
		if (index == selected) {
			return;
		}
		setSelected(index);
		if (onChange) {
			onChange(index);
		}
	}

	void RadioGroup::show() {
		if (getContext() == nullptr) {
			return;
		}
		for (int i = 0; i < static_cast<int>(options.size()); ++i) {
			if (auto* option = static_cast<RadioOption*>(options[static_cast<size_t>(i)].get())) {
				option->show(i == selected, getContext()->getTheme());
			}
		}
	}

} // namespace ui
