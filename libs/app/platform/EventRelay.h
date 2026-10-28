#pragma once

// Passes what a window system's callbacks report to the poll's listener. What arrives outside a poll
// (the pads found at startup, a resize or focus change from a window call) is held, in order, and goes
// to the next poll's listener before anything new; a held resize goes first, the latest only.

#include "input/DeviceInfo.h"
#include "platform/Platform.h"

#include <input/InputTypes.h>

#include <optional>
#include <variant>
#include <vector>

namespace app {

	class EventRelay final : public PlatformEvents {
	  public:
		// Delivers what's held to `events`, then passes what arrives to it until detach.
		void attach(PlatformEvents& events);
		void detach();

		void inputEvent(const foundation::InputEvent& event) override;
		void deviceConnected(const DeviceInfo& device, double time) override;
		void deviceDisconnected(foundation::DeviceId device, double time) override;
		void framebufferResized(const Renderer::Viewport& viewport) override;

	  private:
		struct Connected {
			DeviceInfo device;
			double	   time = 0.0;
		};
		struct Disconnected {
			foundation::DeviceId device;
			double				 time = 0.0;
		};
		using Held = std::variant<foundation::InputEvent, Connected, Disconnected>;

		static void deliver(PlatformEvents& events, const Held& item);
		void		relay(Held item);

		PlatformEvents*					  listener = nullptr;
		std::optional<Renderer::Viewport> heldResize;
		std::vector<Held>				  held;
	};

} // namespace app
