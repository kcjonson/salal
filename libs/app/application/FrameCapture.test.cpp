#include "application/FrameCapture.h"

#include <gtest/gtest.h>

namespace app {

	TEST(FrameCaptureTest, PercentilesAreNearestRankAndGpuTimesAreTheirOwn) {
		FrameCapture capture;
		EXPECT_FALSE(capture.active());
		capture.start();
		EXPECT_TRUE(capture.active());
		for (int i = 100; i >= 1; --i) { // out of order: the summary sorts
			capture.add({.frameMs = static_cast<float>(i), .drawCalls = 2, .uploadBytes = 1000});
		}
		capture.addGpu(0.5F);
		capture.addGpu(1.5F);

		const CaptureSummary summary = capture.stop();
		EXPECT_FALSE(capture.active());
		EXPECT_EQ(summary.frames, 100U);
		EXPECT_NEAR(summary.seconds, 5.05, 1e-9); // 1 + 2 + ... + 100 ms
		EXPECT_DOUBLE_EQ(summary.frameMs.p50, 50.0);
		EXPECT_DOUBLE_EQ(summary.frameMs.p99, 99.0);
		EXPECT_DOUBLE_EQ(summary.frameMs.mean, 50.5);
		EXPECT_DOUBLE_EQ(summary.frameMs.max, 100.0);
		EXPECT_DOUBLE_EQ(summary.drawCalls.p99, 2.0);
		EXPECT_DOUBLE_EQ(summary.uploadBytes.mean, 1000.0);
		EXPECT_EQ(summary.gpuFrames, 2U);
		EXPECT_DOUBLE_EQ(summary.gpuMs.p50, 0.5);
		EXPECT_DOUBLE_EQ(summary.gpuMs.max, 1.5);

		capture.start(); // starts over, empty
		const CaptureSummary empty = capture.stop();
		EXPECT_EQ(empty.frames, 0U);
		EXPECT_EQ(empty.frameMs.p99, 0.0);
	}

} // namespace app
