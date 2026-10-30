#pragma once

// GPUFrameTimer - the GPU time of a frame's submissions: each span (a clear, a flush, an instanced
// draw, a baked pass) bracketed by a pair of the backend's timestamp queries, the spans summed per frame.
// Only the GPU's work inside the spans counts, not the gaps between them where it waits for the
// CPU, so a frame the batcher holds up reads what the GPU did rather than how long it waited, which
// a single span around the frame (GPUTimer) would. Spans nest: one begun inside another is part of
// it, and only the outermost pair is written. A frame's spans are read kFrames frames later, long
// after the GPU finished them, so reading never stalls the CPU; a frame not finished by then is
// dropped, and counted.

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

		/// End the frame begun last, if any, closing a span left open, and begin one, collecting the
		/// frame begun kFrames frames ago.
		void beginFrame();

		/// Bracket GPU work. A span outside a frame is ignored; one inside another is part of it.
		void beginSpan();
		void endSpan();

		/// Forget every frame not yet collected; the next beginFrame() starts afresh.
		void reset();

		/// GPU milliseconds of the latest frame collected, 0 until one is.
		[[nodiscard]] float lastFrameMs() const { return lastMs; }

		/// Frames collected so far; a new result has arrived when it moves.
		[[nodiscard]] uint64_t framesTimed() const { return timed; }

		/// Frames whose spans hadn't all finished kFrames frames later, so were never collected.
		[[nodiscard]] uint64_t framesDropped() const { return dropped; }

		/// False once the backend made no query: it has no timestamps (as some GL drivers lack them),
		/// and nothing is timed from then on.
		[[nodiscard]] bool supported() const { return !unsupported; }

	  private:
		static constexpr int kFrames = 4;

		// The query pairs of each frame in flight (backend handles, defined in GPUFrameTimer.cpp).
		struct Frames;
		std::unique_ptr<Frames> frames;

		void closeSpan();

		backend::Backend& gpu;
		int				  current = 0;
		int				  depth = 0;
		bool			  frameOpen = false;
		bool			  unsupported = false;
		float			  lastMs = 0.0F;
		uint64_t		  timed = 0;
		uint64_t		  dropped = 0;
	};

} // namespace Renderer
