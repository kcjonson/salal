#include "vector/SvgMarkup.h"

#include <algorithm>
#include <cctype>

namespace renderer::svg {

	namespace {

		bool isNameChar(char c) {
			return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '-' || c == '_' || c == ':' || c == '.';
		}

		std::string trim(std::string_view text) {
			const size_t start = text.find_first_not_of(" \t\r\n");
			if (start == std::string_view::npos) {
				return "";
			}
			const size_t end = text.find_last_not_of(" \t\r\n");
			return std::string(text.substr(start, end - start + 1));
		}

	} // namespace

	std::string Problem::text() const {
		std::string out = file;
		if (line > 0) {
			out += ":" + std::to_string(line);
		}
		out += ": ";
		if (!element.empty()) {
			out += element + " ";
		}
		return out + message;
	}

	bool Scanner::next(Tag& tag, std::string& error) {
		while (true) {
			const size_t open = text.find('<', pos);
			if (open == std::string_view::npos) {
				return false;
			}
			advanceTo(open);
			if (skip("<!--", "-->", error) || skip("<?", "?>", error) || skip("<![CDATA[", "]]>", error)) {
				if (!error.empty()) {
					return false;
				}
				continue;
			}
			if (text.substr(pos).starts_with("<!")) {
				const size_t end = text.find('>', pos);
				if (end == std::string_view::npos) {
					error = "the markup ends inside a <!...>";
					return false;
				}
				if (text.substr(pos, end - pos).find('[') != std::string_view::npos) {
					error = "has a DOCTYPE with an internal subset; entities are references, and references are refused";
					return false;
				}
				advanceTo(end + 1);
				continue;
			}
			return readTag(tag, error);
		}
	}

	void Scanner::advanceTo(size_t to) {
		line += static_cast<int>(
			std::count(text.begin() + static_cast<std::ptrdiff_t>(pos), text.begin() + static_cast<std::ptrdiff_t>(to), '\n')
		);
		pos = to;
	}

	bool Scanner::skip(std::string_view open, std::string_view close, std::string& error) {
		if (!text.substr(pos).starts_with(open)) {
			return false;
		}
		const size_t end = text.find(close, pos + open.size());
		if (end == std::string_view::npos) {
			error = "the markup ends inside a " + std::string(open);
			return true;
		}
		advanceTo(end + close.size());
		return true;
	}

	void Scanner::skipSpace() {
		while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos])) != 0) {
			advanceTo(pos + 1);
		}
	}

	bool Scanner::readTag(Tag& tag, std::string& error) {
		tag = Tag{.line = line};
		advanceTo(pos + 1);
		if (pos < text.size() && text[pos] == '/') {
			tag.closing = true;
			advanceTo(pos + 1);
		}
		const size_t nameStart = pos;
		while (pos < text.size() && isNameChar(text[pos])) {
			++pos;
		}
		tag.name = std::string(text.substr(nameStart, pos - nameStart));
		if (tag.name.empty()) {
			error = "has a tag with no name";
			return false;
		}
		while (true) {
			skipSpace();
			if (pos >= text.size()) {
				error = "the markup ends inside <" + tag.name + ">";
				return false;
			}
			if (text[pos] == '>') {
				advanceTo(pos + 1);
				return true;
			}
			if (text.substr(pos).starts_with("/>")) {
				tag.selfClosing = true;
				advanceTo(pos + 2);
				return true;
			}
			const size_t attributeStart = pos;
			while (pos < text.size() && isNameChar(text[pos])) {
				++pos;
			}
			const std::string attribute(text.substr(attributeStart, pos - attributeStart));
			skipSpace();
			if (attribute.empty() || pos >= text.size() || text[pos] != '=') {
				error = "has a malformed attribute in <" + tag.name + ">";
				return false;
			}
			advanceTo(pos + 1);
			skipSpace();
			const char	 quote = pos < text.size() ? text[pos] : '\0';
			const size_t end = quote == '"' || quote == '\'' ? text.find(quote, pos + 1) : std::string_view::npos;
			if (end == std::string_view::npos) {
				error = "has an unquoted or unterminated attribute " + attribute + " in <" + tag.name + ">";
				return false;
			}
			tag.attributes[attribute] = std::string(text.substr(pos + 1, end - pos - 1));
			advanceTo(end + 1);
		}
	}

	std::map<std::string, std::string> propertiesOf(const Tag& tag) {
		std::map<std::string, std::string> properties = tag.attributes;
		const auto						   style = tag.attributes.find("style");
		if (style == tag.attributes.end()) {
			return properties;
		}
		size_t start = 0;
		while (start < style->second.size()) {
			const size_t	  end = std::min(style->second.find(';', start), style->second.size());
			const std::string declaration = style->second.substr(start, end - start);
			const size_t	  colon = declaration.find(':');
			if (colon != std::string::npos) {
				properties[trim(declaration.substr(0, colon))] = trim(declaration.substr(colon + 1));
			}
			start = end + 1;
		}
		return properties;
	}

} // namespace renderer::svg
