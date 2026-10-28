// The native platform over GLFW: one window with a GL 3.3 core context, its framebuffer size, focus
// and close, and present with vsync. GLFW and GL stay in this file.

#include "platform/Platform.h"

#include <backend/gl/GLBackend.h>
#include <utils/Log.h>

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <optional>
#include <utility>
#include <vector>

namespace app {

	namespace {

		// GLFW is process-wide: a second platform's glfwTerminate would take the first one's window with it.
		bool g_glfwPlatformAlive = false; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

		class GlfwClock final : public Clock {
		  public:
			double now() const override { return glfwGetTime(); }
		};

		void errorCallback(int error, const char* description) {
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
				glfwSetWindowUserPointer(window, this);
				glfwSetFramebufferSizeCallback(window, framebufferSizeCallback);
				glfwSetWindowFocusCallback(window, focusCallback);
				g_glfwPlatformAlive = true;
			}

			~GlfwPlatform() override {
				glfwDestroyWindow(window);
				glfwTerminate();
				g_glfwPlatformAlive = false;
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

			// Callbacks fire inside glfwPollEvents; anything that arrived outside one is held for it.
			void pollEvents(PlatformEvents& events) override {
				listener = &events;
				if (heldResize) {
					events.framebufferResized(*std::exchange(heldResize, std::nullopt));
				}
				for (const foundation::InputEvent& event : std::exchange(heldEvents, {})) {
					events.inputEvent(event);
				}
				glfwPollEvents();
				listener = nullptr;
			}

			bool focused() const override { return hasFocus; }
			bool closeRequested() const override { return glfwWindowShouldClose(window) == GLFW_TRUE; }
			void present() override { glfwSwapBuffers(window); }
			void setVsync(bool enabled) override { glfwSwapInterval(enabled ? 1 : 0); }

		  private:
			static GlfwPlatform& from(GLFWwindow* window) { return *static_cast<GlfwPlatform*>(glfwGetWindowUserPointer(window)); }

			// A minimized window reports 0x0, which the App ignores; GL keeps the viewport it had.
			static void framebufferSizeCallback(GLFWwindow* window, int width, int height) {
				if (width > 0 && height > 0) {
					glViewport(0, 0, width, height);
				}
				GlfwPlatform&			 platform = from(window);
				const Renderer::Viewport viewport = viewportOf(window);
				if (platform.listener != nullptr) {
					platform.listener->framebufferResized(viewport);
				} else {
					platform.heldResize = viewport;
				}
			}

			static void focusCallback(GLFWwindow* window, int focused) {
				GlfwPlatform& platform = from(window);
				platform.hasFocus = focused == GLFW_TRUE;
				const foundation::InputEvent event{
					.type = platform.hasFocus ? foundation::InputEvent::Type::FocusGained : foundation::InputEvent::Type::FocusLost,
					.time = platform.glfwClock.now(),
				};
				if (platform.listener != nullptr) {
					platform.listener->inputEvent(event);
				} else {
					platform.heldEvents.push_back(event);
				}
			}

			GLFWwindow*							window;
			GlfwClock							glfwClock;
			PlatformEvents*						listener = nullptr;
			bool								hasFocus;
			std::optional<Renderer::Viewport>	heldResize;
			std::vector<foundation::InputEvent> heldEvents;
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
		if (g_glfwPlatformAlive) {
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
