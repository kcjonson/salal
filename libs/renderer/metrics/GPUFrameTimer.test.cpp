// GPUFrameTimer on the null backend, headless: when it collects a frame, and what it asks of the backend.

#include "metrics/GPUFrameTimer.h"

#include "backend/NullBackend.h"

#include <gtest/gtest.h>

namespace Renderer {

	TEST(GPUFrameTimerTest, AFrameIsCollectedFourFramesLaterWithItsSpansQueriesReused) {
		backend::NullBackend gpu;
		{
			GPUFrameTimer timer(gpu);
			timer.beginSpan(); // outside a frame: ignored
			timer.endSpan();
			EXPECT_EQ(gpu.live().queries, 0U);

			for (int frame = 0; frame < 4; ++frame) {
				timer.beginFrame();
				timer.beginSpan();
				timer.beginSpan(); // inside a span: ignored
				timer.endSpan();
				timer.beginSpan();
				timer.endSpan();
			}
			EXPECT_EQ(timer.framesTimed(), 0U);
			EXPECT_EQ(gpu.live().queries, 16U); // four frames in flight, two spans each, a pair a span

			timer.beginFrame(); // collects the first frame
			EXPECT_EQ(timer.framesTimed(), 1U);
			EXPECT_EQ(timer.lastFrameMs(), 0.0F); // every null timestamp is 0
			timer.beginSpan();
			timer.endSpan();
			timer.beginFrame();
			EXPECT_EQ(timer.framesTimed(), 2U);
			EXPECT_EQ(gpu.live().queries, 16U);
			EXPECT_EQ(gpu.timestampsWritten(), 18);
		}
		EXPECT_EQ(gpu.live().queries, 0U);
	}

	TEST(GPUFrameTimerTest, ResetDropsTheFramesInFlight) {
		backend::NullBackend gpu;
		GPUFrameTimer		 timer(gpu);
		for (int frame = 0; frame < 3; ++frame) {
			timer.beginFrame();
			timer.beginSpan();
			timer.endSpan();
		}
		timer.reset();
		for (int frame = 0; frame < 4; ++frame) {
			timer.beginFrame();
		}
		EXPECT_EQ(timer.framesTimed(), 0U); // the three before the reset are gone, the first after is still in flight
		timer.beginFrame();
		EXPECT_EQ(timer.framesTimed(), 1U);
	}

} // namespace Renderer
