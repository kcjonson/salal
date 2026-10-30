#pragma once

// The typed values of the Design Tokens Format Module 2025.10 (docs/ui-design.md, "Theme"), as the
// loader decodes them: colours in sRGB, dimensions in px, durations in ms. No JSON here, so the
// theme's public headers don't reach nlohmann-json.

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace ui::dtcg {

	enum class TokenType : std::uint8_t {
		Color,
		Dimension,
		FontFamily,
		FontWeight,
		Duration,
		CubicBezier,
		Number,
		StrokeStyle,
		Border,
		Transition,
		Shadow,
		Gradient,
		Typography,
	};

	std::optional<TokenType> parseTokenType(std::string_view name);
	std::string_view		 tokenTypeName(TokenType type);

	// sRGB components and straight alpha, as the file gives them; blended as is (R5.24).
	struct ColorValue {
		float r = 0.0F;
		float g = 0.0F;
		float b = 0.0F;
		float a = 1.0F;
	};

	struct CubicBezierValue {
		float x1 = 0.0F;
		float y1 = 0.0F;
		float x2 = 1.0F;
		float y2 = 1.0F;
	};

	// Most preferred first; the theme maps the first name a game registered to its FontFamily id.
	struct FontFamilyValue {
		std::vector<std::string> names;
	};

	// A CSS line-style keyword, or a dash pattern in px with its cap (keyword empty).
	struct StrokeStyleValue {
		std::string		   keyword;
		std::vector<float> dashArray;
		std::string		   lineCap;
	};

	struct BorderValue {
		ColorValue		 color;
		float			 width = 0.0F;
		StrokeStyleValue style;
	};

	struct TransitionValue {
		float			 durationMs = 0.0F;
		float			 delayMs = 0.0F;
		CubicBezierValue timingFunction;
	};

	struct ShadowLayer {
		ColorValue color;
		float	   offsetX = 0.0F;
		float	   offsetY = 0.0F;
		float	   blur = 0.0F;
		float	   spread = 0.0F;
		bool	   inset = false;
	};

	struct ShadowValue {
		std::vector<ShadowLayer> layers;
	};

	struct GradientStop {
		ColorValue color;
		float	   position = 0.0F; // clamped to [0, 1], as the format says
	};

	struct GradientValue {
		std::vector<GradientStop> stops;
	};

	struct TypographyValue {
		FontFamilyValue fontFamily;
		float			fontSize = 0.0F;
		float			fontWeight = 400.0F;
		float			letterSpacing = 0.0F;
		float			lineHeight = 1.0F; // a multiple of fontSize
	};

	// float carries dimension (px), duration (ms), number and fontWeight; the token's type says which.
	using Value = std::variant<
		float,
		ColorValue,
		CubicBezierValue,
		FontFamilyValue,
		StrokeStyleValue,
		BorderValue,
		TransitionValue,
		ShadowValue,
		GradientValue,
		TypographyValue>;

	// Where a load went wrong. `file` is the tokens or resolver file as the load named it, `path`
	// the token's dot path; either may be empty.
	struct Diagnostic {
		enum class Severity : std::uint8_t { Warning, Error };

		Severity	severity = Severity::Error;
		std::string file;
		std::string path;
		std::string message;

		std::string text() const;
	};

	bool hasErrors(const std::vector<Diagnostic>& diagnostics);

	// A resolver modifier's name -> the context to load, e.g. {"theme": "light"}.
	using ResolverInputs = std::map<std::string, std::string, std::less<>>;

	// The look fields a style holds, named as Style names them (docs/ui-design.md, "Component API").
	// A style is a group under the top-level `styles` group whose tokens are all fields from this list.
	struct StyleField {
		std::string_view name;
		TokenType		 type;
	};

	inline constexpr std::array kStyleFields{
		StyleField{.name = "backgroundColor", .type = TokenType::Color},
		StyleField{.name = "borderColor", .type = TokenType::Color},
		StyleField{.name = "borderWidth", .type = TokenType::Dimension},
		StyleField{.name = "borderRadius", .type = TokenType::Dimension},
		StyleField{.name = "boxShadow", .type = TokenType::Shadow},
		StyleField{.name = "opacity", .type = TokenType::Number},
		StyleField{.name = "color", .type = TokenType::Color},
		StyleField{.name = "font", .type = TokenType::Typography},
		StyleField{.name = "fontFamily", .type = TokenType::FontFamily},
		StyleField{.name = "fontSize", .type = TokenType::Dimension},
		StyleField{.name = "fontWeight", .type = TokenType::FontWeight},
		StyleField{.name = "lineHeight", .type = TokenType::Number},
		StyleField{.name = "letterSpacing", .type = TokenType::Dimension},
	};

	inline constexpr std::string_view kStylesGroup = "styles";

	const StyleField* findStyleField(std::string_view name);

} // namespace ui::dtcg
