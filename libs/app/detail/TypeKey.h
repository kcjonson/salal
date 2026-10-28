#pragma once

// A per-type identity with no RTTI: the address of a variable each type gets its own of. Writable,
// so no linker folds two types' tags into one, as identical-constant folding could.

namespace app::detail {

	template <typename T>
	inline char typeTag = 0; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

	template <typename T>
	const void* typeKey() {
		return &typeTag<T>;
	}

} // namespace app::detail
