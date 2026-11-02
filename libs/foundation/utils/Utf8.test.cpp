// UTF-8 by character: what counts as one, ill-formed sequences included, and that decoding, counting
// and the boundary walks agree on it, since the renderer draws what decode reads and the text editor
// moves its caret by the boundaries.

#include "utils/Utf8.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <string_view>
#include <vector>

namespace foundation {

	namespace {
		constexpr char32_t kBad = UTF8::kReplacement;

		struct Case {
			std::string_view	  text;
			std::vector<char32_t> characters;
		};

		const std::vector<Case>& cases() {
			static const std::vector<Case> kCases{
				{"a\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80", {U'a', 0xE9, 0x20AC, 0x1F600}}, // one to four bytes
				{"\xE2\x82x", {kBad, U'x'}},											  // three bytes cut short: one replacement
				{"ab\xF0\x9F\x98", {U'a', U'b', kBad}},									  // cut short by the end
				{"\x80\x80", {kBad, kBad}},												  // lone continuation bytes, one each
				{"\xC3(", {kBad, U'('}},												  // a lead the next byte doesn't continue
				{"\xC0\xAF\xFF", {kBad, kBad, kBad}},									  // bytes that lead nothing
				{"\xE0\x80\xAF", {kBad, kBad, kBad}},									  // overlong
				{"\xED\xA0\x80", {kBad, kBad, kBad}},									  // a surrogate
				{"\xF4\x90\x80\x80", {kBad, kBad, kBad, kBad}},							  // past U+10FFFF
				{"\xC3\xA9\xA9", {0xE9, kBad}},											  // a continuation byte too many
			};
			return kCases;
		}
	} // namespace

	TEST(Utf8Test, EachIllFormedSequenceIsOneReplacementAndTheBoundariesAgree) {
		for (const Case& sample : cases()) {
			const std::string_view text = sample.text;
			EXPECT_EQ(UTF8::decode(text), sample.characters) << testing::PrintToString(text);
			EXPECT_EQ(UTF8::characterCount(text), sample.characters.size()) << testing::PrintToString(text);

			std::vector<std::size_t> forward{0};
			for (std::size_t at = 0; at < text.size();) {
				at = UTF8::nextCharacterBoundary(text, at);
				forward.push_back(at);
			}
			ASSERT_EQ(forward.size(), sample.characters.size() + 1) << testing::PrintToString(text);

			std::vector<std::size_t> backward{text.size()};
			for (std::size_t at = text.size(); at > 0;) {
				at = UTF8::previousCharacterBoundary(text, at);
				backward.push_back(at);
			}
			std::ranges::reverse(backward);
			EXPECT_EQ(backward, forward) << testing::PrintToString(text);

			// Every byte snaps back to the start of the character holding it.
			for (std::size_t byte = 0; byte < text.size(); ++byte) {
				const auto start = std::ranges::upper_bound(forward, byte) - 1;
				EXPECT_EQ(UTF8::characterStart(text, byte), *start) << testing::PrintToString(text) << " byte " << byte;
			}
			EXPECT_EQ(UTF8::characterStart(text, text.size()), text.size());
		}
	}

	TEST(Utf8Test, SanitizeAndEncodeWriteOnlyWellFormedText) {
		EXPECT_EQ(UTF8::sanitize("caf\xC3\xA9"), "caf\xC3\xA9");
		EXPECT_EQ(
			UTF8::sanitize(
				"a\xE2\x82"
				"b\x80"
			),
			"a\xEF\xBF\xBD"
			"b\xEF\xBF\xBD"
		);
		EXPECT_EQ(UTF8::encode(0xD800), "\xEF\xBF\xBD");
		EXPECT_EQ(UTF8::encode(0x110000), "\xEF\xBF\xBD");
		EXPECT_EQ(UTF8::encode(0x1F600), "\xF0\x9F\x98\x80");
	}

	TEST(Utf8Test, UppercaseCapitalisesByCodePoint) {
		// é, ñ, Greek with a final sigma, Cyrillic; ß and the CJK have no single capital here.
		EXPECT_EQ(
			UTF8::uppercase("caf\xC3\xA9 \xC3\xB1 \xCF\x83\xCF\x82 \xD1\x8F\xD1\x91 \xC3\x9F \xE4\xB8\x96"),
			"CAF\xC3\x89 \xC3\x91 \xCE\xA3\xCE\xA3 \xD0\xAF\xD0\x81 \xC3\x9F \xE4\xB8\x96"
		);
	}

} // namespace foundation
