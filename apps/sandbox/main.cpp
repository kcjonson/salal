// The sandbox: one scene on the GLFW platform, a panel and a dot orbiting over it at the fixed step.
// Where the dev server is compiled in it serves on 127.0.0.1:8090, or --http-port's port; close the
// window, or ask /api/control?action=exit, to quit.

#include "BaseShader.h"

#include <application/App.h>
#include <application/CommandLine.h>
#include <platform/Platform.h>
#include <primitives/Primitives.h>
#include <utils/Log.h>

#include <algorithm>
#include <cinttypes>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <numbers>
#include <utility>

namespace {

	constexpr int kDevServerPort = 8090; // worldsim's ui-sandbox port

	// The dot moves in fixed ticks and is drawn between the last two. The scene logs the frames it drew
	// and the ticks it ran every few seconds, and once more as it exits, and each resize it hears of.
	class OrbitScene final : public app::TypedScene<> {
	  public:
		const char* name() const override { return "orbit"; }

		void onEnter(app::App& app) override { owner = &app; }

		void fixedUpdate(const app::TickContext& tick) override {
			previousAngle = angle;
			angle += kTurnsPerSecond * 2.0 * std::numbers::pi * tick.step;
			++ticks;
		}

		void update(const app::FrameContext& frame) override {
			sinceReport += frame.dt;
			if (sinceReport >= kReportSeconds) {
				sinceReport = 0.0;
				report();
			}
		}

		void render(float alpha) override {
			++frames;
			const Renderer::Viewport& window = owner->viewport();
			const float				  centreX = static_cast<float>(window.logicalWidth) * 0.5F;
			const float				  centreY = static_cast<float>(window.logicalHeight) * 0.5F;
			const float				  radius = static_cast<float>(std::min(window.logicalWidth, window.logicalHeight)) * 0.3F;
			const double			  drawn = previousAngle + ((angle - previousAngle) * alpha);

			Renderer::Primitives::drawRect(
				{.bounds = {centreX - radius, centreY - radius, radius * 2.0F, radius * 2.0F},
				 .style = {
					 .fill = {0.2F, 0.25F, 0.35F, 1.0F},
					 .border = Foundation::BorderStyle{.color = {0.5F, 0.6F, 0.8F, 1.0F}, .width = 2.0F, .cornerRadius = 12.0F}
				 }}
			);
			Renderer::Primitives::drawCircle(
				{.center =
					 {centreX + (radius * static_cast<float>(std::cos(drawn))), centreY + (radius * static_cast<float>(std::sin(drawn)))},
				 .radius = radius * 0.12F,
				 .style = {.fill = {0.95F, 0.7F, 0.3F, 1.0F}}}
			);
		}

		void onResize(const Renderer::Viewport& viewport) override {
			LOG_INFO(Game, "orbit: resized to %dx%d", viewport.logicalWidth, viewport.logicalHeight);
		}

		void onExit() override { report(); }

	  private:
		static constexpr double kTurnsPerSecond = 0.25;
		static constexpr double kReportSeconds = 2.0;

		void report() const {
			const Renderer::MetricsCollector* metrics = owner != nullptr ? owner->metrics() : nullptr;
			const float						  fps = metrics != nullptr ? metrics->getCurrentMetrics().fps : 0.0F;
			LOG_INFO(Game, "orbit: %" PRIu64 " frames, %" PRIu64 " ticks, %.1f fps", frames, ticks, static_cast<double>(fps));
		}

		app::App*	  owner = nullptr;
		double		  angle = 0.0;
		double		  previousAngle = 0.0;
		double		  sinceReport = 0.0;
		std::uint64_t frames = 0;
		std::uint64_t ticks = 0;
	};

} // namespace

int main(int argc, char** argv) {
	const app::CommandLine commandLine = app::parseCommandLine(argc, argv);
	if (commandLine.help || !commandLine.valid) {
		std::printf("Usage: sandbox [--http-port <port>] [--help]\n");
		return commandLine.valid ? 0 : 1;
	}

	std::unique_ptr<app::Platform> platform = app::createGlfwPlatform({.title = "salal sandbox"});
	if (platform == nullptr) {
		return 1;
	}
	std::unique_ptr<app::App> app = app::App::create(
		{.applicationName = "sandbox",
		 .frame = {.fixedStep = 1.0 / 120.0},
		 .baseShader = base_shader::source(),
		 .devServer = {.enabled = true, .port = commandLine.httpPort.value_or(kDevServerPort)},
		 .metrics = true},
		std::move(platform)
	);
	if (app == nullptr) {
		return 1;
	}

	app->scenes().push<OrbitScene>();
	app->run();
	app->shutdown();
	return 0;
}
