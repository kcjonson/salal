#pragma once

// GPUTimer - GPU time of a span of GL commands, measured with a pair of
// GL_TIMESTAMP queries. Timestamps nest and overlap freely (only one
// GL_TIME_ELAPSED query can be active at a time), so a pass can be timed inside
// the frame's own span. Each span's result is read kFrames spans later, long
// after the GPU finished it, so reading never stalls the CPU.

#include <memory>

namespace Renderer {

	class GPUTimer {
	  public:
		GPUTimer();
		~GPUTimer();

		GPUTimer(const GPUTimer&) = delete;
		GPUTimer& operator=(const GPUTimer&) = delete;
		GPUTimer(GPUTimer&&) noexcept;
		GPUTimer& operator=(GPUTimer&&) noexcept;

		/// Start a span. Needs a current GL context; the queries are created on first use.
		void begin();

		/// End the span begun last.
		void end();

		/// GPU milliseconds of the latest span whose result has arrived, 0 until one has.
		[[nodiscard]] float getTimeMs() const { return lastTimeMs; }

	  private:
		static constexpr int kFrames = 4;

		// The query pairs (GL objects, defined in GPUTimer.cpp); null until begin().
		struct Spans;
		std::unique_ptr<Spans> spans;

		int	  current = 0;
		bool  open = false;
		float lastTimeMs = 0.0F;
	};

} // namespace Renderer
