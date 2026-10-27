#include "Rng.h"
#include "FreshSeed.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <gtest/gtest.h>
#include <limits>
#include <map>
#include <numeric>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

// A port of dual-deck-builder's src/renderer/game/core/Rng.test.ts. The golden values are
// copied from it unchanged, so a mismatch here means the two implementations disagree.

namespace foundation {

	struct RngTestAccess {
		static Rng fromState(uint32_t seed, uint32_t a, uint32_t b, uint32_t c, uint32_t counter) {
			return Rng(seed, Rng::State{.a = a, .b = b, .c = c, .counter = counter});
		}
	};

} // namespace foundation

using namespace foundation;

namespace {

	constexpr uint32_t kSeed = 20261006;
	constexpr uint32_t kUInt32Max = 0xffffffff;
	constexpr int64_t  kTwo31 = int64_t{1} << 31;
	constexpr int64_t  kTwo32 = int64_t{1} << 32;
	constexpr int64_t  kTwo53 = int64_t{1} << 53;
	constexpr int64_t  kMaxSafeInteger = kTwo53 - 1;
	constexpr int64_t  kMinSafeInteger = -kMaxSafeInteger;

	// Chi-squared critical values at p = 0.001, by degrees of freedom. The draws are fixed by the
	// seed, so a pass holds until the algorithm or a seed changes.
	const std::map<int, double> kChiSquaredCritical{{3, 16.27}, {4, 18.47}, {5, 20.52}, {9, 27.88}};

	double toDouble(uint32_t draw) {
		return static_cast<double>(draw) / 4294967296.0;
	}

	// The next `count` raw draws.
	std::vector<uint32_t> take(Rng& rng, int count = 16) {
		std::vector<uint32_t> draws;
		for (int draw = 0; draw < count; ++draw) {
			draws.push_back(rng.nextUInt32());
		}
		return draws;
	}

	std::vector<uint32_t> take(Rng&& rng, int count = 16) {
		return take(rng, count);
	}

	std::vector<double> doubles(Rng&& rng, int count) {
		std::vector<double> values;
		for (int draw = 0; draw < count; ++draw) {
			values.push_back(rng.nextDouble());
		}
		return values;
	}

	// Pearson's statistic for counts against an even spread of their total, or against `expected`.
	double chiSquared(const std::vector<int>& counts, const std::vector<double>& expected = {}) {
		const double total = std::accumulate(counts.begin(), counts.end(), 0.0);
		double		 statistic = 0.0;
		for (size_t index = 0; index < counts.size(); ++index) {
			const double expectedCount = expected.empty() ? total / static_cast<double>(counts.size()) : expected[index];
			const double difference = counts[index] - expectedCount;
			statistic += difference * difference / expectedCount;
		}
		return statistic;
	}

	// Counts of values in [0, 1) across `bins` equal bins.
	std::vector<int> histogram(const std::vector<double>& values, int bins) {
		std::vector<int> counts(static_cast<size_t>(bins), 0);
		for (const double value : values) {
			counts[static_cast<size_t>(std::floor(value * bins))] += 1;
		}
		return counts;
	}

	double correlation(const std::vector<double>& xs, const std::vector<double>& ys) {
		const double meanX = std::accumulate(xs.begin(), xs.end(), 0.0) / static_cast<double>(xs.size());
		const double meanY = std::accumulate(ys.begin(), ys.end(), 0.0) / static_cast<double>(ys.size());
		double		 covariance = 0.0;
		double		 varianceX = 0.0;
		double		 varianceY = 0.0;
		for (size_t index = 0; index < xs.size(); ++index) {
			const double dx = xs[index] - meanX;
			const double dy = ys[index] - meanY;
			covariance += dx * dy;
			varianceX += dx * dx;
			varianceY += dy * dy;
		}
		return covariance / std::sqrt(varianceX * varianceY);
	}

	uint32_t unxorshift9(uint32_t value) {
		return value ^ (value >> 9) ^ (value >> 18) ^ (value >> 27);
	}

	struct Scripted {
		Rng rng;
		// The draw after the script, so a call that took exactly the script leaves it next.
		uint32_t after;
	};

	// A real Rng, seed 0, whose next draws are `script`, to reach the edges a seeded stream almost
	// never hits (Rng.test.ts replaces next() instead). From words (a, b, 0, counter) sfc32 draws
	// a + b + counter, then (b ^ b >> 9) + counter + 1, then 9 times the first draw + counter + 2,
	// so any script of up to three draws has a state that makes it.
	Scripted scripted(const std::vector<uint32_t>& script) {
		EXPECT_LE(script.size(), 3U) << "scripted() solves for at most three draws";
		const auto at = [&](size_t index) {
			return index < script.size() ? script[index] : 0U;
		};
		const uint32_t counter = script.size() == 3 ? at(2) - 9 * at(0) - 2 : 0;
		const uint32_t b = script.size() >= 2 ? unxorshift9(at(1) - counter - 1) : 0;
		const uint32_t a = at(0) - b - counter;
		Rng			   check = RngTestAccess::fromState(0, a, b, 0, counter);
		for (const uint32_t draw : script) {
			EXPECT_EQ(check.nextUInt32(), draw) << "the scripted state doesn't make its script";
		}
		return {.rng = RngTestAccess::fromState(0, a, b, 0, counter), .after = check.nextUInt32()};
	}

