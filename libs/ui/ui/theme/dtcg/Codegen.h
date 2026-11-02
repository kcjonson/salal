#pragma once

// salal_tokens()' C++ (cmake/SalalTokens.cmake): a tokens folder's styles and tokens as typed
// constants in a header, and its table with the folder's documents embedded in a source.

#include "ui/theme/dtcg/Document.h"

#include <set>

namespace ui::dtcg {

	struct CatalogEntry {
		std::string path;
		TokenType	type = TokenType::Number; // unused for a style
		std::string file;
	};

	// What a generated table names. A style is a group under the top-level `styles` group whose
	// tokens are style fields of their field's type; it holds no groups. Every other token is a token.
	struct Catalog {
		std::vector<CatalogEntry> tokens;
		std::vector<CatalogEntry> styles;
	};

	// The catalog of `resolved`, leaving out the tokens and styles a base layer already defines
	// (`inherited`, its token paths), which its own table names.
	Catalog
	catalog(const ResolvedTokens& resolved, const std::set<std::string, std::less<>>& inherited, std::vector<Diagnostic>& diagnostics);

	struct SourceFile {
		std::string name;
		std::string text;
	};

	struct CodegenArgs {
		std::string				nameSpace; // "ui", "my_game", "my_game::theme"
		std::string				header;	   // the header as the source includes it
		std::string				origin;	   // the tokens folder, for the comment
		std::string				entry;	   // the resolver file, or empty
		std::vector<SourceFile> files;
		bool					hasBase = true; // false for ui's own table
	};

	struct CodegenOutput {
		std::string header;
		std::string source;
	};

	// A token or group name as a C++ identifier: separators (-, space, /) start a capital, a leading
	// digit gets an underscore, a keyword a trailing one, $root is `root`. Null if there's no way.
	std::optional<std::string> identifierFor(std::string_view name);

	CodegenOutput generate(const Catalog& catalog, const CodegenArgs& args, std::vector<Diagnostic>& diagnostics);

} // namespace ui::dtcg
