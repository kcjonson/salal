#include "ui/shapes/Text.h"

#include "ui/detail/Snapshot.h"

#include <primitives/Primitives.h>

#include <algorithm>
#include <cctype>
#include <limits>
#include <utility>

namespace ui {

	namespace {
		constexpr Foundation::Color kColor = Foundation::Color::white();
		constexpr float				kFontSize = 16.0F;
		constexpr float				kBaseFontSize = 16.0F; // FontRenderer's scale 1

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

	void Text::onMount() {
		hugWidth = size.x <= 0.0F;
		hugHeight = size.y <= 0.0F;
		measure();
	}

	void Text::setText(std::string value) {
		if (value != text) {
			text = std::move(value);
			measure();
		}
	}

	// An axis set to 0 hugs again; any other value fixes it.
	void Text::setSize(Foundation::Vec2 value) {
		hugWidth = value.x <= 0.0F;
		hugHeight = value.y <= 0.0F;
		Component::setSize(value);
		measure();
	}

	void Text::setStyle(const Style& value) {
		Component::setStyle(value);
		measure();
	}

	bool Text::multiline(const std::string& content) const {
		return (wrap && !hugWidth) || content.find('\n') != std::string::npos;
	}

	// FontRenderer takes a width of 0 as one line, newlines and all, so text hugging its width wraps at
	// a width it never reaches and breaks at newlines only.
	float Text::wrapWidth() const {
		return wrap && !hugWidth ? size.x : std::numeric_limits<float>::max();
	}

	std::string Text::shown() const {
		if (style.textTransform.value_or(Foundation::TextTransform::None) != Foundation::TextTransform::Uppercase) {
			return text;
		}
		std::string upper = text;
		std::ranges::transform(upper, upper.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
		return upper;
	}

	// Explicit newlines and wrapping take FontRenderer's wrapped path, which has no letter spacing.
	void Text::measure() {
		glyphOpacity = -1.0F;
		const Renderer::FontRenderer* fonts = Renderer::Primitives::getFontRenderer();
		measured = {0.0F, 0.0F};
		lineCount = 0;
		if (fonts != nullptr && !text.empty()) {
			const float				   scale = style.fontSize.value_or(kFontSize) / kBaseFontSize;
			const Renderer::FontFamily family = style.fontFamily.value_or(Renderer::FontFamily::Default);
			const std::string		   content = shown();
			if (multiline(content)) {
				const auto& wrapped = fonts->wrapText(content, scale, wrapWidth(), family);
				measured = {wrapped.totalWidth, wrapped.totalHeight};
				lineCount = static_cast<int>(wrapped.lines.size());
			} else {
				const glm::vec2 extent = fonts->MeasureText(content, scale, family, style.letterSpacing.value_or(0.0F));
				measured = {extent.x, extent.y};
				lineCount = 1;
			}
		}
		const Foundation::Vec2 hugged{hugWidth ? measured.x : size.x, hugHeight ? measured.y : size.y};
		Component::setSize(hugged);
	}

	void Text::layOut() {
		glyphs.clear();
		glyphOpacity = opacity();
		const Renderer::FontRenderer* fonts = Renderer::Primitives::getFontRenderer();
		if (fonts == nullptr || text.empty()) {
			return;
		}
		const float						  scale = style.fontSize.value_or(kFontSize) / kBaseFontSize;
		const Renderer::FontFamily		  family = style.fontFamily.value_or(Renderer::FontFamily::Default);
		const Foundation::HorizontalAlign textAlign = style.textAlign.value_or(Foundation::HorizontalAlign::Left);
		const Foundation::Color			  color = detail::withOpacity(style.color.value_or(kColor), glyphOpacity);
		const glm::vec4					  rgba(color.r, color.g, color.b, color.a);
		const float		  top = alignOffset(size.y - measured.y, style.verticalAlign.value_or(Foundation::VerticalAlign::Top));
		const std::string content = shown();
		if (multiline(content)) {
			const auto& wrapped = fonts->wrapText(content, scale, wrapWidth(), family);
			fonts->generateWrappedGlyphQuads(wrapped, {0.0F, top}, rgba, textAlign, size.x, glyphs);
		} else {
			const float left = alignOffset(size.x - measured.x, textAlign);
			fonts->generateGlyphQuads(content, {left, top}, scale, rgba, glyphs, family, style.letterSpacing.value_or(0.0F));
		}
	}

	void Text::render() {
		if (glyphOpacity != opacity()) {
			layOut();
		}
		if (!glyphs.empty()) {
			Renderer::Primitives::drawGlyphs(glyphs.data(), glyphs.size(), drawKey());
		}
	}

	void Text::describe(nlohmann::json& node) const {
		node["text"] = {
			{"content", text},
			{"measured", {{"w", measured.x}, {"h", measured.y}, {"lines", lineCount}}},
			{"overflow", "none"},
		};
		node["style"] = detail::styleJson(std::nullopt, style.color.value_or(kColor), std::nullopt);
	}

} // namespace ui