	template <typename Attempt>
	concept ForkTakesAttempt = requires(const Rng& rng, Attempt attempt) { rng.fork("terrain", attempt); };

	template <typename Name>
	concept ForkTakesName = requires(const Rng& rng, Name name) { rng.fork(name); };

	template <typename Bound>
	concept NextIntTakes = requires(Rng& rng, Bound bound) { rng.nextInt(bound, bound); };

	const std::vector<WeightedEntry<std::string_view>> kStops{
		{.value = "raider", .weight = 3},
		{.value = "checkpoint", .weight = 2},
		{.value = "none", .weight = 0},
		{.value = "wreck", .weight = 1},
	};

} // namespace

// ============================================================================
// The sequence
// ============================================================================

TEST(RngTests, RepeatsExactlyForTheSameSeed) {
	EXPECT_EQ(take(Rng(kSeed), 64), take(Rng(kSeed), 64));
}

TEST(RngTests, MatchesThePinnedSfc32Sequence) {
	// Every map depends on these; an intended change bumps kRngVersion.
	EXPECT_EQ(kRngVersion, 1U);
	EXPECT_EQ(
		take(Rng(kSeed), 8),
		(std::vector<uint32_t>{0xbdd943ed, 0x34fa9b12, 0xa35b1c6f, 0x3a82c513, 0x360a65a6, 0xdaee38e7, 0xd23f8ec0, 0xf769aa1b})
	);
	EXPECT_EQ(take(Rng(0), 4), (std::vector<uint32_t>{0x4fc89f56, 0x94fffb63, 0xa38c1db3, 0x48345ebb}));
	EXPECT_EQ(take(Rng(kUInt32Max), 4), (std::vector<uint32_t>{0xa54b7665, 0x1108a63e, 0xc16a67f8, 0x53c7c08d}));
}

