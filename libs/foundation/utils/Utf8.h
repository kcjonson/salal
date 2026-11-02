#pragma once

// UTF-8 in std::string, a character at a time. A character is a well-formed sequence, or one
// ill-formed sequence, which reads as U+FFFD: the longest start of a well-formed sequence that the
// next byte doesn't continue (Unicode's maximal subpart, as the WHATWG decoder takes it), or a
// single byte that starts none. So "\xE2\x82" followed by 'x' is one replacement and then 'x', and
// "\x80\x80" is two. Offsets are bytes.

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace foundation {

	namespace UTF8 {

		inline constexpr char32_t kReplacement = 0xFFFD;

		struct Character {
			char32_t	codePoint;
			std::size_t length; // in bytes, 1 to 4
		};

		// The character starting at `offset`, which is under text.size().
		Character decodeAt(std::string_view text, std::size_t offset);

		// U+FFFD's bytes for a surrogate or a value past U+10FFFF, which have none.
		std::string encode(char32_t codePoint);

		std::vector<char32_t> decode(std::string_view text);

		std::size_t characterCount(std::string_view text);

		// The text with each ill-formed sequence replaced by U+FFFD, so it is well-formed.
		std::string sanitize(std::string_view text);

		// The capital of each small letter in ASCII, the Latin-1 Supplement, Greek and Cyrillic, the
		// scripts most keyboards print on their main block; every other character as it is.
		std::string uppercase(std::string_view text);

		// The start of the character holding the byte at `offset`: `offset` itself on a boundary, and
		// text.size() past the end.
		std::size_t characterStart(std::string_view text, std::size_t offset);

		// The boundary after the character at `offset`, a boundary; text.size() at the end.
		std::size_t nextCharacterBoundary(std::string_view text, std::size_t offset);

		// The boundary before `offset`, a boundary; 0 at the start.
		std::size_t previousCharacterBoundary(std::string_view text, std::size_t offset);

	} // namespace UTF8

} // namespace foundation
