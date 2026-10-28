#pragma once

// The dev server's log ring. Every thread that logs writes to it (the logger's sink runs on whichever
// thread logged), so writes and reads take a lock, held for one copy. Full, it overwrites the oldest
// entry. Each reader keeps its own absolute position and reads forward with writeCursor() and
// peekAt(), so several log streams each get every entry without starving one another.

#include <array>
#include <cstddef>
#include <mutex>

namespace app::detail {

	template <typename T, std::size_t N>
	class RingBuffer {
	  public:
		static constexpr std::size_t capacity() { return N; }

		void write(const T& item) {
			const std::lock_guard lock(mutex);
			buffer[written % N] = item;
			++written;
		}

		// How many items were ever written: the absolute position the next write takes.
		std::size_t writeCursor() const {
			const std::lock_guard lock(mutex);
			return written;
		}

		// The item at absolute position `index`; false when it isn't written yet or has been overwritten.
		bool peekAt(std::size_t index, T& item) const {
			const std::lock_guard lock(mutex);
			if (index >= written || written - index > N) {
				return false;
			}
			item = buffer[index % N];
			return true;
		}

	  private:
		mutable std::mutex mutex;
		std::array<T, N>   buffer{};
		std::size_t		   written = 0;
	};

} // namespace app::detail
