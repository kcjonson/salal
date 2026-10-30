// Draw-order queue performance benchmarks.
//
// Measures the CPU cost that the per-draw-call group queue + key sort adds to the
// batch flush, in isolation (no GL; the actual glDrawElements work is unchanged
// by reordering). Three cases at 10k / 20k draw calls:
//   - Baseline: current behaviour (build indices, upload as-is, no groups/sort).
//   - FastPath: new common case (build one group record per draw; every key 0,
//     so no sort and indices are used as-is).
//   - Sorted:   new worst-ish case (groups built; ~2% carry a non-zero key, so the
//     groups are stable-sorted and the emit-order index list is rebuilt).
// The Baseline-vs-FastPath gap is the always-on cost; Baseline-vs-Sorted is the
// cost only paid on frames where a popup (a non-zero key) is open.

#include <algorithm>
#include <cstdint>
#include <vector>

#include <benchmark/benchmark.h>

namespace {

	struct Group {
		std::uint32_t indexStart;
		std::uint32_t indexCount;
		std::uint64_t order; // the domain, then the key with its sign bit flipped, as BatchRenderer packs it
	};

	inline void pushQuad(std::vector<std::uint32_t>& indices, std::uint32_t base) {
		indices.push_back(base + 0);
		indices.push_back(base + 1);
		indices.push_back(base + 2);
		indices.push_back(base + 0);
		indices.push_back(base + 2);
		indices.push_back(base + 3);
	}

} // namespace

// Current behaviour: indices accumulated in submission order, uploaded as-is.
static void BM_ZQueue_Baseline(benchmark::State& state) {
	const int				   n = static_cast<int>(state.range(0));
	std::vector<std::uint32_t> indices;
	indices.reserve(static_cast<size_t>(n) * 6);

	for (auto _ : state) {
		indices.clear();
		std::uint32_t base = 0;
		for (int i = 0; i < n; ++i) {
			pushQuad(indices, base);
			base += 4;
		}
		benchmark::DoNotOptimize(indices.data());
		benchmark::ClobberMemory();
	}
	state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(n));
}
BENCHMARK(BM_ZQueue_Baseline)->Arg(10000)->Arg(20000);

// New, common case: one POD group per draw; every key 0 -> fast path, no sort,
// indices used as-is. Delta vs Baseline = the group records.
static void BM_ZQueue_FastPath(benchmark::State& state) {
	const int				   n = static_cast<int>(state.range(0));
	std::vector<std::uint32_t> indices;
	std::vector<Group>		   groups;
	indices.reserve(static_cast<size_t>(n) * 6);
	groups.reserve(static_cast<size_t>(n));

	for (auto _ : state) {
		indices.clear();
		groups.clear();
		bool		  anyKey = false;
		std::uint32_t base = 0;
		for (int i = 0; i < n; ++i) {
			const auto start = static_cast<std::uint32_t>(indices.size());
			pushQuad(indices, base);
			groups.push_back({start, 6, 0x80000000U});
			base += 4;
		}
		// Flush fast path: every key 0 -> upload indices unchanged.
		if (anyKey) {
			std::stable_sort(groups.begin(), groups.end(), [](const Group& a, const Group& b) { return a.order < b.order; });
		}
		benchmark::DoNotOptimize(indices.data());
		benchmark::DoNotOptimize(groups.data());
		benchmark::ClobberMemory();
	}
	state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(n));
}
BENCHMARK(BM_ZQueue_FastPath)->Arg(10000)->Arg(20000);

// New, popup-open case: ~2% of draws carry a non-zero key, so groups are
// stable-sorted and the emit-order index list is rebuilt.
static void BM_ZQueue_Sorted(benchmark::State& state) {
	const int				   n = static_cast<int>(state.range(0));
	std::vector<std::uint32_t> indices;
	std::vector<Group>		   groups;
	std::vector<std::uint32_t> emit;
	indices.reserve(static_cast<size_t>(n) * 6);
	groups.reserve(static_cast<size_t>(n));
	emit.reserve(static_cast<size_t>(n) * 6);

	for (auto _ : state) {
		indices.clear();
		groups.clear();
		emit.clear();
		std::uint32_t base = 0;
		for (int i = 0; i < n; ++i) {
			const auto start = static_cast<std::uint32_t>(indices.size());
			pushQuad(indices, base);
			const std::uint64_t key = (i % 50 == 0) ? 1000U : 0U; // ~2% non-zero
			groups.push_back({start, 6, key ^ 0x80000000U});
			base += 4;
		}
		std::stable_sort(groups.begin(), groups.end(), [](const Group& a, const Group& b) { return a.order < b.order; });
		for (const Group& g : groups) {
			for (std::uint32_t k = 0; k < g.indexCount; ++k) {
				emit.push_back(indices[g.indexStart + k]);
			}
		}
		benchmark::DoNotOptimize(emit.data());
		benchmark::ClobberMemory();
	}
	state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(n));
}
BENCHMARK(BM_ZQueue_Sorted)->Arg(10000)->Arg(20000);
