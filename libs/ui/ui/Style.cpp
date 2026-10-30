#include "ui/Style.h"

namespace ui {

	namespace {
		template <typename T>
		void over(std::optional<T>& field, const std::optional<T>& override) {
			if (override) {
				field = override;
			}
		}
	} // namespace

	Style Style::extend(const Style& overrides) const {
		Style out = *this;
		over(out.backgroundColor, overrides.backgroundColor);
		over(out.borderColor, overrides.borderColor);
		over(out.borderWidth, overrides.borderWidth);
		over(out.borderRadius, overrides.borderRadius);
		over(out.boxShadow, overrides.boxShadow);
		over(out.opacity, overrides.opacity);
		over(out.color, overrides.color);
		over(out.fontFamily, overrides.fontFamily);
		over(out.fontSize, overrides.fontSize);
		over(out.textAlign, overrides.textAlign);
		over(out.verticalAlign, overrides.verticalAlign);
		over(out.letterSpacing, overrides.letterSpacing);
		over(out.textTransform, overrides.textTransform);
		over(out.fontWeight, overrides.fontWeight);
		over(out.lineHeight, overrides.lineHeight);
		if (overrides.theme.table != nullptr) {
			out.theme = overrides.theme;
		}
		return out;
	}

} // namespace ui
