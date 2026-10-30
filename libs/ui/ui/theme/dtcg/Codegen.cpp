#include "ui/theme/dtcg/Codegen.h"

#include <algorithm>
#include <cctype>

namespace ui::dtcg {

	namespace {

		using Severity = Diagnostic::Severity;

		constexpr std::string_view kKeywords[] = {
			"alignas",	   "alignof",	"and",		  "and_eq",	   "asm",	   "auto",			"bitand",
			"bitor",	   "bool",		"break",	  "case",	   "catch",	   "char",			"char8_t",
			"char16_t",	   "char32_t",	"class",	  "compl",	   "concept",  "const",			"consteval",
			"constexpr",   "constinit", "const_cast", "continue",  "co_await", "co_return",		"co_yield",
			"decltype",	   "default",	"delete",	  "do",		   "double",   "dynamic_cast",	"else",
			"enum",		   "explicit",	"export",	  "extern",	   "false",	   "float",			"for",
			"friend",	   "goto",		"if",		  "inline",	   "int",	   "long",			"mutable",
			"namespace",   "new",		"noexcept",	  "not",	   "not_eq",   "nullptr",		"operator",
			"or",		   "or_eq",		"private",	  "protected", "public",   "register",		"reinterpret_cast",
			"requires",	   "return",	"short",	  "signed",	   "sizeof",   "static",		"static_assert",
			"static_cast", "struct",	"switch",	  "template",  "this",	   "thread_local",	"throw",
			"true",		   "try",		"typedef",	  "typeid",	   "typename", "union",			"unsigned",
			"using",	   "virtual",	"void",		  "volatile",  "wchar_t",  "while",			"xor",
			"xor_eq",	   "final",		"override",	  "import",	   "module",   "tokens_detail",
		};

		constexpr std::string_view kRawOpen = "R\"tokens(";
		constexpr std::string_view kRawClose = ")tokens\"";
		constexpr size_t		   kPieceBytes = 16000; // under MSVC's 16380-byte limit on one literal

		std::string_view handleType(TokenType type) {
			switch (type) {
				case TokenType::Color:
					return "ColorToken";
				case TokenType::Dimension:
					return "DimensionToken";
				case TokenType::FontFamily:
					return "FontFamilyToken";
				case TokenType::FontWeight:
					return "FontWeightToken";
				case TokenType::Duration:
					return "DurationToken";
				case TokenType::CubicBezier:
					return "CubicBezierToken";
				case TokenType::Number:
					return "NumberToken";
				case TokenType::StrokeStyle:
					return "StrokeStyleToken";
				case TokenType::Border:
					return "BorderToken";
				case TokenType::Transition:
					return "TransitionToken";
				case TokenType::Shadow:
					return "ShadowToken";
				case TokenType::Gradient:
					return "GradientToken";
				case TokenType::Typography:
					return "TypographyToken";
			}
			return "";
		}

		std::string_view typeEnumerator(TokenType type) {
			switch (type) {
				case TokenType::Color:
					return "Color";
				case TokenType::Dimension:
					return "Dimension";
				case TokenType::FontFamily:
					return "FontFamily";
				case TokenType::FontWeight:
					return "FontWeight";
				case TokenType::Duration:
					return "Duration";
				case TokenType::CubicBezier:
					return "CubicBezier";
				case TokenType::Number:
					return "Number";
				case TokenType::StrokeStyle:
					return "StrokeStyle";
				case TokenType::Border:
					return "Border";
				case TokenType::Transition:
					return "Transition";
				case TokenType::Shadow:
					return "Shadow";
				case TokenType::Gradient:
					return "Gradient";
				case TokenType::Typography:
					return "Typography";
			}
			return "";
		}

		std::string cppString(std::string_view text) {
			std::string out = "\"";
			for (char c : text) {
				if (c == '"' || c == '\\') {
					out += '\\';
				}
				out += c;
			}
			return out + "\"";
		}

		std::vector<std::string> segmentsOf(std::string_view path) {
			std::vector<std::string> segments;
			size_t					 start = 0;
			while (true) {
				const size_t dot = path.find('.', start);
				segments.emplace_back(path.substr(start, dot == std::string_view::npos ? std::string_view::npos : dot - start));
				if (dot == std::string_view::npos) {
					return segments;
				}
				start = dot + 1;
			}
		}

		// The generated namespaces: a leaf is a constant, anything else a namespace.
		struct Node {
			std::map<std::string, Node> children;
			std::optional<uint32_t>		index;
			TokenType					type = TokenType::Number;
			std::string					path;
		};

		class Emitter {
		  public:
			Emitter(std::string table, std::vector<Diagnostic>& diagnostics)
				: table(std::move(table)),
				  diagnostics(diagnostics) {}

