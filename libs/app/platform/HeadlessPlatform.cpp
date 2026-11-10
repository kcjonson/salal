#include "platform/HeadlessPlatform.h"

#include <backend/NullBackend.h>
#include <utils/Log.h>

#include <algorithm>
#include <cmath>
#include <utility>

namespace app {

	HeadlessPlatform::HeadlessPlatform(int width, int height)
		: current{.logicalWidth = width, .logicalHeight = height, .framebufferWidth = width, .framebufferHeight = height},
		  windowed{.width = width, .height = height} {}

	void HeadlessPlatform::push(const foundation::InputEvent& event) {
		foundation::InputEvent stamped = event;
		stamped.time = manualClock.now();
		queued.push_back({.kind = Queued::Kind::Input, .event = stamped});
	}

	void HeadlessPlatform::resize(int width, int height) {
		const Size size = width > 0 && height > 0 ? atLeastMinimum({.width = width, .height = height}) : Size{};
		queued.push_back({.kind = Queued::Kind::Resize, .viewport = viewportAt(size)});
	}

	void HeadlessPlatform::setPixelRatio(float ratio) {
		if (!(ratio > 0.0F)) {
			LOG_WARNING(Engine, "A pixel ratio of %g refused", static_cast<double>(ratio));
			return;
		}
		if (ratio == pixelRatio) {
			return;
		}
		pixelRatio = ratio;
		const Size size = atLeastMinimum({.width = current.logicalWidth, .height = current.logicalHeight});
		windowed = atLeastMinimum(windowed);
		current = viewportAt(size);
		queued.push_back({.kind = Queued::Kind::Resize, .viewport = current});
	}

	void HeadlessPlatform::setMinimumSize(int width, int height) {
		minimum = {.width = std::max(0, width), .height = std::max(0, height)};
		windowed = atLeastMinimum(windowed);
		if (!fullscreen) {
			resizeNow(windowed);
		}
	}

	Renderer::Viewport HeadlessPlatform::viewportAt(Size size) const {
		return {
			.logicalWidth = size.width,
			.logicalHeight = size.height,
			.framebufferWidth = static_cast<int>(std::lround(static_cast<float>(size.width) * pixelRatio)),
			.framebufferHeight = static_cast<int>(std::lround(static_cast<float>(size.height) * pixelRatio)),
		};
	}

	HeadlessPlatform::Size HeadlessPlatform::atLeastMinimum(Size size) const {
		return {
			.width = std::max(size.width, static_cast<int>(std::ceil(static_cast<float>(minimum.width) / pixelRatio))),
			.height = std::max(size.height, static_cast<int>(std::ceil(static_cast<float>(minimum.height) / pixelRatio))),
		};
	}

	void HeadlessPlatform::setFocused(bool focused) {
		if (focused == focusOnceQueuedArrives) {
			return;
		}
		focusOnceQueuedArrives = focused;
		const auto type = focused ? foundation::InputEvent::Type::FocusGained : foundation::InputEvent::Type::FocusLost;
		queued.push_back({.kind = Queued::Kind::Focus, .event = {.type = type, .time = manualClock.now()}});
	}

	void HeadlessPlatform::connectGamepad(const DeviceInfo& pad) {
		queued.push_back({.kind = Queued::Kind::Connect, .event = {.time = manualClock.now()}, .device = pad});
	}

	void HeadlessPlatform::disconnectGamepad(std::uint8_t index) {
		queued.push_back({.kind = Queued::Kind::Disconnect, .event = {.time = manualClock.now()}, .device = {.id = padId(index)}});
	}

	void HeadlessPlatform::setGamepad(std::uint8_t index, const GamepadSample& state) {
		if (index < padStates.size()) {
			padStates[index] = state;
		}
	}

	void HeadlessPlatform::setKeyLabel(foundation::Key key, std::string label) {
		keyLabels[key] = std::move(label);
	}

	void HeadlessPlatform::addMonitor(int width, int height) {
		monitors.push_back({.width = width, .height = height});
	}

	void HeadlessPlatform::setFullscreen(std::optional<int> monitor) {
		if (monitor && (*monitor < 0 || static_cast<std::size_t>(*monitor) >= monitors.size())) {
			LOG_WARNING(Engine, "Fullscreen monitor %d isn't connected (%zu are); the window stays as it is", *monitor, monitors.size());
			return;
		}
		const std::optional<int> target = monitor;
		if (target == fullscreen) {
			return;
		}
		if (!fullscreen) {
			windowed = {.width = current.logicalWidth, .height = current.logicalHeight};
		}
		fullscreen = target;
		resizeNow(target ? monitors[static_cast<std::size_t>(*target)] : windowed);
	}

	void HeadlessPlatform::setWindowSize(int width, int height) {
		if (width < 1 || height < 1) {
			LOG_WARNING(Engine, "A window size of %dx%d refused", width, height);
			return;
		}
		windowed = atLeastMinimum({.width = width, .height = height});
		if (!fullscreen) {
			resizeNow(windowed);
		}
	}

	void HeadlessPlatform::resizeNow(Size size) {
		if (size.width == current.logicalWidth && size.height == current.logicalHeight) {
			return;
		}
		current = viewportAt(size);
		queued.push_back({.kind = Queued::Kind::Resize, .viewport = current});
	}

	std::optional<std::string> HeadlessPlatform::keyLabel(foundation::Key key) const {
		const auto found = keyLabels.find(key);
		if (found == keyLabels.end()) {
			return std::nullopt;
		}
		return found->second;
	}

	std::unique_ptr<Renderer::backend::Backend> HeadlessPlatform::createBackend() {
		return std::make_unique<Renderer::backend::NullBackend>();
	}

	void HeadlessPlatform::pollEvents(PlatformEvents& events) {
		const std::vector<Queued> arrived = std::exchange(queued, {});
		for (const Queued& item : arrived) {
			switch (item.kind) {
				case Queued::Kind::Input:
					events.inputEvent(item.event);
					break;
				case Queued::Kind::Resize:
					current = item.viewport;
					events.framebufferResized(current);
					break;
				case Queued::Kind::Focus:
					hasFocus = item.event.type == foundation::InputEvent::Type::FocusGained;
					events.inputEvent(item.event);
					break;
				case Queued::Kind::Connect:
					gamepads.connect(item.device, item.event.time, events);
					break;
				case Queued::Kind::Disconnect:
					gamepads.disconnect(item.device.id.index, item.event.time, events);
					break;
			}
		}
		for (std::uint8_t pad = 0; pad < padStates.size(); ++pad) {
			gamepads.sample(pad, padStates[pad], manualClock.now(), events);
		}
	}

} // namespace app
