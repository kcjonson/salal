#include "utils/Utf8.h"

#include <algorithm>

namespace foundation {
	namespace UTF8 {

		namespace {
			bool isContinuation(char byte) {
				return (static_cast<unsigned char>(byte) & 0xC0U) == 0x80U;
			}

			char32_t capital(char32_t c) {
				if ((c >= U'a' && c <= U'z') || (c >= 0xE0 && c <= 0xFE && c != 0xF7)) {
					return c - 0x20;
				}
				if (c == 0x3C2) {
					return 0x3A3; // final sigma
				}
				if ((c >= 0x3B1 && c <= 0x3CB) || (c >= 0x430 && c <= 0x44F)) {
					return c - 0x20;
				}
				if (c >= 0x450 && c <= 0x45F) {
					return c - 0x50;
				}
				return c;
			}
		} // namespace

		Character decodeAt(std::string_view text, std::size_t offset) {
			const auto byte = [&](std::size_t at) {
				return static_cast<unsigned char>(text[at]);
			};
			const auto	  lead = byte(offset);
			std::size_t	  length = 0;
			char32_t	  codePoint = 0;
			unsigned char low = 0x80;
			unsigned char high = 0xBF;
			// Table 3-7 of the Unicode standard: the lead byte sets the length and the range of the
			// second byte, which keeps out overlong forms, surrogates and values past U+10FFFF.
			if (lead < 0x80) {
				return {.codePoint = lead, .length = 1};
			}
			if (lead >= 0xC2 && lead <= 0xDF) {
				length = 2;
				codePoint = lead & 0x1FU;
			} else if (lead >= 0xE0 && lead <= 0xEF) {
				length = 3;
				codePoint = lead & 0x0FU;
				low = lead == 0xE0 ? 0xA0 : low;
				high = lead == 0xED ? 0x9F : high;
			} else if (lead >= 0xF0 && lead <= 0xF4) {
				length = 4;
				codePoint = lead & 0x07U;
				low = lead == 0xF0 ? 0x90 : low;
				high = lead == 0xF4 ? 0x8F : high;
			} else {
				return {.codePoint = kReplacement, .length = 1};
			}
			for (std::size_t i = 1; i < length; ++i) {
				if (offset + i >= text.size() || byte(offset + i) < low || byte(offset + i) > high) {
					return {.codePoint = kReplacement, .length = i};
				}
				codePoint = (codePoint << 6U) | (byte(offset + i) & 0x3FU);
				low = 0x80;
				high = 0xBF;
			}
			return {.codePoint = codePoint, .length = length};
		}

		std::string encode(char32_t codePoint) {
			if ((codePoint >= 0xD800 && codePoint <= 0xDFFF) || codePoint > 0x10FFFF) {
				codePoint = kReplacement;
			}
			std::string out;
			if (codePoint <= 0x7F) {
				out.push_back(static_cast<char>(codePoint));
			} else if (codePoint <= 0x7FF) {
				out.push_back(static_cast<char>(0xC0U | (codePoint >> 6U)));
				out.push_back(static_cast<char>(0x80U | (codePoint & 0x3FU)));
			} else if (codePoint <= 0xFFFF) {
				out.push_back(static_cast<char>(0xE0U | (codePoint >> 12U)));
				out.push_back(static_cast<char>(0x80U | ((codePoint >> 6U) & 0x3FU)));
				out.push_back(static_cast<char>(0x80U | (codePoint & 0x3FU)));
			} else {
				out.push_back(static_cast<char>(0xF0U | (codePoint >> 18U)));
				out.push_back(static_cast<char>(0x80U | ((codePoint >> 12U) & 0x3FU)));
				out.push_back(static_cast<char>(0x80U | ((codePoint >> 6U) & 0x3FU)));
				out.push_back(static_cast<char>(0x80U | (codePoint & 0x3FU)));
			}
			return out;
		}

		std::vector<char32_t> decode(std::string_view text) {
			std::vector<char32_t> out;
			out.reserve(text.size());
			for (std::size_t at = 0; at < text.size();) {
				const Character character = decodeAt(text, at);
				out.push_back(character.codePoint);
				at += character.length;
			}
			return out;
		}

		std::size_t characterCount(std::string_view text) {
			std::size_t count = 0;
			for (std::size_t at = 0; at < text.size(); at += decodeAt(text, at).length) {
				++count;
			}
			return count;
		}

		std::string sanitize(std::string_view text) {
			std::string out;
			out.reserve(text.size());
			for (std::size_t at = 0; at < text.size();) {
				const Character character = decodeAt(text, at);
				if (character.codePoint == kReplacement) {
					out += encode(kReplacement);
				} else {
					out.append(text.substr(at, character.length));
				}
				at += character.length;
			}
			return out;
		}

		std::string uppercase(std::string_view text) {
			std::string out;
			out.reserve(text.size());
			for (const char32_t c : decode(text)) {
				out += encode(capital(c));
			}
			return out;
		}

		std::size_t characterStart(std::string_view text, std::size_t offset) {
			if (offset >= text.size()) {
				return text.size();
			}
			// A character's later bytes are all continuation bytes, so one holding `offset` starts at
			// most three bytes back, at the nearest byte that isn't one, and only if it reaches this far.
			for (std::size_t start = offset; start > 0 && offset - start < 3 && isContinuation(text[start]);) {
				--start;
				if (!isContinuation(text[start])) {
					return start + decodeAt(text, start).length > offset ? start : offset;
				}
			}
			return offset;
		}

		std::size_t nextCharacterBoundary(std::string_view text, std::size_t offset) {
			return offset >= text.size() ? text.size() : offset + decodeAt(text, offset).length;
		}

		std::size_t previousCharacterBoundary(std::string_view text, std::size_t offset) {
			const std::size_t end = std::min(offset, text.size());
			return end == 0 ? 0 : characterStart(text, end - 1);
		}

	} // namespace UTF8
} // namespace foundation
