#include "backend/gl/GLNames.h"

#include "utils/Log.h"

#include <vector>

namespace Renderer::backend::gl {

	namespace {
		struct Slot {
			GLuint	 name = 0;
			uint16_t generation = 0;
			bool	 live = false;
		};

		// Indices 0..0xFFFE: an index of 0xFFFF with generation 0xFFFF would spell the invalid handle.
		constexpr size_t kMaxSlots = 0xFFFF;

		// Leaked on purpose: consumers hold FontRenderer and TileAtlasBuilder in namespace-scope
		// statics whose destructors unregister, which can run after a static vector here is gone.
		std::vector<Slot>& slots() {
			static auto& s = *new std::vector<Slot>;
			return s;
		}

		std::vector<uint16_t>& freeSlots() {
			static auto& s = *new std::vector<uint16_t>;
			return s;
		}

		Slot* slotFor(TextureHandle handle) {
			if (!handle.isValid() || handle.index() >= slots().size()) {
				return nullptr;
			}
			Slot& slot = slots()[handle.index()];
			return slot.live && slot.generation == handle.generation() ? &slot : nullptr;
		}
	} // namespace

	TextureHandle registerTexture(GLuint name) {
		uint16_t index = 0;
		if (!freeSlots().empty()) {
			index = freeSlots().back();
			freeSlots().pop_back();
		} else {
			if (slots().size() >= kMaxSlots) {
				LOG_ERROR(Renderer, "Texture registry full (%zu live textures); handle not issued", slots().size());
				return {};
			}
			index = static_cast<uint16_t>(slots().size());
			slots().emplace_back();
		}
		Slot& slot = slots()[index];
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
		slot->generation = static_cast<uint16_t>(slot->generation + 1); // wraps at 65536 reuses of one slot
		freeSlots().push_back(handle.index());
	}

	GLuint name(TextureHandle handle) {
		const Slot* slot = slotFor(handle);
		if (slot == nullptr && handle.isValid()) {
			// A stale or foreign handle samples texture 0; say so once rather than render silently wrong.
			static bool warned = false;
			if (!warned) {
				warned = true;
				LOG_WARNING(
					Renderer,
					"Texture handle %u (slot %u, generation %u) is not registered; using texture 0",
					handle.value,
					handle.index(),
					handle.generation()
				);
			}
		}
		return slot != nullptr ? slot->name : 0;
	}

} // namespace Renderer::backend::gl
