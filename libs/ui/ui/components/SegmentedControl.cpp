#include "ui/components/SegmentedControl.h"

#include "ui/Context.h"
#include "ui/detail/Snapshot.h"
#include "ui/shapes/Text.h"
#include "ui/theme/Tokens.h"

namespace ui {

	namespace {

		constexpr float kInset = 2.0F;
		constexpr float kPadX = 12.0F;

		class Segment : public Control {
		  public:
			struct Args {
				UI_COMMON_ARGS
				std::string label;
				int			index = 0;
				float		fontSize = 14.0F;
			};

			explicit Segment(const Args& args)
				: label(args.label),
				  index(args.index),
				  fontSize(args.fontSize) {}

			[[nodiscard]] const char* typeName() const override { return "Segment"; }
			[[nodiscard]] StyleId	  themeStyle() const override { return styles::segment.theme; }

			void show(bool chosen, const ToneColors& tone) {
				setActive(chosen);
				setStyle(chosen ? Style{.backgroundColor = tone.subtle, .color = tone.color} : Style{});
				if (Text* shown = text.get()) {
					shown->setStyle({.color = getStyle().color, .fontFamily = getStyle().fontFamily, .fontSize = fontSize});
				}
			}

		  protected:
			void onMount() override {
				Control::onMount();
				setLayout({
					.display = Display::Flex,
					.padding = {0.0F, kPadX},
					.alignItems = AlignItems::Center,
					.justifyContent = JustifyContent::Center,
					.flexGrow = 1.0F,
				});
				text = addChild<Text>({.text = label});
			}

			void activate() override {
				if (auto* control = dynamic_cast<SegmentedControl*>(parent().get())) {
					control->choose(index);
				}
			}

		  private:
			std::string label;
			int			index;
			float		fontSize;
			Ref<Text>	text;
		};

	} // namespace

	SegmentedControl::SegmentedControl(const Args& args)
		: labels(args.options),
		  selected(args.selected),
		  tone(args.tone),
		  controlSize(args.controlSize),
		  onChange(args.onChange) {}

	void SegmentedControl::describe(nlohmann::json& node) const {
		Rectangle::describe(node);
		node["value"] = selected;
	}

	StyleId SegmentedControl::themeStyle() const {
		return styles::segmented.theme;
	}

	void SegmentedControl::onMount() {
		const SizeMetrics metrics = sizeMetrics(getContext()->getTheme(), controlSize);
		Layout			  row = getLayout();
		row.display = Display::Flex;
		row.flexDirection = FlexDirection::Row;
		row.gap = kInset;
		row.padding = kInset;
		setLayout(row);
		if (getSizeSpec().height.isAuto()) {
			setSize({getSizeSpec().width, metrics.controlHeight});
		}
		setFocusGroup(FocusGroup{.orientation = FocusGroup::Orientation::Horizontal, .wrap = true});
		for (int i = 0; i < static_cast<int>(labels.size()); ++i) {
			segments.push_back(addChild<Segment>({.label = labels[static_cast<size_t>(i)], .index = i, .fontSize = metrics.fontSize}));
		}
		show();
	}

	void SegmentedControl::setStyle(const Style& declared) {
		Rectangle::setStyle(declared);
		show();
	}

	void SegmentedControl::setSelected(int index) {
		selected = index;
		show();
	}

	void SegmentedControl::choose(int index) {
		if (index == selected) {
			return;
		}
		setSelected(index);
		if (onChange) {
			onChange(index);
		}
	}

	void SegmentedControl::show() {
		if (getContext() == nullptr) {
			return;
		}
		const ToneColors colors = toneColors(getContext()->getTheme(), tone);
		for (int i = 0; i < static_cast<int>(segments.size()); ++i) {
			if (auto* segment = static_cast<Segment*>(segments[static_cast<size_t>(i)].get())) {
				segment->show(i == selected, colors);
			}
		}
	}

} // namespace ui