			void add(Node& root, const std::string& path, size_t skip, uint32_t index, TokenType type, const std::string& file) {
				const std::vector<std::string> segments = segmentsOf(path);
				Node*						   node = &root;
				for (size_t i = skip; i < segments.size(); ++i) {
					const std::optional<std::string> name = identifierFor(segments[i]);
					if (!name.has_value()) {
						error(file, path, cppString(segments[i]) + " has no C++ name; use letters, digits, - and _");
						return;
					}
					Node&	   child = node->children[*name];
					const bool last = i + 1 == segments.size();
					if (child.index.has_value() || (last && !child.children.empty())) {
						error(file, path, "gets the same C++ name as " + (child.path.empty() ? std::string("another path") : child.path));
						return;
					}
					if (child.path.empty()) {
						child.path = path;
					}
					if (last) {
						child.index = index;
						child.type = type;
						child.path = path;
					}
					node = &child;
				}
			}

			void emit(const Node& node, std::string& out, int depth, bool styles) const {
				const std::string indent(static_cast<size_t>(depth), '\t');
				for (const auto& [name, child] : node.children) {
					if (child.index.has_value()) {
						const std::string type = styles ? "StyleId" : std::string(handleType(child.type));
						out += indent + "inline constexpr ::ui::" + type + " " + name + "{.table = &" + table +
							   ", .index = " + std::to_string(*child.index) + "};\n";
					} else {
						out += indent + "namespace " + name + " {\n";
						emit(child, out, depth + 1, styles);
						out += indent + "}\n";
					}
				}
			}

		  private:
			void error(const std::string& file, const std::string& path, std::string message) {
				diagnostics.push_back({.severity = Severity::Error, .file = file, .path = path, .message = std::move(message)});
			}

			std::string				 table;
			std::vector<Diagnostic>& diagnostics;
		};

		std::string rawPieces(const std::string& text) {
			std::string out;
			size_t		start = 0;
			while (start < text.size()) {
				size_t end = start;
				while (end < text.size()) {
					const size_t newline = text.find('\n', end);
					const size_t next = newline == std::string::npos ? text.size() : newline + 1;
					if (next - start > kPieceBytes && end > start) {
						break;
					}
					end = next;
				}
				out += "\t\t\t";
				out += kRawOpen;
				out += text.substr(start, end - start);
				out += kRawClose;
				out += ",\n";
				start = end;
			}
			return out;
		}

	} // namespace

	std::optional<std::string> identifierFor(std::string_view name) {
		if (name == "$root") {
			return "root";
		}
		std::string out;
		bool		capitalise = false;
		for (const char c : name) {
			if (c == '-' || c == ' ' || c == '/') {
				capitalise = !out.empty();
				continue;
			}
			if (std::isalnum(static_cast<unsigned char>(c)) == 0 && c != '_') {
				return std::nullopt;
			}
			out += capitalise ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : c;
			capitalise = false;
		}
		if (out.empty()) {
			return std::nullopt;
		}
		if (std::isdigit(static_cast<unsigned char>(out.front())) != 0) {
			out.insert(out.begin(), '_');
		}
		if (std::find(std::begin(kKeywords), std::end(kKeywords), out) != std::end(kKeywords)) {
			out += '_';
		}
		return out;
	}

	Catalog
	catalog(const ResolvedTokens& resolved, const std::set<std::string, std::less<>>& inherited, std::vector<Diagnostic>& diagnostics) {
		const std::string				   prefix = std::string(kStylesGroup) + ".";
		Catalog							   result;
		std::map<std::string, std::string> groups; // style path -> a file that sets one of its fields
		for (const auto& [path, token] : resolved) {
			if (path == kStylesGroup) {
				diagnostics.push_back(
					{.severity = Severity::Error,
					 .file = token.file,
					 .path = path,
					 .message = "styles is the group styles live in, not a token"}
				);
				continue;
			}
			if (!path.starts_with(prefix)) {
				if (!inherited.contains(path)) {
					result.tokens.push_back({.path = path, .type = token.type, .file = token.file});
				}
				continue;
			}
			const size_t dot = path.rfind('.');
			if (dot < prefix.size()) {
				diagnostics.push_back(
					{.severity = Severity::Error,
					 .file = token.file,
					 .path = path,
					 .message = "is a token directly under styles; a style is a group of style fields"}
				);
				continue;
			}
			groups.emplace(path.substr(0, dot), token.file);
			const std::string_view fieldName = std::string_view(path).substr(dot + 1);
			const StyleField*	   field = findStyleField(fieldName);
			if (field == nullptr) {
				std::string names;
				for (const StyleField& known : kStyleFields) {
					names += (names.empty() ? "" : ", ") + std::string(known.name);
				}
				diagnostics.push_back(
					{.severity = Severity::Error,
					 .file = token.file,
					 .path = path,
					 .message = std::string(fieldName) + " is not a style field (" + names + ")"}
				);
			} else if (field->type != token.type) {
				diagnostics.push_back(
					{.severity = Severity::Error,
					 .file = token.file,
					 .path = path,
					 .message = "is a " + std::string(tokenTypeName(token.type)) + "; a style's " + std::string(field->name) + " is a " +
								std::string(tokenTypeName(field->type))}
				);
			}
		}
		for (const auto& [group, file] : groups) {
			const auto inner = groups.lower_bound(group + ".");
			if (inner != groups.end() && inner->first.starts_with(group + ".")) {
				diagnostics.push_back(
					{.severity = Severity::Error,
					 .file = inner->second,
					 .path = group,
					 .message = "holds style fields and the style " + inner->first + "; a style holds no groups"}
				);
				continue;
			}
			const auto ownToken = inherited.lower_bound(group + ".");
			if (ownToken == inherited.end() || !ownToken->starts_with(group + ".")) {
				result.styles.push_back({.path = group, .file = file});
			}
		}
		return result;
	}

