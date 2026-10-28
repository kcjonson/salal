#pragma once

// The seam the frame reads time through. Nothing else in app reads a clock: the platform supplies
// one, and a test drives the frame with a ManualClock it advances.

namespace app {

	class Clock {
	  public:
		virtual ~Clock() = default;

		// Seconds, monotonic.
		virtual double now() const = 0;
	};

	// Time that moves only when advanced, from 0.
	class ManualClock final : public Clock {
	  public:
		double now() const override { return current; }
		void   advance(double seconds) { current += seconds; }

	  private:
		double current = 0.0;
	};

} // namespace app
