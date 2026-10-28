// The native platform over GLFW: one window with a GL 3.3 core context, its framebuffer size, focus
// and close, the keyboard, the mouse and gamepads, key labels, the clipboard, and present with vsync.
// GLFW and GL stay in this file and in GlfwInput.cpp, which turns GLFW's values into events.

#include "platform/EventRelay.h"
#include "platform/Gamepads.h"
#include "platform/GlfwInput.h"
#include "platform/Platform.h"

#include <backend/gl/GLBackend.h>
#include <utils/Log.h>

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <array>
#include <cstdint>
#include <optional>

namespace app {

	namespace {

		class GlfwPlatform;

		// GLFW is process-wide: a second platform's glfwTerminate would take the first one's window with
		// it, and the joystick callback has no window to find the platform through.
		GlfwPlatform* g_glfwPlatform = nullptr; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

		class GlfwClock final : public Clock {
		  public:
			double now() const override { return glfwGetTime(); }
		};

		// An empty clipboard, or one holding no text, is normal, though GLFW reports it as an error.
		void errorCallback(int error, const char* description) {
			if (error == GLFW_FORMAT_UNAVAILABLE) {
				LOG_DEBUG(Engine, "GLFW (%d): %s", error, description);
				return;
			}
			LOG_ERROR(Engine, "GLFW Error (%d): %s", error, description);
		}

		Renderer::Viewport viewportOf(GLFWwindow* window) {
			Renderer::Viewport viewport;
			glfwGetWindowSize(window, &viewport.logicalWidth, &viewport.logicalHeight);
			glfwGetFramebufferSize(window, &viewport.framebufferWidth, &viewport.framebufferHeight);
			return viewport;
		}

		class GlfwPlatform final : public Platform {
		  public:
			explicit GlfwPlatform(GLFWwindow* glfwWindow)
				: window(glfwWindow),
				  hasFocus(glfwGetWindowAttrib(glfwWindow, GLFW_FOCUSED) == GLFW_TRUE) {
				g_glfwPlatform = this;
				glfwSetWindowUserPointer(window, this);
				glfwSetFramebufferSizeCallback(window, framebufferSizeCallback);
				glfwSetWindowFocusCallback(window, focusCallback);
				glfwSetKeyCallback(window, keyCallback);
				glfwSetCharCallback(window, charCallback);
				glfwSetCursorPosCallback(window, cursorPosCallback);
				glfwSetCursorEnterCallback(window, cursorEnterCallback);
				glfwSetMouseButtonCallback(window, mouseButtonCallback);
				glfwSetScrollCallback(window, scrollCallback);
				glfwSetJoystickCallback(joystickCallback);

				refreshCursor();
				const bool hovered = glfwGetWindowAttrib(window, GLFW_HOVERED) == GLFW_TRUE;
				for (const foundation::InputEvent& event : glfw::cursorAtOpen(hovered, cursor.x, cursor.y, glfwClock.now())) {
					relay.inputEvent(event);
				}

				// glfwInit found the pads already plugged in, before there was a callback to hear of them.
				for (int jid = 0; jid < foundation::kMaxRealGamepads; ++jid) {
					joystickConnected(jid);
				}
			}

			~GlfwPlatform() override {
				glfwSetJoystickCallback(nullptr);
				glfwDestroyWindow(window);
				glfwTerminate();
				g_glfwPlatform = nullptr;
			}

			GlfwPlatform(const GlfwPlatform&) = delete;
			GlfwPlatform& operator=(const GlfwPlatform&) = delete;
			GlfwPlatform(GlfwPlatform&&) = delete;
			GlfwPlatform& operator=(GlfwPlatform&&) = delete;

			const Clock& clock() const override { return glfwClock; }

			Renderer::Viewport viewport() const override { return viewportOf(window); }

			std::unique_ptr<Renderer::backend::Backend> createBackend() override {
				std::unique_ptr<Renderer::backend::gl::GLBackend> backend = Renderer::backend::gl::GLBackend::create();
				if (backend == nullptr) {
					return nullptr;
				}
				LOG_INFO(Renderer, "OpenGL Version: %s", reinterpret_cast<const char*>(glGetString(GL_VERSION)));
				LOG_INFO(Renderer, "GLSL Version: %s", reinterpret_cast<const char*>(glGetString(GL_SHADING_LANGUAGE_VERSION)));
				return backend;
			}

			// Callbacks fire inside glfwPollEvents, and a pad found gone while sampling disconnects inside
			// glfwGetGamepadState, so both run with the listener attached.
			void pollEvents(PlatformEvents& events) override {
				relay.attach(events);
				glfwPollEvents();
				const double now = glfwClock.now();
				for (std::uint8_t pad = 0; pad < foundation::kMaxRealGamepads; ++pad) {
					GLFWgamepadstate state;
					if (gamepads.connected(pad) && glfwGetGamepadState(pad, &state) == GLFW_TRUE) {
						gamepads.sample(pad, glfw::gamepadSample(state.buttons, state.axes, triggersSeen[pad]), now, relay);
					}
				}
				relay.detach();
			}

