// GPUTimer on the null backend, headless: what it asks of the backend.

#include "metrics/GPUTimer.h"

#include "backend/NullBackend.h"
#include "primitives/Primitives.h"

#include <gtest/gtest.h>

#include <memory>

namespace Renderer {

	TEST(GPUTimerTest, SpansWriteTimestampPairsAndTheQueriesGoWithTheTimer) {
		auto				  owned = std::make_unique<backend::NullBackend>();
		backend::NullBackend& gpu = *owned;
		Primitives::init(std::move(owned));
		const size_t before = gpu.live().queries;
		{
			GPUTimer timer;
			EXPECT_EQ(gpu.live().queries, before); // created on first use
			for (int frame = 0; frame < 6; ++frame) {
				timer.begin();
				timer.begin(); // a second begin inside a span is ignored
				timer.end();
				timer.end();
			}
			EXPECT_EQ(gpu.live().queries, before + 8); // four spans, a pair each
			EXPECT_EQ(gpu.timestampsWritten(), 12);
			EXPECT_EQ(timer.getTimeMs(), 0.0F); // every null timestamp is 0
		}
		EXPECT_EQ(gpu.live().queries, before);
		Primitives::shutdown();
	}

	TEST(GPUTimerTest, WithNoBackendItDoesNothing) {
		GPUTimer timer;
		timer.begin();
		timer.end();
		EXPECT_EQ(timer.getTimeMs(), 0.0F);
	}

} // namespace Renderer
