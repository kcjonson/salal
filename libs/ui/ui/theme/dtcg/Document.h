#pragma once

// The Design Tokens Format Module 2025.10 (https://www.designtokens.org/tr/2025.10/format/) on
// nlohmann-json: documents checked and merged into one tree, then every token resolved, aliases
// followed, its type found and its value decoded. Shared by the theme loader and salal_tokens' tool.
//
// Where the format leaves a choice, the loader takes one:
// - A token's type is its own $type, else its alias target's, else its nearest group's (5.2.2).
// - A later document replaces a token at the same path whole and merges groups by name; a token
//   over a group, or a group over a token, is an error.
// - A $ref to a whole token takes the token's $value. References are local (#/...); a reference
//   that resolves to an array inside a shadow or gradient array is spliced into it.
// - Only sRGB colours are blended as given; another colour space takes its hex or fails.

#include "ui/theme/dtcg/Values.h"

#include <functional>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ui::dtcg {

	// A document by the name a load gives it (a path relative to its tokens folder), or nothing.
	using ReadFile = std::function<std::optional<std::string>(const std::string& name)>;

	// Parses `text` as JSON; a syntax error is a diagnostic and gives a discarded value.
	nlohmann::json parseJson(std::string_view text, const std::string& file, std::vector<Diagnostic>& diagnostics);

	// Documents merged in order, later over earlier. A document with a structural error (a bad name,
	// a token with children, an unknown $type) is reported and left out whole.
	class TokenSet {
	  public:
		void merge(const nlohmann::json& document, const std::string& file, std::vector<Diagnostic>& diagnostics);

		const nlohmann::json&					  root() const { return tree; }
		const std::map<std::string, std::string>& origins() const { return origin; }

	  private:
		void mergeGroup(
			nlohmann::json&			 into,
			const nlohmann::json&	 from,
			const std::string&		 path,
			const std::string&		 file,
			std::vector<Diagnostic>& diagnostics
		);

		nlohmann::json					   tree = nlohmann::json::object();
		std::map<std::string, std::string> origin; // token path -> the file that last set it
	};

	struct ResolvedToken {
		TokenType	type;
		Value		value;
		std::string file;
	};

	using ResolvedTokens = std::map<std::string, ResolvedToken, std::less<>>;

	struct ResolveOptions {
		float rootFontSize = 16.0F; // px a rem is
	};

	// Every token in `set`, keyed by dot path ($root included as a segment). A token that fails is
	// left out with an error; so is one whose alias reaches it.
	ResolvedTokens resolve(const TokenSet& set, const ResolveOptions& options, std::vector<Diagnostic>& diagnostics);

} // namespace ui::dtcg
