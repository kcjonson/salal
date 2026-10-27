#pragma once

// The engine's game PRNG: dual-deck-builder's Rng (src/renderer/game/core/Rng.ts) at
// RNG_VERSION 1, bit for bit, so a seed draws the same numbers from the TypeScript game and
// from C++ on every platform. sfc32 with PractRand's step and three-word seeding, and named
// child streams from fork(name, attempt). Worldgen keeps Pcg32 and SplitMix64.
//
// The core is integer math, nextDouble scales by a power of two, and weighted uses only IEEE
// addition and one multiply, so nothing depends on the compiler or the optimisation level.
// That assumes denormals aren't flushed to zero (FTZ or DAZ on x64, FPCR.FZ on arm64): with
// them on, weighted reads a subnormal weight or total as zero where Rng.ts doesn't. salal sets
// neither. Golden tests pin the algorithm, the fork hash and each method's draw count, the same
// values Rng.test.ts pins; changing any of them moves every seed in both implementations and
// needs a kRngVersion bump in both. The design record is dual-deck-builder's
// docs/AI_TECHNICAL_DECISIONS/seeded-prng.md.
//
// A stream is its uint32 seed. There is no default constructor, so nothing gets an unseeded
// stream by accident (freshSeed() in FreshSeed.h mints a root seed), and no copy: a copy
// would replay the stream from wherever it had reached, which is saving a stream mid-sequence,
// and the TypeScript has no way to do that either. Pass an Rng by reference. A move hands the
// stream over but leaves the source drawing the same numbers, so never draw from a moved-from
// Rng.
//
// Rng doesn't model UniformRandomBitGenerator, on purpose: std::shuffle and the <random>
// distributions are implementation-defined, so libstdc++, libc++ and the MSVC STL would draw
// different results from one stream. Use Rng's own methods.
//
// Bad arguments are rejected in two ways. Those that would be undefined behaviour (a range
// with no values or more than 2^32, an empty pick or weighted) fail in every build, printing
// the message and aborting. Those where Rng.ts would throw but C++ has a defined result (an
// unsafe-integer bound, an attempt past 2^32 - 1, a name that isn't UTF-8, a weight that
// isn't finite and >= 0) assert, as elsewhere in foundation, so NDEBUG compiles them out.
// What Rng.ts rejects by type doesn't compile: a seed, an attempt or a bound has to be an
// integer type (not bool or a character type), a name a std::string_view, a weight a double.

#include <bit>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <limits>
#include <ranges>
#include <string_view>
#include <utility>

namespace foundation {

	// Names what a seed draws. The goldens assert it, beside RNG_VERSION in Rng.test.ts.
	inline constexpr uint32_t kRngVersion = 1;

	template <typename T>
	struct WeightedEntry {
		T value;
		// Finite and >= 0. A zero weight is never chosen.
		double weight;
	};

	namespace detail {

		// The integer types std::in_range and std::cmp_less take: not bool or the character types.
		template <typename T>
		concept RngInteger = std::integral<T> && !std::same_as<T, bool> && !std::same_as<T, char> && !std::same_as<T, wchar_t> &&
							 !std::same_as<T, char8_t> && !std::same_as<T, char16_t> && !std::same_as<T, char32_t>;

		template <typename Entry>
		struct WeightedEntryValue {};
		template <typename T>
		struct WeightedEntryValue<WeightedEntry<T>> {
			using Type = T;
		};

		template <typename Range>
		concept WeightedRange =
			std::ranges::forward_range<Range> && requires { typename WeightedEntryValue<std::ranges::range_value_t<Range>>::Type; };

		// The checks that guard undefined behaviour. Always on, and the same on every platform.
		[[noreturn]] inline void rngCheckFailed(const char* message) {
			std::fprintf(stderr, "%s\n", message);
			std::abort();
		}

	} // namespace detail

	class Rng {
	  public:
		// Any integer, reduced modulo 2^32 as ECMAScript's ToUint32 reduces Rng.ts's seed, so -1 is
		// 0xffffffff. Only seeds within +-(2^53 - 1) have a Rng.ts counterpart: no JS number holds
		// one past that exactly, so ToUint32 sees it rounded. Rng.ts also coerces fractional, NaN and
		// infinite seeds; here they don't compile.
		template <detail::RngInteger Seed>
		explicit constexpr Rng(Seed seed)
			: Rng(static_cast<uint32_t>(seed), seededState(static_cast<uint32_t>(seed))) {}

