#include "platform/EventRelay.h"

#include <utility>

namespace app {

	void EventRelay::attach(PlatformEvents& events) {
		listener = &events;
		for (const Held& item : std::exchange(held, {})) {
			deliver(events, item);
		}
	}

	void EventRelay::detach() {
		listener = nullptr;
	}

	void EventRelay::inputEvent(const foundation::InputEvent& event) {
		relay(event);
	}

	void EventRelay::deviceConnected(const DeviceInfo& device, double time) {
		relay(Connected{.device = device, .time = time});
	}

	void EventRelay::deviceDisconnected(foundation::DeviceId device, double time) {
		relay(Disconnected{.device = device, .time = time});
	}

	void EventRelay::framebufferResized(const Renderer::Viewport& viewport) {
		if (listener == nullptr) {
			std::erase_if(held, [](const Held& item) { return std::holds_alternative<Renderer::Viewport>(item); });
		}
		relay(viewport);
	}

	void EventRelay::deliver(PlatformEvents& events, const Held& item) {
		if (const auto* event = std::get_if<foundation::InputEvent>(&item)) {
			events.inputEvent(*event);
		} else if (const auto* connected = std::get_if<Connected>(&item)) {
			events.deviceConnected(connected->device, connected->time);
		} else if (const auto* disconnected = std::get_if<Disconnected>(&item)) {
			events.deviceDisconnected(disconnected->device, disconnected->time);
		} else if (const auto* viewport = std::get_if<Renderer::Viewport>(&item)) {
			events.framebufferResized(*viewport);
		}
	}

	void EventRelay::relay(Held item) {
		if (listener != nullptr) {
			deliver(*listener, item);
		} else {
			held.push_back(std::move(item));
		}
	}

} // namespace app
