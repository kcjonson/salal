#include "metrics/GPUTimer.h"

#include "backend/Backend.h"

#include <array>

namespace Renderer {

	struct GPUTimer::Spans {
		struct Span {
			QueryHandle start;
			QueryHandle stop;
			bool		pending = false;
		};

		std::array<Span, kFrames> spans;

		explicit Spans(backend::Backend& gpu) {
			for (Span& span : spans) {
				span.start = gpu.createTimestampQuery();
				span.stop = gpu.createTimestampQuery();
			}
		}

		~Spans() {
			if (backend::Backend* gpu = backend::active(); gpu != nullptr) {
				for (const Span& span : spans) {
					gpu->destroyTimestampQuery(span.start);
					gpu->destroyTimestampQuery(span.stop);
				}
			}
		}

		Spans(const Spans&) = delete;
		Spans& operator=(const Spans&) = delete;
		Spans(Spans&&) = delete;
		Spans& operator=(Spans&&) = delete;
	};

	GPUTimer::GPUTimer() = default;
	GPUTimer::~GPUTimer() = default;
	GPUTimer::GPUTimer(GPUTimer&&) noexcept = default;
	GPUTimer& GPUTimer::operator=(GPUTimer&&) noexcept = default;

	void GPUTimer::begin() {
		backend::Backend* gpu = backend::active();
		if (open || gpu == nullptr) {
			return;
		}
		if (!spans) {
			spans = std::make_unique<Spans>(*gpu);
		}

		// The span about to be reused was issued kFrames spans ago. A result still
		// pending after that long is dropped rather than waited for.
		Spans::Span& span = spans->spans[current];
		if (span.pending && gpu->timestampReady(span.stop)) {
			const uint64_t startNs = gpu->timestampNs(span.start);
			const uint64_t stopNs = gpu->timestampNs(span.stop);
			lastTimeMs = static_cast<float>(static_cast<double>(stopNs - startNs) / 1.0e6);
		}
		span.pending = false;
		gpu->writeTimestamp(span.start);
		open = true;
	}

	void GPUTimer::end() {
		backend::Backend* gpu = backend::active();
		if (!open || !spans || gpu == nullptr) { // a moved-from timer keeps `open` but not its spans
			return;
		}
		Spans::Span& span = spans->spans[current];
		gpu->writeTimestamp(span.stop);
		span.pending = true;
		open = false;
		current = (current + 1) % kFrames;
	}

} // namespace Renderer
