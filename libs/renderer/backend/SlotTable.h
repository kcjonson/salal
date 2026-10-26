#pragma once

// A backend's objects of one kind, keyed by a Handle: a 16-bit slot index and a
// 16-bit generation that advances when the slot is freed, so a stale handle finds
// nothing. Freed slots are reused. GL-free; each backend keeps one per kind.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace Renderer::backend {

	template <typename HandleType, typename T>
	class SlotTable {
	  public:
		// Store `value`; an invalid handle when the table is full.
		HandleType insert(T value) {
			uint16_t index = 0;
			if (!freeSlots.empty()) {
				index = freeSlots.back();
				freeSlots.pop_back();
			} else {
				if (slots.size() >= kMaxSlots) {
					return {};
				}
				index = static_cast<uint16_t>(slots.size());
				slots.emplace_back();
			}
			Slot& slot = slots[index];
			slot.value.emplace(std::move(value));
			++liveCount;
			return HandleType::make(index, slot.generation);
		}

		// Null for an invalid or stale handle. The pointer lasts until the next insert.
		T*		 find(HandleType handle) { return holds(handle) ? &*slots[handle.index()].value : nullptr; }
		const T* find(HandleType handle) const { return holds(handle) ? &*slots[handle.index()].value : nullptr; }

		// Destroy the value and free its slot; false for an invalid or stale handle.
		bool erase(HandleType handle) {
			if (!holds(handle)) {
				return false;
			}
			Slot& slot = slots[handle.index()];
			slot.value.reset();
			slot.generation = static_cast<uint16_t>(slot.generation + 1); // wraps after 65536 reuses of one slot
			freeSlots.push_back(handle.index());
			--liveCount;
			return true;
		}

		[[nodiscard]] size_t size() const { return liveCount; }

	  private:
		// Indices 0..0xFFFE: index 0xFFFF with generation 0xFFFF would spell the invalid handle.
		static constexpr size_t kMaxSlots = 0xFFFF;

		struct Slot {
			std::optional<T> value;
			uint16_t		 generation = 0;
		};

		[[nodiscard]] bool holds(HandleType handle) const {
			if (!handle.isValid() || handle.index() >= slots.size()) {
				return false;
			}
			const Slot& slot = slots[handle.index()];
			return slot.value.has_value() && slot.generation == handle.generation();
		}

		std::vector<Slot>	  slots;
		std::vector<uint16_t> freeSlots;
		size_t				  liveCount = 0;
	};

} // namespace Renderer::backend