	CodegenOutput generate(const Catalog& catalog, const CodegenArgs& args, std::vector<Diagnostic>& diagnostics) {
		const std::string table = "::" + args.nameSpace + "::tokens_detail::kTable";
		Emitter			  emitter(table, diagnostics);
		Node			  styles;
		Node			  tokens;
		for (uint32_t i = 0; i < catalog.styles.size(); ++i) {
			emitter.add(styles, catalog.styles[i].path, 1, i, TokenType::Number, catalog.styles[i].file);
		}
		for (uint32_t i = 0; i < catalog.tokens.size(); ++i) {
			emitter.add(tokens, catalog.tokens[i].path, 0, i, catalog.tokens[i].type, catalog.tokens[i].file);
		}

		const std::string banner = "// Generated by salal_tokens() from " + args.origin + ". Do not edit.\n";
		CodegenOutput	  out;
		out.header = "#pragma once\n\n" + banner + "\n#include <ui/theme/TokenTable.h>\n\n";
		out.header += "namespace " + args.nameSpace + "::tokens_detail {\n\textern const ::ui::TokenTable kTable;\n}\n\n";
		out.header += "namespace " + args.nameSpace + " {\n\t// The table to hand the theme, so its styles and tokens load.\n";
		out.header += "\tinline const ::ui::TokenTable& tokenTable() {\n\t\treturn tokens_detail::kTable;\n\t}\n}\n\n";
		out.header += "namespace " + args.nameSpace + "::styles {\n";
		emitter.emit(styles, out.header, 1, true);
		out.header += "}\n\nnamespace " + args.nameSpace + "::tokens {\n";
		emitter.emit(tokens, out.header, 1, false);
		out.header += "}\n";

		out.source = banner + "\n#include \"" + args.header + "\"\n";
		if (args.hasBase) {
			out.source += "\n#include <ui/theme/Tokens.h>\n";
		}
		out.source += "\n#include <array>\n#include <string_view>\n\nnamespace " + args.nameSpace + "::tokens_detail {\n\tnamespace {\n";
		out.source += "\t\tconstexpr std::array<::ui::TokenEntry, " + std::to_string(catalog.tokens.size()) + "> kTokens{{\n";
		for (const CatalogEntry& entry : catalog.tokens) {
			out.source += "\t\t\t{.path = " + cppString(entry.path) +
						  ", .type = ::ui::dtcg::TokenType::" + std::string(typeEnumerator(entry.type)) + "},\n";
		}
		out.source += "\t\t}};\n\t\tconstexpr std::array<std::string_view, " + std::to_string(catalog.styles.size()) + "> kStyles{{\n";
		for (const CatalogEntry& style : catalog.styles) {
			out.source += "\t\t\t" + cppString(style.path) + ",\n";
		}
		out.source += "\t\t}};\n";
		for (size_t i = 0; i < args.files.size(); ++i) {
			if (args.files[i].text.find(kRawClose) != std::string::npos) {
				diagnostics.push_back(
					{.severity = Severity::Error,
					 .file = args.files[i].name,
					 .path = "",
					 .message = "contains )tokens\", which ends the raw string that embeds it"}
				);
			}
			const std::string pieces = rawPieces(args.files[i].text);
			const size_t	  count = static_cast<size_t>(std::count(pieces.begin(), pieces.end(), '\n'));
			out.source += "\t\tconstexpr std::array<std::string_view, " + std::to_string(count) + "> kFile" + std::to_string(i) + "{{\n" +
						  pieces + "\t\t}};\n";
		}
		out.source += "\t\tconstexpr std::array<::ui::EmbeddedFile, " + std::to_string(args.files.size()) + "> kFiles{{\n";
		for (size_t i = 0; i < args.files.size(); ++i) {
			out.source += "\t\t\t{.name = " + cppString(args.files[i].name) + ", .pieces = kFile" + std::to_string(i) + "},\n";
		}
		out.source += "\t\t}};\n\t} // namespace\n\n\tconst ::ui::TokenTable kTable{\n";
		out.source +=
			"\t\t.name = " + cppString(args.nameSpace) + ",\n\t\t.tokens = kTokens,\n\t\t.styles = kStyles,\n\t\t.files = kFiles,\n";
		out.source += "\t\t.entry = " + cppString(args.entry) +
					  ",\n\t\t.base = " + (args.hasBase ? "&::ui::tokens_detail::kTable" : "nullptr") + ",\n\t};\n}\n";
		return out;
	}

} // namespace ui::dtcg