			bool focused() const override { return hasFocus; }
			bool closeRequested() const override { return glfwWindowShouldClose(window) == GLFW_TRUE; }
			void present() override { glfwSwapBuffers(window); }
			void setVsync(bool enabled) override { glfwSwapInterval(enabled ? 1 : 0); }

			std::optional<std::string> keyLabel(foundation::Key key) const override {
				// A key this keyboard lacks has no scancode, and glfwGetKeyName raises an error for it.
				const int glfwKey = glfw::toGlfwKey(key);
				if (glfwKey == GLFW_KEY_UNKNOWN || glfwGetKeyScancode(glfwKey) < 0) {
					return std::nullopt;
				}
				const char* label = glfwGetKeyName(glfwKey, 0);
				if (label == nullptr) {
					return std::nullopt;
				}
				return std::string(label);
			}

			std::string clipboardText() const override {
				const char* text = glfwGetClipboardString(window);
				return text == nullptr ? std::string() : std::string(text);
			}

			void setClipboardText(const std::string& text) override { glfwSetClipboardString(window, text.c_str()); }

		  private:
			static GlfwPlatform& from(GLFWwindow* window) { return *static_cast<GlfwPlatform*>(glfwGetWindowUserPointer(window)); }

			// A minimized window reports 0x0, which the App ignores; GL keeps the viewport it had.
			// A resize can move the content under a still cursor, so its cached position is read again.
			static void framebufferSizeCallback(GLFWwindow* window, int width, int height) {
				if (width > 0 && height > 0) {
					glViewport(0, 0, width, height);
				}
				GlfwPlatform& platform = from(window);
				platform.refreshCursor();
				platform.relay.framebufferResized(viewportOf(window));
			}

			static void focusCallback(GLFWwindow* window, int focused) {
				GlfwPlatform& platform = from(window);
				platform.hasFocus = focused == GLFW_TRUE;
				if (platform.hasFocus) {
					platform.refreshCursor();
				}
				platform.relay.inputEvent({
					.type = platform.hasFocus ? foundation::InputEvent::Type::FocusGained : foundation::InputEvent::Type::FocusLost,
					.time = platform.glfwClock.now(),
				});
			}

			static void keyCallback(GLFWwindow* window, int key, int /*scancode*/, int action, int mods) {
				GlfwPlatform& platform = from(window);
				platform.relay.inputEvent(glfw::keyEvent(key, action, mods, platform.glfwClock.now()));
			}

			static void charCallback(GLFWwindow* window, unsigned int codepoint) {
				GlfwPlatform& platform = from(window);
				platform.relay.inputEvent(glfw::charEvent(codepoint, platform.glfwClock.now()));
			}

			// The last position is what button and scroll events carry, in order with the moves.
			static void cursorPosCallback(GLFWwindow* window, double x, double y) {
				GlfwPlatform&				 platform = from(window);
				const foundation::InputEvent event = glfw::cursorPosEvent(x, y, platform.glfwClock.now());
				platform.cursor = event.position;
				platform.relay.inputEvent(event);
			}

			static void cursorEnterCallback(GLFWwindow* window, int entered) {
				GlfwPlatform& platform = from(window);
				platform.relay.inputEvent(glfw::cursorEnterEvent(entered, platform.glfwClock.now()));
			}

			static void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
				GlfwPlatform& platform = from(window);
				platform.relay.inputEvent(glfw::mouseButtonEvent(button, action, mods, platform.cursor, platform.glfwClock.now()));
			}

			static void scrollCallback(GLFWwindow* window, double x, double y) {
				GlfwPlatform& platform = from(window);
				platform.relay.inputEvent(glfw::scrollEvent(x, y, platform.cursor, platform.glfwClock.now()));
			}

			static void joystickCallback(int jid, int event) {
				if (g_glfwPlatform == nullptr || jid < 0 || jid >= foundation::kMaxRealGamepads) {
					return;
				}
				if (event == GLFW_CONNECTED) {
					g_glfwPlatform->joystickConnected(jid);
				} else if (event == GLFW_DISCONNECTED) {
					g_glfwPlatform->triggersSeen[static_cast<std::size_t>(jid)] = {};
					g_glfwPlatform->gamepads.disconnect(
						static_cast<std::uint8_t>(jid), g_glfwPlatform->glfwClock.now(), g_glfwPlatform->relay
					);
				}
			}

			// Where the cursor is now, for button and scroll events until the next move reports it.
			void refreshCursor() {
				double x = 0.0;
				double y = 0.0;
				glfwGetCursorPos(window, &x, &y);
				cursor = {static_cast<float>(x), static_cast<float>(y)};
			}