		Rng(const Rng&) = delete;
		Rng& operator=(const Rng&) = delete;
		constexpr Rng(Rng&&) = default;
		constexpr Rng& operator=(Rng&&) = default;
		~Rng() = default;

		// The seed this stream was built from; Rng(seed()) replays it from the start.
		[[nodiscard]] constexpr uint32_t seed() const { return streamSeed; }

		// The child stream `name` at `attempt`, a pure function of this stream's seed, the name and
		// the attempt: it never reads or advances this stream, so siblings don't depend on fork order
		// or on how far this one has drawn. `attempt` is a retry or reroll count from 0 to 2^32 - 1.
		//
		// The name is UTF-8 and must be valid; it hashes as the UTF-16 code units Rng.ts hashes, so
		// any name gives the stream the same name gives there. A JS string can also hold a lone
		// surrogate, which valid UTF-8 can't. With NDEBUG a byte that isn't part of valid UTF-8
		// hashes as the lone surrogate 0xDC00 + byte (Python's surrogateescape), so such names stay
		// apart and match the JS string holding those surrogates.
		template <detail::RngInteger Attempt = uint32_t>
		[[nodiscard]] constexpr Rng fork(std::string_view name, Attempt attempt = 0) const {
			assert(std::in_range<uint32_t>(attempt) && "Rng::fork: attempt must be an integer from 0 to 2^32 - 1");
			return Rng(forkSeed(streamSeed, name, static_cast<uint32_t>(attempt)));
		}

		// A literal 0 or NULL would otherwise reach std::string_view's const char* constructor.
		template <typename... Rest>
		Rng fork(std::nullptr_t, Rest...) const = delete;

		// The next raw draw. One sfc32 step.
		constexpr uint32_t nextUInt32() { return step(state); }

		// A double in [0, 1), a multiple of 2^-32 (Rng.ts's float()). One draw, unlike
		// Pcg32::nextDouble's two.
		constexpr double nextDouble() { return static_cast<double>(nextUInt32()) / static_cast<double>(kUInt32Count); }

		// A float in [0, 1), a multiple of 2^-24 from the draw's top 24 bits. One draw. Rng.ts has no
		// counterpart; rounding nextDouble() to float would give 1.0F for the top 128 draws.
		constexpr float nextFloat() { return static_cast<float>(nextUInt32() >> 8) * 0x1p-24F; }

		// An integer in [min, max], both ends inclusive, with no modulo bias (Rng.ts's int()). The
		// bounds are within +-(2^53 - 1), JavaScript's safe integers, and span at most 2^32 values.
		// One draw, or more on a rejection, which happens with probability below span / 2^32.
		template <detail::RngInteger Integer>
		constexpr Integer nextInt(Integer min, Integer max) {
			// In uint64_t, where wrapping is defined: min > max or the full 64-bit range gives a span of
			// 0. The explicit min <= max also catches a reversed range more than 2^64 - 2^32 wide,
			// which wraps back to a small span.
			const uint64_t span = static_cast<uint64_t>(max) - static_cast<uint64_t>(min) + 1;
			check(std::cmp_less_equal(min, max) && span - 1 < kUInt32Count, "Rng::nextInt: [min, max] must hold 1 to 2^32 values");
			assert(
				std::cmp_greater_equal(min, -kMaxSafeInteger) && std::cmp_less_equal(max, kMaxSafeInteger) &&
				"Rng::nextInt: bounds must be safe integers"
			);
			return static_cast<Integer>(static_cast<uint64_t>(min) + below(span));
		}

		// A uniformly chosen item, drawing as nextInt(0, size - 1) does. A reference into the range,
		// or a copy when the range is a temporary that owns its items. Fails on an empty range.
		template <typename Range>
			requires std::ranges::random_access_range<Range> && std::ranges::sized_range<Range>
		constexpr decltype(auto) pick(Range&& items) {
			const auto count = static_cast<uint64_t>(std::ranges::size(items));
			check(count - 1 < kUInt32Count, "Rng::pick: the range must hold 1 to 2^32 items");
			const auto item = std::ranges::begin(items) + static_cast<std::ranges::range_difference_t<Range>>(below(count));
			if constexpr (std::ranges::borrowed_range<Range>) {
				return *item;
			} else {
				return std::ranges::range_value_t<Range>(std::ranges::iter_move(item));
			}
		}

