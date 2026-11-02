#include "ui/shapes/Text.h"

#include "ui/detail/Snapshot.h"
#include "ui/theme/Tokens.h"

#include <primitives/Primitives.h>
#include <utils/Utf8.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace ui {

	namespace {
		constexpr Foundation::Color kColor = Foundation::Color::white();
		constexpr float				kFontSize = 16.0F;
		constexpr float				kBaseFontSize = 16.0F; // FontRenderer's scale 1
		// FontRenderer takes a wrap width of 0 as one line, newlines and all, so text that breaks only
		// at newlines wraps at a width it never reaches.
		constexpr float kNoWrap = std::numeric_limits<float>::max();

		float alignOffset(float room, Foundation::HorizontalAlign align) {
			switch (align) {
				case Foundation::HorizontalAlign::Center:
					return room * 0.5F;
				case Foundation::HorizontalAlign::Right:
					return room;
				case Foundation::HorizontalAlign::Left:
					break;
			}
			return 0.0F;
		}

		float alignOffset(float room, Foundation::VerticalAlign align) {
			switch (align) {
				case Foundation::VerticalAlign::Middle:
					return room * 0.5F;
				case Foundation::VerticalAlign::Bottom:
					return room;
				case Foundation::VerticalAlign::Top:
					break;
			}
			return 0.0F;
		}
	} // namespace

	Text::Text(const Args& args)
		: text(args.text),
		  wrap(args.wrap) {}

	void Text::setText(std::string value) {
		if (value != text) {
			text = std::move(value);
			glyphOpacity = -1.0F;
			contentChanged();
		}
	}

	StyleId Text::themeStyle() const {
		return styles::text::body.theme;
	}

	void Text::setStyle(const Style& value) {
		Component::setStyle(value);
		glyphOpacity = -1.0F;
		contentChanged();
	}

	std::string Text::shown() const {
		if (style.textTransform.value_or(Foundation::TextTransform::None) != Foundation::TextTransform::Uppercase) {
			return text;
		}
		return foundation::UTF8::uppercase(text);
	}

	// Wrapping and explicit newlines take FontRenderer's wrapped path, which has no letter spacing.
	Text::Extent Text::measureAt(std::optional<float> wrapWidth) const {
		const Renderer::FontRenderer* fonts = Renderer::Primitives::getFontRenderer();
		if (fonts == nullptr || text.empty()) {
			return {};
		}
		const float				   scale = style.fontSize.value_or(kFontSize) / kBaseFontSize;
		const Renderer::FontFamily family = style.fontFamily.value_or(Renderer::FontFamily::Default);
		const std::string		   content = shown();
		if (wrapWidth || content.find('\n') != std::string::npos) {
			const auto& wrapped = fonts->wrapText(content, scale, wrapWidth.value_or(kNoWrap), family);
			return {.size = {wrapped.totalWidth, wrapped.totalHeight}, .lines = static_cast<int>(wrapped.lines.size())};
		}
		const glm::vec2 extent = fonts->MeasureText(content, scale, family, style.letterSpacing.value_or(0.0F));
		return {.size = {extent.x, extent.y}, .lines = 1};
	}

	std::optional<Foundation::Vec2> Text::intrinsicSize(std::optional<float> width, float availableWidth) const {
		if (!wrap) {
			return measureAt(std::nullopt).size;
		}
		std::optional<float> wrapWidth = width;
		if (!wrapWidth && std::isfinite(availableWidth)) {
			wrapWidth = availableWidth;
		}
		const Extent extent = measureAt(wrapWidth);
		return Foundation::Vec2{width.value_or(extent.size.x), extent.size.y};
	}

	// Its longest word when it wraps (R10.4), all of it when it doesn't.
	float Text::minContentWidth() const {
		if (!wrap) {
			return measureAt(std::nullopt).size.x;
		}
		const Renderer::FontRenderer* fonts = Renderer::Primitives::getFontRenderer();
		if (fonts == nullptr) {
			return 0.0F;
		}
		const float				   scale = style.fontSize.value_or(kFontSize) / kBaseFontSize;
		const Renderer::FontFamily family = style.fontFamily.value_or(Renderer::FontFamily::Default);
		const std::string		   content = shown();
		float					   longest = 0.0F;
		std::size_t				   start = 0;
		while (start < content.size()) {
			const std::size_t end = content.find_first_of(" \n", start);
			const std::size_t stop = end == std::string::npos ? content.size() : end;
			if (stop > start) {
				longest = std::max(longest, fonts->MeasureText(content.substr(start, stop - start), scale, family).x);
			}
			start = stop + 1;
		}
		return longest;
	}

	Text::Extent Text::measureExtent() const {
		return measureAt(wrap ? std::optional(size.x) : std::nullopt);
	}

	void Text::layOut() {
		glyphs.clear();
		glyphOpacity = opacity();
		glyphSize = size;
		const Renderer::FontRenderer* fonts = Renderer::Primitives::getFontRenderer();
		if (fonts == nullptr || text.empty()) {
			return;
		}
		const float						  scale = style.fontSize.value_or(kFontSize) / kBaseFontSize;
		const Renderer::FontFamily		  family = style.fontFamily.value_or(Renderer::FontFamily::Default);
		const Foundation::HorizontalAlign textAlign = style.textAlign.value_or(Foundation::HorizontalAlign::Left);
		const Foundation::Color			  color = detail::withOpacity(style.color.value_or(kColor), glyphOpacity);
		const glm::vec4					  rgba(color.r, color.g, color.b, color.a);
		const Extent					  extent = measureExtent();
		const float		  top = alignOffset(size.y - extent.size.y, style.verticalAlign.value_or(Foundation::VerticalAlign::Top));
		const std::string content = shown();
		if (wrap || content.find('\n') != std::string::npos) {
			const auto& wrapped = fonts->wrapText(content, scale, wrap ? size.x : kNoWrap, family);
			fonts->generateWrappedGlyphQuads(wrapped, {0.0F, top}, rgba, textAlign, size.x, glyphs);
		} else {
			const float left = alignOffset(size.x - extent.size.x, textAlign);
			fonts->generateGlyphQuads(content, {left, top}, scale, rgba, glyphs, family, style.letterSpacing.value_or(0.0F));
		}
	}

	void Text::render() {
		if (glyphOpacity != opacity() || glyphSize != size) {
			layOut();
		}
		if (!glyphs.empty()) {
			Renderer::Primitives::drawGlyphs(glyphs.data(), glyphs.size(), drawKey());
		}
	}

	void Text::describe(nlohmann::json& node) const {
		const Extent extent = measureExtent();
		const bool	 overflows = extent.size.x > size.x + 0.5F || extent.size.y > size.y + 0.5F;
		node["text"] = {
			{"content", text},
			{"measured", {{"w", extent.size.x}, {"h", extent.size.y}, {"lines", extent.lines}}},
			{"overflow", overflows ? "overflowing" : "none"},
		};
		node["style"] = detail::styleJson(std::nullopt, style.color.value_or(kColor), std::nullopt);
	}

} // namespace ui
