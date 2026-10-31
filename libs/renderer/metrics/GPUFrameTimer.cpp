#include "metrics/GPUFrameTimer.h"

#include "backend/Backend.h"

#include <array>
#include <cstddef>
#include <vector>

namespace Renderer {

	struct GPUFrameTimer::Frames {
		struct Span {
			QueryHandle start;
			QueryHandle stop;
		};

		struct Frame {
			std::vector<Span> spans; // grown as a frame needs more, kept for the frames after
			size_t			  used = 0;
			bool			  pending = false;
		};

		std::array<Frame, kFrames> slots;
	};

	GPUFrameTimer::GPUFrameTimer(backend::Backend& backend)
		: frames(std::make_unique<Frames>()),
		  gpu(backend) {}

	GPUFrameTimer::~GPUFrameTimer() {
		for (const Frames::Frame& frame : frames->slots) {
			for (const Frames::Span& span : frame.spans) {
				gpu.destroyTimestampQuery(span.start);
				gpu.destroyTimestampQuery(span.stop);
			}
		}
	}

	void GPUFrameTimer::beginFrame() {
		if (frameOpen) {
			if (depth > 0) {
				closeSpan();
			}
			frames->slots[current].pending = true;
			current = (current + 1) % kFrames;
		}

		Frames::Frame& frame = frames->slots[current];
		if (frame.pending) {
			bool ready = true;
			for (size_t i = 0; i < frame.used && ready; ++i) {
				ready = gpu.timestampReady(frame.spans[i].stop);
			}
			if (ready) {
				uint64_t totalNs = 0;
				for (size_t i = 0; i < frame.used; ++i) {
					totalNs += gpu.timestampNs(frame.spans[i].stop) - gpu.timestampNs(frame.spans[i].start);
				}
				lastMs = static_cast<float>(static_cast<double>(totalNs) / 1.0e6);
				++timed;
			} else {
				++dropped;
			}
		}
		frame.pending = false;
		frame.used = 0;
		frameOpen = true;
	}

	void GPUFrameTimer::beginSpan() {
		if (!frameOpen || unsupported || depth++ > 0) {
			return;
		}
		Frames::Frame& frame = frames->slots[current];
		if (frame.used == frame.spans.size()) {
			const Frames::Span span{.start = gpu.createTimestampQuery(), .stop = gpu.createTimestampQuery()};
			if (!span.start.isValid() || !span.stop.isValid()) {
				gpu.destroyTimestampQuery(span.start);
				gpu.destroyTimestampQuery(span.stop);
				unsupported = true;
				depth = 0;
				return;
			}
			frame.spans.push_back(span);
		}
		gpu.writeTimestamp(frame.spans[frame.used].start);
	}

	void GPUFrameTimer::endSpan() {
		if (depth == 0 || --depth > 0) {
			return;
		}
		closeSpan();
	}

	void GPUFrameTimer::closeSpan() {
		Frames::Frame& frame = frames->slots[current];
		gpu.writeTimestamp(frame.spans[frame.used].stop);
		++frame.used;
		depth = 0;
	}

	void GPUFrameTimer::reset() {
		for (Frames::Frame& frame : frames->slots) {
			frame.pending = false;
			frame.used = 0;
		}
		frameOpen = false;
		depth = 0;
	}

} // namespace Renderer
