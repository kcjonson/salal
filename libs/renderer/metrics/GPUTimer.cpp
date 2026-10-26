#include "metrics/GPUTimer.h"

#include "backend/gl/GLQuery.h"

#include <GL/glew.h>
#include <array>

namespace Renderer {

	struct GPUTimer::Spans {
		struct Span {
			GLQuery start;
			GLQuery stop;
			bool	pending = false;
		};

		std::array<Span, kFrames> spans;

		Spans() {
			for (Span& span : spans) {
				span.start = GLQuery::create();
				span.stop = GLQuery::create();
			}
		}
	};

	GPUTimer::GPUTimer() = default;
	GPUTimer::~GPUTimer() = default;
	GPUTimer::GPUTimer(GPUTimer&&) noexcept = default;
	GPUTimer& GPUTimer::operator=(GPUTimer&&) noexcept = default;

	void GPUTimer::begin() {
		if (open) {
			return;
		}
		if (!spans) {
			spans = std::make_unique<Spans>();
		}

		// The span about to be reused was issued kFrames spans ago. A result still
		// pending after that long is dropped rather than waited for.
		Spans::Span& span = spans->spans[current];
		if (span.pending && span.stop.isResultAvailable()) {
			const GLuint64 startNs = span.start.getResult();
			const GLuint64 stopNs = span.stop.getResult();
			lastTimeMs = static_cast<float>(static_cast<double>(stopNs - startNs) / 1.0e6);
		}
		span.pending = false;
		span.start.recordTimestamp();
		open = true;
	}

	void GPUTimer::end() {
		if (!open || !spans) { // a moved-from timer keeps `open` but not its spans
			return;
		}
		Spans::Span& span = spans->spans[current];
		span.stop.recordTimestamp();
		span.pending = true;
		open = false;
		current = (current + 1) % kFrames;
	}

} // namespace Renderer
