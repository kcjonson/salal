#pragma once

// Just enough XML to check an SVG at build time, for salal_art()'s check (ArtCheck.h) and ui's icon
// check: its tags with their attributes and lines, comments, processing instructions, CDATA and a
// doctype skipped, text ignored. A problem names the file, the line and the element.

#include <map>
#include <string>
#include <string_view>

namespace renderer::svg {

	struct Problem {
		std::string file;
		int			line = 0; // 0 when the problem is the file's as a whole
		std::string element;
		std::string message;

		// "file:line: <element> message", the line and element left out when there are none.
		std::string text() const;
	};

	struct Tag {
		std::string						   name;
		std::map<std::string, std::string> attributes;
		int								   line = 0;
		bool							   closing = false;
		bool							   selfClosing = false;
	};

	class Scanner {
	  public:
		explicit Scanner(std::string_view text)
			: text(text) {}

		// The next tag into `tag`: false at the end, with `error` set if the markup is broken. A doctype
		// with an internal subset is broken: its entities are references.
		bool next(Tag& tag, std::string& error);

		int currentLine() const { return line; }

	  private:
		void advanceTo(size_t to);
		bool skip(std::string_view open, std::string_view close, std::string& error);
		void skipSpace();
		bool readTag(Tag& tag, std::string& error);

		std::string_view text;
		size_t			 pos = 0;
		int				 line = 1;
	};

	// An element's presentation properties: its attributes with its style="..." declarations over
	// them, as CSS has it.
	std::map<std::string, std::string> propertiesOf(const Tag& tag);

} // namespace renderer::svg
