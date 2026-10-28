#pragma once

// A platform with no window and no GPU, for tests: the renderer runs on a NullBackend, time is a
// ManualClock, and what a window system would report is queued by hand and arrives in the next
// poll, in the order it was queued, as a callback would.

#include "platform/Clock.h"
#include "platform/Platform.h"

#include <cstdint>
#include <vector>

namespace app {

	class HeadlessPlatform final : public Platform {
	  public:
		// A focused window of width x height logical pixels, at one framebuffer pixel each.
		explicit HeadlessPlatform(int width, int height);

		ManualClock&	   clock() { return manualClock; }
		const ManualClock& clock() const override { return manualClock; }

		// An input event, stamped with the clock's time now.
		void push(const foundation::InputEvent& event);
		// A framebuffer resize to width x height, logical and framebuffer alike.
		void resize(int width, int height);
		// Focus changes, with the FocusLost or FocusGained event a window system sends; no change, no event.
		void setFocused(bool focused);

		Renderer::Viewport							viewport() const override { return current; }
		std::unique_ptr<Renderer::backend::Backend> createBackend() override;
		void										pollEvents(PlatformEvents& events) override;
		bool										focused() const override { return hasFocus; }
		bool										closeRequested() const override { return false; }
		void										present() override {}
		void										setVsync(bool /*enabled*/) override {}

	  private:
		struct Queued {
			enum class Kind : std::uint8_t { Input, Resize, Focus };
			Kind				   kind = Kind::Input;
			foundation::InputEvent event{};
			Renderer::Viewport	   viewport{};
		};

		ManualClock			manualClock;
		Renderer::Viewport	current;
		bool				hasFocus = true;
		bool				focusOnceQueuedArrives = true;
		std::vector<Queued> queued;
	};

} // namespace app
