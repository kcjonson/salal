#include "Rng.h"
#include <benchmark/benchmark.h>

using namespace foundation;

static void BM_RngNextUInt32(benchmark::State& state) {
	Rng rng(42);
	for (auto _ : state) {
		benchmark::DoNotOptimize(rng.nextUInt32());
	}
	state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_RngNextUInt32);

static void BM_RngFork(benchmark::State& state) {
	const Rng root(42);
	uint32_t  attempt = 0;
	for (auto _ : state) {
		benchmark::DoNotOptimize(root.fork("stop:123", attempt++).seed());
	}
	state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_RngFork);
