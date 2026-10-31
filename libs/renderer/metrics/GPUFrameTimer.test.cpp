// GPUFrameTimer on the null backend, headless: when it collects a frame, what it asks of the backend,
// and what it does when the backend's timestamps lag or don't exist.

#include "metrics/GPUFrameTimer.h"

#include "backend/NullBackend.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <map>

namespace Renderer {

	namespace {
		// Timestamps on a clock the test moves, ready only once `ready` says so.
		class ClockBackend final : public backend::NullBackend {
		  public:
			uint64_t now = 0;
			bool	 ready = true;

			void writeTimestamp(QueryHandle query) override {
				NullBackend::writeTimestamp(query);
				at[query.value] = now;
			}
			bool	 timestampReady(QueryHandle query) override { return ready && NullBackend::timestampReady(query); }
			uint64_t timestampNs(QueryHandle query) override { return at[query.value]; }

		  private:
			std::map<uint32_t, uint64_t> at;
		};

		// A backend with no timestamps: every query is refused.
		class NoTimestampBackend final : public backend::NullBackend {
		  public:
			QueryHandle createTimestampQuery() override { return {}; }
		};
	} // namespace

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
			EXPECT_EQ(timer.framesDropped(), 0U);
		}
		EXPECT_EQ(gpu.live().queries, 0U);
	}

	TEST(GPUFrameTimerTest, ANestedSpanIsPartOfTheOneAroundIt) {
		ClockBackend  gpu;
		GPUFrameTimer timer(gpu);
		for (int frame = 0; frame < 5; ++frame) {
			timer.beginFrame();
			gpu.now += 1'000'000; // idle, outside any span
			timer.beginSpan();	  // a baked pass
			gpu.now += 1'000'000;
			timer.beginSpan(); // a flush inside it
			gpu.now += 1'000'000;
			timer.endSpan();
			gpu.now += 3'000'000;
			timer.endSpan();
			timer.beginSpan(); // a span left open closes at the next frame
			gpu.now += 500'000;
		}
		EXPECT_EQ(timer.framesTimed(), 1U);
		EXPECT_FLOAT_EQ(timer.lastFrameMs(), 5.5F);
	}

	TEST(GPUFrameTimerTest, AFrameNotBackInTimeIsDroppedAndResetDropsTheFramesInFlight) {
		ClockBackend  gpu;
		GPUFrameTimer timer(gpu);
		gpu.ready = false;
		for (int frame = 0; frame < 6; ++frame) {
			timer.beginFrame();
			timer.beginSpan();
			timer.endSpan();
		}
		EXPECT_EQ(timer.framesTimed(), 0U);
		EXPECT_EQ(timer.framesDropped(), 2U);

		gpu.ready = true;
		timer.reset();
		for (int frame = 0; frame < 4; ++frame) {
			timer.beginFrame();
		}
		EXPECT_EQ(timer.framesTimed(), 0U); // those before the reset are gone, the first after still in flight
		timer.beginFrame();
		EXPECT_EQ(timer.framesTimed(), 1U);
		EXPECT_EQ(timer.framesDropped(), 2U);
	}

	TEST(GPUFrameTimerTest, WithNoTimestampsItTimesNothing) {
		NoTimestampBackend gpu;
		GPUFrameTimer	   timer(gpu);
		for (int frame = 0; frame < 6; ++frame) {
			timer.beginFrame();
			timer.beginSpan();
			timer.endSpan();
		}
		EXPECT_FALSE(timer.supported());
		EXPECT_EQ(gpu.timestampsWritten(), 0);
		EXPECT_EQ(timer.framesDropped(), 0U);
	}

} // namespace Renderer