		// Shuffles in place, Fisher-Yates from the last index down, drawing as nextInt(0, index) for
		// each index above 0. A caller that needs the original order shuffles a copy. A temporary
		// that owns its items doesn't compile, since the shuffle would be thrown away with it.
		template <typename Range>
			requires std::ranges::random_access_range<Range> && std::ranges::sized_range<Range> && std::ranges::borrowed_range<Range> &&
					 std::permutable<std::ranges::iterator_t<Range>>
		constexpr void shuffle(Range&& items) {
			using Difference = std::ranges::range_difference_t<Range>;
			const auto size = static_cast<uint64_t>(std::ranges::size(items));
			check(size <= kUInt32Count, "Rng::shuffle: the range must hold at most 2^32 items");
			const auto first = std::ranges::begin(items);
			for (auto count = size; count > 1; --count) {
				const uint32_t other = below(count);
				std::ranges::iter_swap(first + static_cast<Difference>(count - 1), first + static_cast<Difference>(other));
			}
		}

		// The value of an entry chosen with probability weight / total weight, by value as Rng.ts
		// returns it. One draw. Fails on an empty range; asserts on a weight that isn't finite and
		// >= 0, or a total that is zero or overflows.
		template <detail::WeightedRange Range>
		constexpr typename detail::WeightedEntryValue<std::ranges::range_value_t<Range>>::Type weighted(Range&& entries) {
			const auto first = std::ranges::begin(entries);
			const auto last = std::ranges::end(entries);
			check(first != last, "Rng::weighted: no entries to choose from");
			auto   lastPositive = first;
			double total = 0.0;
			for (auto entry = first; entry != last; ++entry) {
				const double weight = (*entry).weight;
				assert(weight >= 0.0 && weight < kInfinity && "Rng::weighted: a weight must be a finite number >= 0");
				total += weight;
				if (weight > 0.0) {
					lastPositive = entry;
				}
			}
			assert(total > 0.0 && total < kInfinity && "Rng::weighted: the weights must sum to a finite number above 0");
			// A zero weight can't carry `cumulative` past the target, and stopping at the last positive
			// weight keeps a target that rounds up to a subnormal total off trailing zeros.
			const double target = nextDouble() * total;
			double		 cumulative = 0.0;
			for (auto entry = first; entry != lastPositive; ++entry) {
				cumulative += (*entry).weight;
				if (target < cumulative) {
					return (*entry).value;
				}
			}
			return (*lastPositive).value;
		}

	  private:
		// sfc32's words, with PractRand's names.
		struct State {
			uint32_t a;
			uint32_t b;
			uint32_t c;
			uint32_t counter;
		};

		static constexpr uint64_t kUInt32Count = uint64_t{1} << 32;
		static constexpr int64_t  kMaxSafeInteger = (int64_t{1} << 53) - 1;
		static constexpr double	  kInfinity = std::numeric_limits<double>::infinity();
		// The SplitMix increment that spaces the three seed words apart.
		static constexpr uint32_t kGoldenGamma = 0x9e3779b9;
		// Draws discarded after seeding, as PractRand's sfc32 seed(s1, s2, s3) does.
		static constexpr int kWarmupDraws = 15;

		uint32_t streamSeed;
		State	 state;

		// Tests reach draws a seeded stream almost never makes by building one from raw state.
		friend struct RngTestAccess;

		constexpr Rng(uint32_t seed, State seeded)
			: streamSeed(seed),
			  state(seeded) {}

		// Fails in every build: these guard undefined behaviour, not just parity with Rng.ts.
		static constexpr void check(bool condition, const char* message) {
			if (!condition) {
				detail::rngCheckFailed(message);
			}
		}

		static constexpr uint32_t step(State& words) {
			const uint32_t result = words.a + words.b + words.counter;
			words.a = words.b ^ (words.b >> 9);
			words.b = words.c + (words.c << 3);
			words.c = std::rotl(words.c, 21) + result;
			words.counter += 1;
			return result;
		}

		static constexpr State seededState(uint32_t seed) {
			State words{
				.a = fmix32(seed + kGoldenGamma),
				.b = fmix32(seed + 2 * kGoldenGamma),
				.c = fmix32(seed + 3 * kGoldenGamma),
				.counter = 1,
			};
			for (int draw = 0; draw < kWarmupDraws; ++draw) {
				step(words);
			}
			return words;
		}

		// A uniform integer in [0, span) for span in [1, 2^32]: draw % span, redrawn while the draw
		// falls in the last, incomplete block of span values.
		constexpr uint32_t below(uint64_t span) {
			assert(span >= 1 && span <= kUInt32Count && "Rng: a range must hold 1 to 2^32 values");
			if (span == kUInt32Count) {
				return nextUInt32();
			}
			const auto divisor = static_cast<uint32_t>(span);
			for (;;) {
				const uint32_t draw = nextUInt32();
				const uint32_t value = draw % divisor;
				if (draw - value <= 0U - divisor) {
					return value;
				}
			}
		}

