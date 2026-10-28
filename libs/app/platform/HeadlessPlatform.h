#pragma once

// A platform with no window and no GPU, for tests: the renderer runs on a NullBackend, time is a
// ManualClock, and what a window system would report is queued by hand and arrives in the next
// poll, in the order it was queued, as a callback would. Gamepads are polled as GLFW's are: each
// connected pad's state is sampled once per poll, after the queue, and diffed into events by the
// same GamepadTracker the GLFW platform runs.

#include "input/DeviceInfo.h"
#include "platform/Clock.h"
#include "platform/Gamepads.h"
#include "platform/Platform.h"

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
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

		// A gamepad arriving with its name and model GUID; pad.id.index is its joystick id, 0 to 15.
		// Stamped with the clock's time now, and sampled at every poll from the one it arrives in.
		void connectGamepad(const DeviceInfo& pad);
		void disconnectGamepad(std::uint8_t index);
		// What pad `index` holds from now on, sampled at each poll while it's connected. It outlasts a
		// disconnect, as a button held through one does.
		void setGamepad(std::uint8_t index, const GamepadSample& state);

		// The label the layout prints on `key`, which keyLabel gives back; keys with none give nullopt.
		void setKeyLabel(foundation::Key key, std::string label);

		Renderer::Viewport							viewport() const override { return current; }
		std::unique_ptr<Renderer::backend::Backend> createBackend() override;
		void										pollEvents(PlatformEvents& events) override;
		bool										focused() const override { return hasFocus; }
		bool										closeRequested() const override { return false; }
		void										present() override {}
		void										setVsync(bool /*enabled*/) override {}
		std::optional<std::string>					keyLabel(foundation::Key key) const override;

	  private:
		struct Queued {
			enum class Kind : std::uint8_t { Input, Resize, Focus, Connect, Disconnect };
			Kind				   kind = Kind::Input;
			foundation::InputEvent event{}; // carries the time for every kind
			Renderer::Viewport	   viewport{};
			DeviceInfo			   device{};
		};

		ManualClock												manualClock;
		Renderer::Viewport										current;
		bool													hasFocus = true;
		bool													focusOnceQueuedArrives = true;
		std::vector<Queued>										queued;
		GamepadTracker											gamepads;
		std::array<GamepadSample, foundation::kMaxRealGamepads> padStates{};
		std::map<foundation::Key, std::string>					keyLabels;
	};

} // namespace app