TEST(RngTests, PinsTheDerivedDraws) {
	EXPECT_EQ(kRngVersion, 1U);
	Rng rng(kSeed);
	EXPECT_EQ(rng.nextDouble(), toDouble(0xbdd943ed));
	EXPECT_EQ(rng.nextDouble(), toDouble(0x34fa9b12));
	EXPECT_EQ(rng.nextDouble(), toDouble(0xa35b1c6f));

	std::vector<int> dice;
	for (int roll = 0; roll < 10; ++roll) {
		dice.push_back(rng.nextInt(1, 6));
	}
	EXPECT_EQ(dice, (std::vector<int>{6, 5, 6, 5, 4, 3, 4, 4, 2, 6}));

	std::vector<int> offsets;
	for (int roll = 0; roll < 5; ++roll) {
		offsets.push_back(rng.nextInt(-50, 50));
	}
	EXPECT_EQ(offsets, (std::vector<int>{-33, -29, -34, -41, -41}));

	const std::array<std::string_view, 4> kinds{"raider", "wreck", "find", "hazard"};
	std::vector<std::string_view>		  picks;
	for (int roll = 0; roll < 5; ++roll) {
		picks.push_back(rng.pick(kinds));
	}
	EXPECT_EQ(picks, (std::vector<std::string_view>{"find", "hazard", "raider", "hazard", "hazard"}));

	std::array<int, 10> cards{0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
	rng.shuffle(cards);
	EXPECT_EQ(cards, (std::array<int, 10>{0, 7, 1, 2, 5, 6, 8, 4, 3, 9}));

	std::vector<std::string_view> stops;
	for (int roll = 0; roll < 12; ++roll) {
		stops.push_back(rng.weighted(kStops));
	}
	EXPECT_EQ(
		stops,
		(std::vector<std::string_view>{
			"raider",
			"checkpoint",
			"raider",
			"raider",
			"raider",
			"checkpoint",
			"checkpoint",
			"raider",
			"wreck",
			"raider",
			"wreck",
			"wreck",
		})
	);
}

TEST(RngTests, DiffersBetweenNeighbouringSeeds) {
	EXPECT_NE(take(Rng(1)), take(Rng(2)));
}

TEST(RngTests, ReducesIntegerSeedsModulo2To32) {
	// Rng.ts's ToUint32 on the integer cases of its coercion test; its 1.9, NaN and Infinity
	// cases are floating-point seeds, which don't compile here.
	const std::array<std::pair<int64_t, uint32_t>, 3> cases{{{-1, kUInt32Max}, {kTwo32 + 5, 5}, {-kTwo32 - 2, kUInt32Max - 1}}};
	for (const auto& [seed, reduced] : cases) {
		Rng rng(seed);
		EXPECT_EQ(rng.seed(), reduced) << seed;
		EXPECT_EQ(take(rng), take(Rng(reduced))) << seed;
	}
}

TEST(RngTests, TakesOnlyIntegerSeeds) {
	// Rng.ts rejects undefined, null, '123', true and [5] at run time; here none compiles, and
	// neither does a floating-point seed or a stream with no seed at all.
	static_assert(!std::is_default_constructible_v<Rng>);
	static_assert(!std::is_constructible_v<Rng, std::nullptr_t>);
	static_assert(!std::is_constructible_v<Rng, const char*>);
	static_assert(!std::is_constructible_v<Rng, bool>);
	static_assert(!std::is_constructible_v<Rng, std::vector<int>>);
	static_assert(!std::is_constructible_v<Rng, std::optional<uint32_t>>);
	static_assert(!std::is_constructible_v<Rng, double>);
	static_assert(std::is_constructible_v<Rng, int>);
	static_assert(std::is_constructible_v<Rng, uint64_t>);
	// A copy would replay the stream from where it had reached; Rng.ts can't copy one either.
	static_assert(!std::is_copy_constructible_v<Rng>);
	static_assert(!std::is_copy_assignable_v<Rng>);
	static_assert(std::is_nothrow_move_constructible_v<Rng>);
}

TEST(RngTests, EvaluatesAtCompileTime) {
	static_assert([] {
		Rng rng(kSeed);
		return rng.nextUInt32();
	}() == 0xbdd943ed);
	static_assert(Rng(kSeed).fork("terrain").seed() == 0x7843fb4e);
	static_assert(Rng(kSeed).fork("car \xF0\x9F\x9A\x97", 3).seed() == 0xddc4a4af);
}

// ============================================================================
// fork
// ============================================================================

TEST(RngForkTests, DerivesThePinnedChildSeeds) {
	EXPECT_EQ(kRngVersion, 1U);
	const Rng root(kSeed);
	EXPECT_EQ(root.fork("terrain").seed(), 0x7843fb4eU);
	EXPECT_EQ(root.fork("terrain", 1).seed(), 0xb60805a9U);
	EXPECT_EQ(root.fork("terrain", kUInt32Max).seed(), 0x855371eeU);
	EXPECT_EQ(root.fork("stop:123", 2).seed(), 0xc75516e2U);
	EXPECT_EQ(root.fork("").seed(), 0xf84e7cdeU);
	// "car \u{1F697}": the car is one code point, a surrogate pair in UTF-16 and four bytes in UTF-8.
	EXPECT_EQ(root.fork("car \xF0\x9F\x9A\x97", 3).seed(), 0xddc4a4afU);
	EXPECT_EQ(take(root.fork("terrain", 1), 4), (std::vector<uint32_t>{0xc20035bd, 0x07fa6c27, 0xd69e7744, 0x50e352a4}));
}

TEST(RngForkTests, PinsNamesBeyondAsciiAgainstRngTs) {
	// Not in Rng.test.ts: seeds Rng.ts gives these names at RNG_VERSION 1, one for each UTF-8
	// sequence length and both ends of the astral planes.
	EXPECT_EQ(kRngVersion, 1U);
	const Rng root(kSeed);
	EXPECT_EQ(root.fork("\xC3\xA9").seed(), 0x94802169U);				  // é
	EXPECT_EQ(root.fork("\xE4\xB8\x96\xE7\x95\x8C").seed(), 0x0e724754U); // 世界
	EXPECT_EQ(root.fork("\xEF\xBF\xBD").seed(), 0xfeab641dU);			  // U+FFFD
	EXPECT_EQ(root.fork("\xF0\x90\x80\x80").seed(), 0x081a3c10U);		  // U+10000
	EXPECT_EQ(root.fork("\xF4\x8F\xBF\xBF", 7).seed(), 0xe75c69b8U);	  // U+10FFFF
	// Мёртвая дорога
	EXPECT_EQ(
		root.fork("\xD0\x9C\xD1\x91\xD1\x80\xD1\x82\xD0\xB2\xD0\xB0\xD1\x8F \xD0\xB4\xD0\xBE\xD1\x80\xD0\xBE\xD0\xB3\xD0\xB0", 1).seed(),
		0xb9daca8fU
	);
	EXPECT_EQ(root.fork(std::string(10000, 'x'), 1).seed(), 0xfac9bf17U);
}

TEST(RngForkTests, GivesTheSameStreamForTheSameNameAndAttemptWithAttemptZeroByDefault) {
	const Rng root(kSeed);
	EXPECT_EQ(take(root.fork("growth", 3)), take(root.fork("growth", 3)));
	EXPECT_EQ(take(root.fork("growth")), take(root.fork("growth", 0)));
}

TEST(RngForkTests, DoesNotDependOnHowFarTheParentHasDrawn) {
	Rng drawn(kSeed);
	take(drawn, 1000);
	EXPECT_EQ(take(drawn.fork("terrain", 2)), take(Rng(kSeed).fork("terrain", 2)));
}

TEST(RngForkTests, NeverAdvancesTheParentOrASiblingSoForkOrderDoesNotMatter) {
	Rng root(kSeed);
	Rng terrain = root.fork("terrain");
	Rng growth = root.fork("growth");
	Rng stop = root.fork("stops").fork("stop:7", 1);
	take(terrain, 100);
	EXPECT_EQ(take(root), take(Rng(kSeed)));
	EXPECT_EQ(take(growth), take(Rng(kSeed).fork("growth")));
	EXPECT_EQ(take(stop), take(Rng(kSeed).fork("stops").fork("stop:7", 1)));
}

TEST(RngForkTests, GivesEachAttemptItsOwnStream) {
	const Rng		   root(kSeed);
	std::set<uint32_t> seeds;
	for (uint32_t attempt = 0; attempt < 32; ++attempt) {
		seeds.insert(root.fork("growth", attempt).seed());
	}
	EXPECT_EQ(seeds.size(), 32U);
	EXPECT_NE(take(root.fork("growth", 1)), take(root.fork("growth", 0)));
}

TEST(RngForkTests, GivesNearIdenticalNamesTheirOwnStreams) {
	const Rng							   root(kSeed);
	const std::array<std::string_view, 12> names{
		"stop:1", "stop:2", "stop:12", "stop:21", "stop:01", "Stop:1", "stop:1 ", " stop:1", "stop1", "terrain", "terrain2", ""
	};
	std::set<uint32_t> seeds;
	for (const std::string_view name : names) {
		seeds.insert(root.fork(name).seed());
	}
	EXPECT_EQ(seeds.size(), names.size());
	// 'b' is code unit 98, so a name and an attempt must not run together.
	EXPECT_NE(root.fork("a", 98).seed(), root.fork("ab").seed());
}

TEST(RngForkTests, TakesAnyValidUtf8Name) {
	const Rng		   root(kSeed);
	std::set<uint32_t> seeds;
	for (const std::string& name : {
			 std::string(),
			 std::string("\xD0\x9C\xD1\x91\xD1\x80\xD1\x82\xD0\xB2\xD0\xB0\xD1\x8F \xD0\xB4\xD0\xBE\xD1\x80\xD0\xBE\xD0\xB3\xD0\xB0"),
			 std::string("car \xF0\x9F\x9A\x97"),
			 std::string(10000, 'x'),
		 }) {
		seeds.insert(root.fork(name, 1).seed());
	}
	EXPECT_EQ(seeds.size(), 4U);
	// Rng.ts also gives the lone surrogates U+D800 and U+DC00 streams apart from U+FFFD's. Valid
	// UTF-8 can't hold a lone surrogate, so their encodings are rejected here
	// (RngDeathTest.ForkRejectsANameThatIsNotValidUtf8), and U+FFFD is pinned as an ordinary name.
}

TEST(RngForkTests, GivesEveryCampaignSeedItsOwnStageStream) {
	// For a fixed name and attempt the child seed is a bijection of the parent's.
	std::set<uint32_t> seeds;
	for (uint32_t seed = 0; seed < 10000; ++seed) {
		seeds.insert(Rng(seed).fork("terrain").seed());
	}
	EXPECT_EQ(seeds.size(), 10000U);
}

TEST(RngForkTests, IsRebuiltFromItsSeedAlone) {
	Rng stop = Rng(kSeed).fork("stop:123", 2);
	EXPECT_EQ(take(Rng(stop.seed())), take(stop));
}

TEST(RngForkTests, DerivesTheChildFromTheSeedWhateverTheParentsState) {
	// Rng.test.ts forks a parent whose next() throws; here the parent's words are arbitrary.
	const Rng parent = RngTestAccess::fromState(0, 0x12345678, 0x9abcdef0, 0x0badf00d, 99);
	EXPECT_EQ(take(parent.fork("terrain", 2)), take(Rng(0).fork("terrain", 2)));
}

TEST(RngForkTests, TakesOnlyIntegerAttemptsAndStringNames) {
	// Rng.ts rejects 1.5, NaN and Infinity at run time; here a floating-point attempt doesn't
	// compile. Its -1 and 2^32 are RngDeathTest.ForkRejectsAnAttemptOutsideUInt32.
	static_assert(ForkTakesAttempt<int>);
	static_assert(ForkTakesAttempt<int64_t>);
	static_assert(ForkTakesAttempt<uint32_t>);
	static_assert(!ForkTakesAttempt<double>);
	static_assert(!ForkTakesAttempt<bool>);
	static_assert(!ForkTakesAttempt<char>);
	// Rng.ts rejects an explicit undefined so a roll count missing from a save can't quietly
	// become 0; a std::optional count doesn't compile.
	static_assert(!ForkTakesAttempt<std::optional<uint32_t>>);
	// Rng.ts rejects the names 5, 7, null, undefined and ['terrain'] at run time. A nullptr
	// name compiles, as std::string_view's constructor takes one until C++23.
	static_assert(ForkTakesName<std::string>);
	static_assert(ForkTakesName<const char*>);
	static_assert(!ForkTakesName<int>);
	static_assert(!ForkTakesName<std::vector<std::string>>);
}

// ============================================================================
// nextDouble
// ============================================================================

TEST(RngTests, NextDoubleSpansZeroToOneAndNeverReachesOne) {
	EXPECT_EQ(scripted({0}).rng.nextDouble(), 0.0);
	EXPECT_EQ(scripted({kUInt32Max}).rng.nextDouble(), 1.0 - std::ldexp(1.0, -32));
}

// ============================================================================
// nextInt
// ============================================================================

TEST(RngIntTests, StaysInsideTheRangeAndReachesBothEnds) {
	const std::array<std::pair<int, int>, 9> ranges{{{0, 0}, {7, 7}, {-7, -7}, {0, 1}, {-1, 0}, {1, 6}, {-3, 3}, {-10, -1}, {250, 260}}};
	for (const auto& [min, max] : ranges) {
		Rng			  rng(kSeed);
		std::set<int> seen;
		for (int draw = 0; draw < 2000; ++draw) {
			seen.insert(rng.nextInt(min, max));
		}
		std::vector<int> every(static_cast<size_t>(max - min + 1));
		std::iota(every.begin(), every.end(), min);
		EXPECT_EQ(std::vector<int>(seen.begin(), seen.end()), every) << min << ", " << max;
	}
}

TEST(RngIntTests, StaysInsideTheWidestSpansAndTheSafeIntegerLimits) {
	const std::array<std::pair<int64_t, int64_t>, 5> ranges{{
		{-kTwo31, kTwo31 - 1},
		{0, kUInt32Max},
		{0, kTwo31},
		{kMaxSafeInteger - kUInt32Max, kMaxSafeInteger},
		{kMinSafeInteger, kMinSafeInteger + 3},
	}};
	for (const auto& [min, max] : ranges) {
		Rng rng(kSeed);
		for (int draw = 0; draw < 1000; ++draw) {
			const int64_t value = rng.nextInt(min, max);
			ASSERT_GE(value, min) << min << ", " << max;
			ASSERT_LE(value, max) << min << ", " << max;
		}
	}
}

TEST(RngIntTests, RedrawsInsteadOfWrappingWhenADrawLandsInTheLastIncompleteBlock) {
	// 2^32 = 6 * 715827882 + 4: the top four draws can't map evenly onto six values.
	Scripted rejecting = scripted({kUInt32Max - 3, kUInt32Max, 7});
	EXPECT_EQ(rejecting.rng.nextInt(0, 5), 1);
	EXPECT_EQ(rejecting.rng.nextUInt32(), rejecting.after);
	EXPECT_EQ(scripted({kUInt32Max - 4}).rng.nextInt(0, 5), 5);
	// A power-of-two span divides 2^32 evenly, so even the top draw sits in a complete block.
	Scripted top = scripted({kUInt32Max});
	EXPECT_EQ(top.rng.nextInt(int64_t{0}, kTwo31 - 1), kTwo31 - 1);
	EXPECT_EQ(top.rng.nextUInt32(), top.after);
	// Span 2^31 + 1 rejects every draw above 2^31, so nextInt returns the first raw draw at or below it.
	std::vector<int64_t> raw;
	for (const uint32_t draw : take(Rng(kSeed), 64)) {
		if (draw <= kTwo31) {
			raw.push_back(draw);
		}
	}
	Rng					 rng(kSeed);
	std::vector<int64_t> values;
	for (int roll = 0; roll < 10; ++roll) {
		values.push_back(rng.nextInt(int64_t{0}, kTwo31));
	}
	EXPECT_EQ(values, std::vector<int64_t>(raw.begin(), raw.begin() + 10));
}

TEST(RngIntTests, ReturnsMinPlusTheRawDrawOverAFull2To32SpanOneDrawEach) {
	const std::vector<uint32_t> raw = take(Rng(kSeed), 4);
	Rng							rng(kSeed);
	EXPECT_EQ(rng.nextInt(int64_t{0}, int64_t{kUInt32Max}), int64_t{raw[0]});
	EXPECT_EQ(rng.nextInt(-kTwo31, kTwo31 - 1), int64_t{raw[1]} - kTwo31);
	EXPECT_EQ(rng.nextInt(int64_t{1}, kTwo32), int64_t{raw[2]} + 1);
	EXPECT_EQ(rng.nextUInt32(), raw[3]);
}

TEST(RngIntTests, TakesOnlyIntegerBounds) {
	// Rng.ts rejects a fractional bound, NaN, Infinity and a string bound at run time; here none
	// compiles. Its range errors are RngDeathTest.NextIntRejectsBadBounds.
	static_assert(NextIntTakes<int>);
	static_assert(NextIntTakes<int64_t>);
	static_assert(!NextIntTakes<double>);
	static_assert(!NextIntTakes<bool>);
	static_assert(!NextIntTakes<const char*>);
}

// ============================================================================
// pick
// ============================================================================

TEST(RngPickTests, ReturnsEveryItemOverEnoughDrawsAndOnlyThose) {
	Rng									  rng(kSeed);
	const std::array<std::string_view, 3> items{"raider", "wreck", "find"};
	std::set<std::string_view>			  seen;
	for (int draw = 0; draw < 300; ++draw) {
		seen.insert(rng.pick(items));
	}
	EXPECT_EQ(seen, (std::set<std::string_view>{"find", "raider", "wreck"}));
}

// ============================================================================
// shuffle
// ============================================================================

TEST(RngShuffleTests, PermutesTheRangeInPlace) {
	std::vector<int> deck(52);
	std::iota(deck.begin(), deck.end(), 0);
	const std::vector<int> ordered = deck;
	Rng(kSeed).shuffle(deck);
	std::vector<int> sorted = deck;
	std::ranges::sort(sorted);
	EXPECT_EQ(sorted, ordered);
	EXPECT_NE(deck, ordered);
}

TEST(RngShuffleTests, LeavesEmptyAndSingleItemRangesAlone) {
	std::vector<std::string> empty;
	Rng(kSeed).shuffle(empty);
	EXPECT_TRUE(empty.empty());
	std::vector<std::string> only{"only"};
	Rng(kSeed).shuffle(only);
	EXPECT_EQ(only, (std::vector<std::string>{"only"}));
}

// ============================================================================
// weighted
// ============================================================================

TEST(RngWeightedTests, NeverChoosesAZeroWeight) {
	Rng												   rng(kSeed);
	const std::vector<WeightedEntry<std::string_view>> entries{
		{.value = "zero", .weight = 0},
		{.value = "a", .weight = 1},
		{.value = "zero", .weight = 0},
		{.value = "b", .weight = 2},
		{.value = "zero", .weight = 0},
	};
	std::set<std::string_view> seen;
	for (int draw = 0; draw < 10000; ++draw) {
		seen.insert(rng.weighted(entries));
	}
	EXPECT_EQ(seen, (std::set<std::string_view>{"a", "b"}));
}

TEST(RngWeightedTests, SkipsZeroWeightsAtTheLowestAndHighestDrawsEvenWhenTheTotalIsSubnormal) {
	// Only a total this small lets the top draw's target round up to the total and walk past the
	// last positive weight.
	const double									   smallest = std::numeric_limits<double>::denorm_min();
	const std::vector<WeightedEntry<std::string_view>> entries{
		{.value = "zero", .weight = 0},
		{.value = "first", .weight = smallest},
		{.value = "zero", .weight = 0},
		{.value = "last", .weight = smallest},
		{.value = "zero", .weight = 0},
	};
	EXPECT_EQ(scripted({0}).rng.weighted(entries), "first");
	EXPECT_EQ(scripted({kUInt32Max}).rng.weighted(entries), "last");
}

TEST(RngWeightedTests, ChoosesTheLaterEntryWhenTheTargetLandsExactlyOnARunningTotal) {
	// Draw 2^31 is the double 0.5, so the target is exactly 1, the first entry's running total.
	const std::vector<WeightedEntry<std::string_view>> entries{{.value = "first", .weight = 1}, {.value = "second", .weight = 1}};
	EXPECT_EQ(scripted({uint32_t{1} << 31}).rng.weighted(entries), "second");
}

// Rng.ts also rejects the weights "3", true, null and [1]. WeightedEntry's weight is a double, so
// a string or an array doesn't compile, and a bool converts as C++ converts one.

// ============================================================================
// Draw counts
// ============================================================================

TEST(RngDrawCountTests, TakesExactlyOneDrawForNextDoubleAndWeighted) {
	std::vector<WeightedEntry<int>> manyEntries;
	for (int index = 0; index < 100; ++index) {
		manyEntries.push_back({.value = index, .weight = static_cast<double>(index % 3)});
	}
	const std::vector<WeightedEntry<std::string_view>> oneEntry{{.value = "only", .weight = 1}};
	for (const uint32_t draw : {uint32_t{0}, uint32_t{1} << 31, kUInt32Max}) {
		Scripted forDouble = scripted({draw});
		forDouble.rng.nextDouble();
		EXPECT_EQ(forDouble.rng.nextUInt32(), forDouble.after) << "nextDouble() at draw " << draw;
		Scripted forOne = scripted({draw});
		forOne.rng.weighted(oneEntry);
		EXPECT_EQ(forOne.rng.nextUInt32(), forOne.after) << "weighted() over one entry at draw " << draw;
		Scripted forMany = scripted({draw});
		forMany.rng.weighted(manyEntries);
		EXPECT_EQ(forMany.rng.nextUInt32(), forMany.after) << "weighted() over 100 entries, a third of them zero, at draw " << draw;
	}
}

TEST(RngDrawCountTests, TakesOneDrawForNextIntAndPickAndAnotherForEachRejectedDraw) {
	const std::vector<uint32_t> reference = take(Rng(kSeed), 4);
	Rng							rng(kSeed);
	rng.nextInt(5, 5);
	rng.nextInt(-1000, 1000);
	rng.pick(std::array<std::string_view, 1>{"only"});
	EXPECT_EQ(rng.nextUInt32(), reference[3]);
	// kSeed's first draw is above 2^31, so nextInt(0, 2^31) rejects it and returns the second.
	Rng rejecting(kSeed);
	EXPECT_EQ(rejecting.nextInt(int64_t{0}, kTwo31), int64_t{reference[1]});
	EXPECT_EQ(rejecting.nextUInt32(), reference[2]);
	// 2^32 = 3 * 1431655765 + 1, so the top draw is the incomplete block for three items.
	Scripted picking = scripted({kUInt32Max, 4});
	EXPECT_EQ(picking.rng.pick(std::array<std::string_view, 3>{"a", "b", "c"}), "b");
	EXPECT_EQ(picking.rng.nextUInt32(), picking.after);
}

TEST(RngDrawCountTests, DrawsAsNextIntFromZeroToTheIndexForEachIndexAboveZeroToShuffle) {
	Rng						 rng(kSeed);
	Rng						 shadow(kSeed);
	std::vector<std::string> empty;
	rng.shuffle(empty);
	std::vector<std::string> only{"only"};
	rng.shuffle(only);
	std::array<int, 10> cards{0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
	rng.shuffle(cards);
	for (int index = 9; index > 0; --index) {
		shadow.nextInt(0, index);
	}
	EXPECT_EQ(rng.nextUInt32(), shadow.nextUInt32());
	// A rejected draw costs a shuffle a redraw too.
	Scripted			shuffling = scripted({kUInt32Max, 4, 0});
	std::array<char, 3> letters{'a', 'b', 'c'};
	shuffling.rng.shuffle(letters);
	EXPECT_EQ(letters, (std::array<char, 3>{'c', 'a', 'b'}));
	EXPECT_EQ(shuffling.rng.nextUInt32(), shuffling.after);
}

// ============================================================================
// Distribution
// ============================================================================

TEST(RngDistributionTests, SetsEachBitOfNextUInt32AboutHalfTheTime) {
	Rng					rng(kSeed);
	constexpr int		draws = 20000;
	std::array<int, 32> setCounts{};
	for (int draw = 0; draw < draws; ++draw) {
		const uint32_t value = rng.nextUInt32();
		for (size_t bit = 0; bit < 32; ++bit) {
			setCounts[bit] += static_cast<int>((value >> bit) & 1U);
		}
	}
	// 0.48 to 0.52 is more than five standard deviations either side.
	for (const int count : setCounts) {
		EXPECT_GT(count / static_cast<double>(draws), 0.48);
		EXPECT_LT(count / static_cast<double>(draws), 0.52);
	}
}

TEST(RngDistributionTests, SpreadsNextDoubleEvenlyOverZeroToOne) {
	const std::vector<double> values = doubles(Rng(kSeed), 100000);
	const double			  mean = std::accumulate(values.begin(), values.end(), 0.0) / static_cast<double>(values.size());
	EXPECT_LT(std::abs(mean - 0.5), 0.005);
	EXPECT_LT(chiSquared(histogram(values, 10)), kChiSquaredCritical.at(9));
}

TEST(RngDistributionTests, SpreadsNextIntEvenlyOverItsRange) {
	Rng				 rng(kSeed);
	std::vector<int> counts(10, 0);
	for (int draw = 0; draw < 100000; ++draw) {
		counts[static_cast<size_t>(rng.nextInt(-5, 4) + 5)] += 1;
	}
	EXPECT_LT(chiSquared(counts), kChiSquaredCritical.at(9));
}

TEST(RngDistributionTests, SpreadsPickEvenlyOverTheItems) {
	Rng						 rng(kSeed);
	const std::array<int, 5> items{0, 1, 2, 3, 4};
	std::vector<int>		 counts(items.size(), 0);
	for (int draw = 0; draw < 50000; ++draw) {
		counts[static_cast<size_t>(rng.pick(items))] += 1;
	}
	EXPECT_LT(chiSquared(counts), kChiSquaredCritical.at(4));
}

TEST(RngDistributionTests, ChoosesWeightedEntriesInProportionToTheirWeights) {
	Rng								rng(kSeed);
	std::vector<WeightedEntry<int>> entries;
	for (int index = 0; index < 4; ++index) {
		entries.push_back({.value = index, .weight = static_cast<double>(index + 1)});
	}
	std::vector<int> counts(4, 0);
	constexpr int	 draws = 100000;
	for (int draw = 0; draw < draws; ++draw) {
		counts[static_cast<size_t>(rng.weighted(entries))] += 1;
	}
	EXPECT_LT(chiSquared(counts, {0.1 * draws, 0.2 * draws, 0.3 * draws, 0.4 * draws}), kChiSquaredCritical.at(3));
}

TEST(RngDistributionTests, ShufflesIntoEveryOrderAboutEquallyOften) {
	Rng						   rng(kSeed);
	std::map<std::string, int> counts;
	for (int shuffle = 0; shuffle < 60000; ++shuffle) {
		std::string order = "abc";
		rng.shuffle(order);
		counts[order] += 1;
	}
	EXPECT_EQ(counts.size(), 6U);
	std::vector<int> values;
	for (const auto& [order, count] : counts) {
		values.push_back(count);
	}
	EXPECT_LT(chiSquared(values), kChiSquaredCritical.at(5));
}

TEST(RngDistributionTests, SpreadsTheFirstDrawsOfNeighbouringSeedsNamesAndAttemptsWithNoLinkBetweenNeighbours) {
	constexpr uint32_t				   count = 10000;
	const Rng						   root(kSeed);
	std::array<std::vector<double>, 3> firstDraws;
	for (uint32_t index = 0; index < count; ++index) {
		firstDraws[0].push_back(Rng(index).nextDouble());
		firstDraws[1].push_back(root.fork("stop:" + std::to_string(index)).nextDouble());
		firstDraws[2].push_back(root.fork("growth", index).nextDouble());
	}
	for (const std::vector<double>& values : firstDraws) {
		EXPECT_LT(chiSquared(histogram(values, 10)), kChiSquaredCritical.at(9));
		// Five standard deviations of a sample correlation over 10,000 pairs.
		const std::vector<double> earlier(values.begin(), values.end() - 1);
		const std::vector<double> later(values.begin() + 1, values.end());
		EXPECT_LT(std::abs(correlation(earlier, later)), 0.05);
	}
}

TEST(RngDistributionTests, KeepsAForkUncorrelatedWithItsParentAndItsSiblings) {
	const Rng				  root(kSeed);
	const std::vector<double> parent = doubles(Rng(kSeed), 10000);
	const std::vector<double> terrain = doubles(root.fork("terrain"), 10000);
	const std::vector<double> growth = doubles(root.fork("growth"), 10000);
	const std::vector<double> retry = doubles(root.fork("terrain", 1), 10000);
	EXPECT_LT(std::abs(correlation(parent, terrain)), 0.05);
	EXPECT_LT(std::abs(correlation(terrain, growth)), 0.05);
	EXPECT_LT(std::abs(correlation(terrain, retry)), 0.05);
}

// ============================================================================
// Rejected arguments
// ============================================================================

// Rng.ts throws a RangeError for each of these. Rng asserts, so they die in a build with asserts
// and can't be checked without them.
class RngDeathTest : public testing::Test {
  protected:
	void SetUp() override {
#ifdef NDEBUG
		GTEST_SKIP() << "asserts are compiled out";
#endif
	}
};

TEST_F(RngDeathTest, ForkRejectsAnAttemptOutsideUInt32) {
	const Rng root(kSeed);
	EXPECT_DEATH((void)root.fork("terrain", -1), "attempt must be an integer");
	EXPECT_DEATH((void)root.fork("terrain", kTwo32), "attempt must be an integer");
}

TEST_F(RngDeathTest, ForkRejectsANameThatIsNotValidUtf8) {
	const Rng root(kSeed);
	// U+D800 and U+DC00 encoded as if they were code points, a lone continuation byte, overlong
	// forms of '/', a sequence cut short, a code point past U+10FFFF, and a byte UTF-8 never uses.
	for (const std::string_view name : {
			 std::string_view("\xED\xA0\x80"),
			 std::string_view("\xED\xB0\x80"),
			 std::string_view("stop:\x80"),
			 std::string_view("\xC0\xAF"),
			 std::string_view("\xE0\x80\xAF"),
			 std::string_view("\xE4\xB8"),
			 std::string_view("\xF4\x90\x80\x80"),
			 std::string_view("\xFF"),
		 }) {
		EXPECT_DEATH((void)root.fork(name), "not valid UTF-8");
	}
}

TEST_F(RngDeathTest, NextIntRejectsBadBounds) {
	Rng rng(kSeed);
	EXPECT_DEATH(rng.nextInt(3, 2), "Rng::nextInt");
	EXPECT_DEATH(rng.nextInt(kTwo53, kTwo53 + 2), "Rng::nextInt");		// unsafe bounds with a small span
	EXPECT_DEATH(rng.nextInt(-kTwo53, -kTwo53 + 2), "Rng::nextInt");	// an unsafe min
	EXPECT_DEATH(rng.nextInt(kMaxSafeInteger, kTwo53), "Rng::nextInt"); // an unsafe max
	EXPECT_DEATH(rng.nextInt(int64_t{0}, kTwo32), "spans more than");	// a span over 2^32
}

TEST_F(RngDeathTest, PickRejectsAnEmptyRange) {
	Rng									rng(kSeed);
	const std::vector<std::string_view> none;
	EXPECT_DEATH(rng.pick(none), "Rng::pick");
}

TEST_F(RngDeathTest, WeightedRejectsBadWeights) {
	using Entries = std::vector<WeightedEntry<std::string_view>>;
	Rng			 rng(kSeed);
	const double infinity = std::numeric_limits<double>::infinity();
	EXPECT_DEATH(rng.weighted(Entries{}), "Rng::weighted");
	EXPECT_DEATH(rng.weighted(Entries{{.value = "a", .weight = 0}, {.value = "b", .weight = 0}}), "Rng::weighted");
	EXPECT_DEATH(rng.weighted(Entries{{.value = "a", .weight = 2}, {.value = "b", .weight = -1}}), "Rng::weighted");
	EXPECT_DEATH(rng.weighted(Entries{{.value = "a", .weight = std::nan("")}}), "Rng::weighted");
	EXPECT_DEATH(rng.weighted(Entries{{.value = "a", .weight = infinity}}), "Rng::weighted");
	EXPECT_DEATH(rng.weighted(Entries{{.value = "a", .weight = 1e308}, {.value = "b", .weight = 1e308}}), "Rng::weighted");
}

// ============================================================================
// freshSeed
// ============================================================================

TEST(FreshSeedTests, MintsADifferentSeedEachCall) {
	// Rng.test.ts pins how freshSeed scales Math.random; here the source is std::random_device,
	// and eight equal seeds in a row would mean it isn't random at all.
	std::set<uint32_t> seeds;
	for (int call = 0; call < 8; ++call) {
		seeds.insert(freshSeed());
	}
	EXPECT_GT(seeds.size(), 1U);
}