		struct Utf8Sequence {
			uint32_t codePoint;
			// 0 when the bytes aren't well-formed UTF-8.
			size_t length;
		};

		// The code point at `index`, accepting only the well-formed sequences of the Unicode
		// standard's table 3-7: no overlong forms, no encoded surrogates, nothing past U+10FFFF.
		// utils/Utf8.h substitutes U+FFFD for bad bytes, which would give them all one stream.
		static constexpr Utf8Sequence decodeUtf8(std::string_view text, size_t index) {
			const auto byteAt = [&](size_t offset) {
				return static_cast<uint8_t>(text[index + offset]);
			};
			const uint8_t lead = byteAt(0);
			if (lead < 0x80) {
				return {.codePoint = lead, .length = 1};
			}
			size_t	 length = 0;
			uint32_t codePoint = 0;
			uint8_t	 low = 0x80;
			uint8_t	 high = 0xBF;
			if (lead >= 0xC2 && lead <= 0xDF) {
				length = 2;
				codePoint = lead & 0x1FU;
			} else if (lead >= 0xE0 && lead <= 0xEF) {
				length = 3;
				codePoint = lead & 0x0FU;
				low = lead == 0xE0 ? 0xA0 : 0x80;
				high = lead == 0xED ? 0x9F : 0xBF;
			} else if (lead >= 0xF0 && lead <= 0xF4) {
				length = 4;
				codePoint = lead & 0x07U;
				low = lead == 0xF0 ? 0x90 : 0x80;
				high = lead == 0xF4 ? 0x8F : 0xBF;
			} else {
				return {.codePoint = 0, .length = 0};
			}
			if (text.size() - index < length) {
				return {.codePoint = 0, .length = 0};
			}
			for (size_t offset = 1; offset < length; ++offset) {
				const uint8_t trail = byteAt(offset);
				if (trail < low || trail > high) {
					return {.codePoint = 0, .length = 0};
				}
				codePoint = (codePoint << 6) | (trail & 0x3FU);
				low = 0x80;
				high = 0xBF;
			}
			return {.codePoint = codePoint, .length = length};
		}

		// MurmurHash3_x86_32 seeded with the parent's seed, over the name's UTF-16 code units and
		// then the attempt, each as one little-endian uint32 block, so the input is
		// 4 * (code units + 1) bytes and has no tail.
		static constexpr uint32_t forkSeed(uint32_t parentSeed, std::string_view name, uint32_t attempt) {
			uint32_t hash = parentSeed;
			uint32_t codeUnits = 0;
			size_t	 index = 0;
			while (index < name.size()) {
				const Utf8Sequence sequence = decodeUtf8(name, index);
				assert(sequence.length != 0 && "Rng::fork: the name is not valid UTF-8");
				if (sequence.length == 0) {
					hash = murmurBlock(hash, 0xDC00 + static_cast<uint8_t>(name[index]));
					codeUnits += 1;
					index += 1;
					continue;
				}
				index += sequence.length;
				if (sequence.codePoint < 0x10000) {
					hash = murmurBlock(hash, sequence.codePoint);
					codeUnits += 1;
				} else {
					const uint32_t offset = sequence.codePoint - 0x10000;
					hash = murmurBlock(hash, 0xD800 + (offset >> 10));
					hash = murmurBlock(hash, 0xDC00 + (offset & 0x3FF));
					codeUnits += 2;
				}
			}
			hash = murmurBlock(hash, attempt);
			return fmix32(hash ^ ((codeUnits + 1) * 4));
		}

		static constexpr uint32_t murmurBlock(uint32_t hash, uint32_t block) {
			uint32_t mixed = block * 0xcc9e2d51;
			mixed = std::rotl(mixed, 15);
			mixed *= 0x1b873593;
			return std::rotl(hash ^ mixed, 13) * 5 + 0xe6546b64;
		}

		// MurmurHash3's 32-bit finalizer, a bijection on uint32.
		static constexpr uint32_t fmix32(uint32_t value) {
			value ^= value >> 16;
			value *= 0x85ebca6b;
			value ^= value >> 13;
			value *= 0xc2b2ae35;
			value ^= value >> 16;
			return value;
		}
	};

} // namespace foundation
