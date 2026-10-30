#pragma once

// A perf capture: each frame's times and the batch's counts from start to stop, and the GPU times
// that came back meanwhile, summed into distributions at stop. The App records a frame that began
// while capturing once it closes, at the next frame's start, so a capture holds whole frames only.
// Room for kMaxFrames is reserved at start, so recording never allocates; frames past it are
// counted as dropped, not kept. docs/testing/README.md has the runner that reads it.

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace app {

	struct FrameSample {
		float	 frameMs = 0.0F;   // start to start: the frame's work, present and pacing
		float	 cpuMs = 0.0F;	   // poll, input, update and render: the frame's CPU work, without present
		float	 presentMs = 0.0F; // step 11, which waits when the GPU or the compositor is behind
		float	 renderMs = 0.0F;  // step 9: the scenes and overlays drawing, and the batch's flushes
		float	 flushMs = 0.0F;   // of that, the flushes: the plan, the uploads and the draw calls
		uint32_t drawCalls = 0;
		uint32_t vertices = 0;
		uint32_t triangles = 0;
		uint32_t instances = 0;
		uint64_t uploadBytes = 0;
	};

	// Nearest-rank percentiles, so each is a value some frame had.
	struct Distribution {
		double p50 = 0.0;
		double p99 = 0.0;
		double mean = 0.0;
		double max = 0.0;
	};

	struct CaptureSummary {
		uint64_t	 frames = 0;
		uint64_t	 droppedFrames = 0; // past kMaxFrames
		double		 seconds = 0.0;		// the frames' times summed
		Distribution frameMs, cpuMs, presentMs, renderMs, flushMs;
		uint64_t	 gpuFrames = 0; // GPU results that came back during the capture, a few frames behind
		Distribution gpuMs;
		Distribution drawCalls, vertices, triangles, instances, uploadBytes;

		nlohmann::ordered_json toJson() const;
	};

	class FrameCapture {
	  public:
		static constexpr std::size_t kMaxFrames = std::size_t{1} << 17;

		// Start again, empty.
		void start();
		bool active() const { return running; }
		void add(const FrameSample& sample);
		void addGpu(float ms);
		// The summary of what was added since start; recording stops.
		CaptureSummary stop();

	  private:
		std::vector<FrameSample> samples;
		std::vector<float>		 gpuSamples;
		uint64_t				 dropped = 0;
		bool					 running = false;
	};

} // namespace app
