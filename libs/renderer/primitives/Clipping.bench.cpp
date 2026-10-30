// The clip stack's CPU cost, through Primitives on the null backend: pushing and
// popping nested clips, and a clipped list whose items past its viewport sit under
// the empty clip and are culled. The fragment shader's test is the GPU's and isn't
// measured here.

#include "backend/NullBackend.h"
#include "graphics/Rect.h"
#include "primitives/Primitives.h"
#include <benchmark/benchmark.h>
#include <cstdint>
#include <memory>
#include <utility>

namespace {

	using namespace Renderer;

	// Primitives on a null backend for one benchmark's run.
	class NullRenderer {
	  public:
		NullRenderer() {
			auto owned = std::make_unique<backend::NullBackend>();
			gpu = owned.get();
			Primitives::init(std::move(owned), {});
			Primitives::setViewport(800, 600);
		}
		~NullRenderer() { Primitives::shutdown(); }

		NullRenderer(const NullRenderer&) = delete;
		NullRenderer& operator=(const NullRenderer&) = delete;
		NullRenderer(NullRenderer&&) = delete;
		NullRenderer& operator=(NullRenderer&&) = delete;

		// The backend keeps a record of every draw; dropped each iteration so it doesn't grow.
		void forget() { gpu->clearRecord(); }

	  private:
		backend::NullBackend* gpu = nullptr;
	};

} // namespace

// Nested clips, a UI hierarchy's window, panel, card and content.
static void BM_NestedClips(benchmark::State& state) {
	const int		   depth = static_cast<int>(state.range(0));
	const NullRenderer renderer;
	for (auto _ : state) {
		for (int i = 0; i < depth; ++i) {
			const float margin = static_cast<float>(i * 20);
			Primitives::pushClip(Foundation::Rect{margin, margin, 800.0F - (2.0F * margin), 600.0F - (2.0F * margin)});
		}
		for (int i = 0; i < depth; ++i) {
			Primitives::popClip();
		}
	}
	state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(depth) * 2);
}
BENCHMARK(BM_NestedClips)->Arg(1)->Arg(4)->Arg(16);

// A list 300 high with an item every 50: each item clipped inside the list's clip
// and drawn, so past the sixth the item's clip is empty and its rect culled.
static void BM_ClippedListItems(benchmark::State& state) {
	const int	 items = static_cast<int>(state.range(0));
	NullRenderer renderer;
	for (auto _ : state) {
		Primitives::beginFrame();
		Primitives::pushClip(Foundation::Rect{0.0F, 0.0F, 400.0F, 300.0F});
		for (int i = 0; i < items; ++i) {
			const Foundation::Rect item{0.0F, static_cast<float>(i * 50), 400.0F, 50.0F};
			Primitives::pushClip(item);
			Primitives::drawRect({.bounds = item});
			Primitives::popClip();
		}
		Primitives::popClip();
		Primitives::endFrame();
		renderer.forget();
	}
	state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(items));
}
BENCHMARK(BM_ClippedListItems)->Arg(10)->Arg(100)->Arg(200);
