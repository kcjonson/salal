#pragma once

// GPUFrameTimer - the GPU time of a frame's submissions: each span (a clear, a flush, an instanced
// draw, a baked pass) bracketed by a pair of the backend's timestamp queries, the spans summed per frame.
// Only the GPU's work inside the spans counts, not the gaps between them where it waits for the
// CPU, so a frame the batcher holds up reads what the GPU did rather than how long it waited, which
// a single span around the frame (GPUTimer) would. A frame's spans are read kFrames frames later,
// long after the GPU finished them, so reading never stalls the CPU; a frame not finished by then
// is dropped.

#include <cstdint>
#include <memory>

namespace Renderer { // NOLINT(readability-identifier-naming)

	namespace backend {
		class Backend;
	}

	class GPUFrameTimer {
	  public:
		/// Queries are made on `gpu` as spans first need them, and destroyed with the timer; `gpu` outlives it.
		explicit GPUFrameTimer(backend::Backend& gpu);
		~GPUFrameTimer();

		GPUFrameTimer(const GPUFrameTimer&) = delete;
		GPUFrameTimer& operator=(const GPUFrameTimer&) = delete;
		GPUFrameTimer(GPUFrameTimer&&) = delete;
		GPUFrameTimer& operator=(GPUFrameTimer&&) = delete;

		/// End the frame begun last, if any, and begin one, collecting the frame begun kFrames frames ago.
		void beginFrame();

		/// Bracket GPU work; a span outside a frame, or begun inside another, is ignored.
		void beginSpan();
		void endSpan();

		/// Forget every frame not yet collected; the next beginFrame() starts afresh.
		void reset();

		/// GPU milliseconds of the latest frame collected, 0 until one is.
		[[nodiscard]] float lastFrameMs() const { return lastMs; }

		/// Frames collected so far; a new result has arrived when it moves.
		[[nodiscard]] uint64_t framesTimed() const { return timed; }

	  private:
		static constexpr int kFrames = 4;

		// The query pairs of each frame in flight (backend handles, defined in GPUFrameTimer.cpp).
		struct Frames;
		std::unique_ptr<Frames> frames;

		backend::Backend& gpu;
		int				  current = 0;
		bool			  frameOpen = false;
		bool			  spanOpen = false;
		float			  lastMs = 0.0F;
		uint64_t		  timed = 0;
	};

} // namespace Renderer