			// Only a joystick with a gamepad mapping, from GLFW's built-in SDL database, is a gamepad.
			void joystickConnected(int jid) {
				if (glfwJoystickIsGamepad(jid) != GLFW_TRUE) {
					if (glfwJoystickPresent(jid) == GLFW_TRUE) {
						[[maybe_unused]] const char* name = glfwGetJoystickName(jid);
						LOG_INFO(Engine, "Joystick %d (%s) has no gamepad mapping; not a gamepad", jid, name != nullptr ? name : "unnamed");
					}
					return;
				}
				const char* name = glfwGetGamepadName(jid);
				const char* guid = glfwGetJoystickGUID(jid);
				gamepads.connect(
					{.id = padId(static_cast<std::uint8_t>(jid)),
					 .name = name != nullptr ? name : "",
					 .modelGuid = guid != nullptr ? guid : ""},
					glfwClock.now(),
					relay
				);
			}

			GLFWwindow*													 window;
			GlfwClock													 glfwClock;
			bool														 hasFocus;
			glm::vec2													 cursor{};
			EventRelay													 relay;
			GamepadTracker												 gamepads;
			std::array<glfw::TriggersSeen, foundation::kMaxRealGamepads> triggersSeen{};
		};

		// The window's size: sizePercent of the primary monitor's work area, or the whole of a
		// fullscreen monitor. Null, logged, with no monitor or video mode to size it by.
		GLFWwindow* createWindow(const WindowConfig& config) {
			GLFWmonitor* fullscreen = nullptr;
			if (config.fullscreenMonitor) {
				int					monitorCount = 0;
				GLFWmonitor** const monitors = glfwGetMonitors(&monitorCount);
				if (*config.fullscreenMonitor >= 0 && *config.fullscreenMonitor < monitorCount) {
					fullscreen = monitors[*config.fullscreenMonitor]; // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
				} else {
					LOG_WARNING(
						Engine, "Fullscreen monitor %d isn't connected (%d are); windowed", *config.fullscreenMonitor, monitorCount
					);
				}
			}

			int width = 0;
			int height = 0;
			if (fullscreen != nullptr) {
				const GLFWvidmode* mode = glfwGetVideoMode(fullscreen);
				if (mode == nullptr) {
					LOG_ERROR(Engine, "Monitor %d has no video mode", *config.fullscreenMonitor);
					return nullptr;
				}
				width = mode->width;
				height = mode->height;
				LOG_INFO(Engine, "Window: fullscreen %dx%d on monitor %d", width, height, *config.fullscreenMonitor);
			} else {
				GLFWmonitor* primary = glfwGetPrimaryMonitor();
				if (primary == nullptr) {
					LOG_ERROR(Engine, "No primary monitor to size the window by");
					return nullptr;
				}
				int areaX = 0;
				int areaY = 0;
				int areaWidth = 0;
				int areaHeight = 0;
				glfwGetMonitorWorkarea(primary, &areaX, &areaY, &areaWidth, &areaHeight);
				if (areaWidth <= 0 || areaHeight <= 0) {
					const GLFWvidmode* mode = glfwGetVideoMode(primary);
					if (mode == nullptr) {
						LOG_ERROR(Engine, "The primary monitor has no work area or video mode to size the window by");
						return nullptr;
					}
					areaWidth = mode->width;
					areaHeight = mode->height;
				}
				width = static_cast<int>(static_cast<float>(areaWidth) * config.sizePercent);
				height = static_cast<int>(static_cast<float>(areaHeight) * config.sizePercent);
				LOG_INFO(Engine, "Work area: %dx%d", areaWidth, areaHeight);
				LOG_INFO(Engine, "Window: %dx%d (%.0f%% of the work area)", width, height, config.sizePercent * 100.0F);
			}

			glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
			glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
			glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
			glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
			return glfwCreateWindow(width, height, config.title.c_str(), fullscreen, nullptr);
		}

	} // namespace

	std::unique_ptr<Platform> createGlfwPlatform(const WindowConfig& config) {
		if (g_glfwPlatform != nullptr) {
			LOG_ERROR(Engine, "createGlfwPlatform: a GLFW platform is alive already, and there is one at a time");
			return nullptr;
		}
		glfwSetErrorCallback(errorCallback);
		if (glfwInit() != GLFW_TRUE) {
			LOG_ERROR(Engine, "Failed to initialize GLFW");
			return nullptr;
		}
		GLFWwindow* window = createWindow(config);
		if (window == nullptr) {
			LOG_ERROR(Engine, "Failed to create GLFW window");
			glfwTerminate();
			return nullptr;
		}
		glfwMakeContextCurrent(window);
		glfwSwapInterval(config.vsync ? 1 : 0);
		return std::make_unique<GlfwPlatform>(window);
	}

} // namespace app
