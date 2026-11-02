#include "ui/theme/dtcg/Values.h"

#include <algorithm>
#include <utility>

namespace ui::dtcg {

	namespace {
		constexpr std::array<std::pair<std::string_view, TokenType>, 13> kTypeNames{{
			{"color", TokenType::Color},
			{"dimension", TokenType::Dimension},
			{"fontFamily", TokenType::FontFamily},
			{"fontWeight", TokenType::FontWeight},
			{"duration", TokenType::Duration},
			{"cubicBezier", TokenType::CubicBezier},
			{"number", TokenType::Number},
			{"strokeStyle", TokenType::StrokeStyle},
			{"border", TokenType::Border},
			{"transition", TokenType::Transition},
			{"shadow", TokenType::Shadow},
			{"gradient", TokenType::Gradient},
			{"typography", TokenType::Typography},
		}};
	} // namespace

	std::optional<TokenType> parseTokenType(std::string_view name) {
		for (const auto& [typeName, type] : kTypeNames) {
			if (typeName == name) {
				return type;
			}
		}
		return std::nullopt;
	}

	std::string_view tokenTypeName(TokenType type) {
		for (const auto& [typeName, candidate] : kTypeNames) {
			if (candidate == type) {
				return typeName;
			}
		}
		return "?";
	}

	std::string Diagnostic::text() const {
		std::string out = severity == Severity::Error ? "error: " : "warning: ";
		if (!file.empty()) {
			out += file + ": ";
		}
		if (!path.empty()) {
			out += path + ": ";
		}
		return out + message;
	}

	bool hasErrors(const std::vector<Diagnostic>& diagnostics) {
		return std::any_of(diagnostics.begin(), diagnostics.end(), [](const Diagnostic& d) {
			return d.severity == Diagnostic::Severity::Error;
		});
	}

	const StyleField* findStyleField(std::string_view name) {
		for (const StyleField& field : kStyleFields) {
			if (field.name == name) {
				return &field;
			}
		}
		return nullptr;
	}

} // namespace ui::dtcg
