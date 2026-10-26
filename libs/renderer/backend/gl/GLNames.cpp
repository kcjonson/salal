#include "backend/gl/GLNames.h"

#include <cassert>
#include <vector>

namespace Renderer::backend::gl {

	namespace {
		struct Slot {
			GLuint	 name = 0;
			uint16_t generation = 0;
			bool	 live = false;
		};

		std::vector<Slot>	  g_slots;
		std::vector<uint16_t> g_freeSlots;

		Slot* slotFor(TextureHandle handle) {
			if (!handle.isValid() || handle.index() >= g_slots.size()) {
				return nullptr;
			}
			Slot& slot = g_slots[handle.index()];
			return slot.live && slot.generation == handle.generation() ? &slot : nullptr;
		}
	} // namespace

	TextureHandle registerTexture(GLuint name) {
		uint16_t index = 0;
		if (!g_freeSlots.empty()) {
			index = g_freeSlots.back();
			g_freeSlots.pop_back();
		} else {
			assert(g_slots.size() < 0xFFFFU && "texture registry out of slots");
			index = static_cast<uint16_t>(g_slots.size());
			g_slots.emplace_back();
		}
		Slot& slot = g_slots[index];
		slot.name = name;
		slot.live = true;
		return TextureHandle::make(index, slot.generation);
	}

	void unregisterTexture(TextureHandle handle) {
		Slot* slot = slotFor(handle);
		if (slot == nullptr) {
			return;
		}
		slot->name = 0;
		slot->live = false;
		// Generation 0xFFFF at index 0xFFFF would spell the invalid handle; skip it.
		slot->generation = static_cast<uint16_t>(slot->generation == 0xFFFEU ? 0 : slot->generation + 1);
		g_freeSlots.push_back(handle.index());
	}

	GLuint name(TextureHandle handle) {
		const Slot* slot = slotFor(handle);
		return slot != nullptr ? slot->name : 0;
	}

} // namespace Renderer::backend::gl
