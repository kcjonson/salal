#pragma once

// What the App runs on: the window and its GL context, the clock, the events the window system
// reports, gamepads, key labels, and present. createGlfwPlatform makes the native one;
// HeadlessPlatform runs a frame with no window and no GPU. No window-system or GL type appears here.

#include "input/DeviceInfo.h"
#include "platform/Clock.h"

#include <CoordinateSystem/CoordinateSystem.h>
#include <backend/Backend.h>
#include <input/InputTypes.h>

#include <memory>
#include <optional>
#include <string>

namespace app {

	struct WindowConfig {
		std::string		   title;
		float			   sizePercent = 0.8F; // of the primary monitor's work area
		std::optional<int> fullscreenMonitor;  // an index into the connected monitors; windowed when unset or out of range
		bool			   vsync = true;
	};

	// What a platform reports while it polls. The App is the one listener, and the dev server feeds
	// injected input through the same calls.
	class PlatformEvents {
	  public:
		// An input event, in arrival order, stamped with the platform's clock. Connections come through
		// deviceConnected and deviceDisconnected instead; a DeviceConnected or DeviceDisconnected event
		// given here is dropped, logged.
		virtual void inputEvent(const foundation::InputEvent& event) = 0;
		// A gamepad arrived, with the name and model GUID its event can't carry. Its DeviceConnected
		// joins the input events in arrival order, at `time`.
		virtual void deviceConnected(const DeviceInfo& device, double time) = 0;
		// A gamepad left. Its DeviceDisconnected joins the input events in arrival order, at `time`.
		virtual void deviceDisconnected(foundation::DeviceId device, double time) = 0;
		// The framebuffer changed size; the viewport holds the window's logical and framebuffer sizes.
		virtual void framebufferResized(const Renderer::Viewport& viewport) = 0;

	  protected:
		~PlatformEvents() = default;
	};

	class Platform {
	  public:
		virtual ~Platform() = default;

		virtual const Clock& clock() const = 0;

		// The window's size now, logical and framebuffer.
		virtual Renderer::Viewport viewport() const = 0;

		// The renderer backend for this platform's context, which the App hands to Primitives; null,
		// logged, when it cannot be made.
		virtual std::unique_ptr<Renderer::backend::Backend> createBackend() = 0;

		// Process what the window system queued, reporting it to `events` as it arrives, then sample the
		// connected gamepads once and report what changed since the last sample.
		virtual void pollEvents(PlatformEvents& events) = 0;

		virtual bool focused() const = 0;

		// The window's close was asked for (its close button, or the system's quit).
		virtual bool closeRequested() const = 0;

		// Show the frame just drawn, waiting for the display when vsync is on.
		virtual void present() = 0;

		virtual void setVsync(bool enabled) = 0;

		// What the keyboard layout prints on a key, for keys that print something; nullopt for the rest,
		// and where the window system can't say.
		virtual std::optional<std::string> keyLabel(foundation::Key key) const = 0;
	};

	// A window with a GL 3.3 core context current on the calling thread; null, logged, when the window
	// system or the window can't start, and while another GLFW platform is alive (GLFW is process-wide).
	// Its createBackend() is the GL backend on that context.
	std::unique_ptr<Platform> createGlfwPlatform(const WindowConfig& config);

} // namespace app
