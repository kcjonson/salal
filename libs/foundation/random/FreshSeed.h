#pragma once

// A new root seed for Rng, from the platform's entropy source: for the places a game mints
// one, such as founding a campaign or a tool's new-seed button (Rng.ts's freshSeed). It is
// foundation/random's one unseeded read, in its own header so a sim layer can keep it out by
// never including it.

#include <cstdint>
#include <random>

namespace foundation {

	inline uint32_t freshSeed() {
		std::random_device device;
		return static_cast<uint32_t>(device());
	}

} // namespace foundation
