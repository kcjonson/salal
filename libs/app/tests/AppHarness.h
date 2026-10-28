#pragma once

// What the app tests share: an App on a HeadlessPlatform, scenes and overlays that write each call
// they get into one journal, and per-scene hooks for what a test needs a scene to do mid-frame.
// The steps are powers of two so the clock's sums are exact and tick counts can be.

#include "application/App.h"
#include "platform/HeadlessPlatform.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdio>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace app::testing {

	inline constexpr double kStep = 1.0 / 64.0;

	inline std::vector<std::string>& journal() {
		static std::vector<std::string> entries;
		return entries;
	}

	inline void record(std::string_view who, std::string_view what) {
		journal().push_back(std::string(who) + "." + std::string(what));
	}

	inline std::string alphaText(float alpha) {
		char text[32];
		std::snprintf(text, sizeof(text), "%g", static_cast<double>(alpha));
		return text;
	}

	// The journal's entries starting with `prefix`.
	inline int count(std::string_view prefix) {
		return static_cast<int>(std::ranges::count_if(journal(), [&](const std::string& entry) { return entry.starts_with(prefix); }));
	}

	// The journal so far, emptied.
	inline std::vector<std::string> take() {
		return std::exchange(journal(), {});
	}

	// The journal's first and last n entries, or as many as there are, so a short journal fails a comparison
	// rather than reading past its end.
	inline std::vector<std::string> head(std::size_t n) {
		const std::vector<std::string>& all = journal();
		return {all.begin(), all.begin() + static_cast<std::ptrdiff_t>(std::min(n, all.size()))};
	}

	inline std::vector<std::string> tail(std::size_t n) {
		const std::vector<std::string>& all = journal();
		return {all.end() - static_cast<std::ptrdiff_t>(std::min(n, all.size())), all.end()};
	}

	// What a test has a scene do, by the scene's name; each runs after the call is recorded.
	struct Hooks {
		std::function<void(App&)>								 onEnter;
		std::function<void(App&)>								 onExit;
		std::function<void(App&)>								 onUncovered;
		std::function<bool(App&, const foundation::InputEvent&)> handleEvent;
		std::function<void(App&, const TickContext&)>			 fixedUpdate;
		std::function<void(App&, const FrameContext&)>			 update;
		std::function<void(App&)>								 render;
	};

	inline std::map<std::string, Hooks, std::less<>>& hooks() {
		static std::map<std::string, Hooks, std::less<>> byScene;
		return byScene;
	}

	// A scene named Name that records every call: "<Name>.built", "<Name>.onEnter", "<Name>.fixedUpdate <tick>",
	// "<Name>.render <alpha>", "<Name>.onResize <w>x<h>" and so on.
	template <const char* Name, typename A = NoArgs, typename R = NoResult, bool Opaque = true>
	class Recording : public TypedScene<A, R> {
	  public:
		Recording()
			requires std::same_as<A, NoArgs>
		{
			record(Name, "built");
		}
		explicit Recording(A&& sceneArgs)
			requires(!std::same_as<A, NoArgs>)
			: args(std::move(sceneArgs)) {
			record(Name, "built");
		}
		~Recording() override { record(Name, "destroyed"); }

		Recording(const Recording&) = delete;
		Recording& operator=(const Recording&) = delete;
		Recording(Recording&&) = delete;
		Recording& operator=(Recording&&) = delete;

		using TypedScene<A, R>::finish;

		const char* name() const override { return Name; }
		void		onEnter(App& app) override {
			   owner = &app;
			   record(Name, "onEnter");
			   run(&Hooks::onEnter);
		}
		void onExit() override {
			record(Name, "onExit");
			run(&Hooks::onExit);
		}
		void onCovered() override { record(Name, "onCovered"); }
		void onUncovered() override {
			record(Name, "onUncovered");
			run(&Hooks::onUncovered);
		}
		bool handleEvent(const foundation::InputEvent& event) override {
			record(Name, "handleEvent");
			events.push_back(event);
			const Hooks* hook = hooksFor();
			return hook != nullptr && hook->handleEvent && hook->handleEvent(*owner, event);
		}
		void fixedUpdate(const TickContext& tick) override {
			record(Name, "fixedUpdate " + std::to_string(tick.tick));
			ticks.push_back(tick.input);
			if (const Hooks* hook = hooksFor(); hook != nullptr && hook->fixedUpdate) {
				hook->fixedUpdate(*owner, tick);
			}
		}
		void update(const FrameContext& frame) override {
			record(Name, "update");
			frames.push_back(frame.dt);
			if (const Hooks* hook = hooksFor(); hook != nullptr && hook->update) {
				hook->update(*owner, frame);
			}
		}
		void render(float alpha) override {
			record(Name, "render " + alphaText(alpha));
			run(&Hooks::render);
		}
		void onResize(const Renderer::Viewport& viewport) override {
			record(Name, "onResize " + std::to_string(viewport.logicalWidth) + "x" + std::to_string(viewport.logicalHeight));
		}
		bool opaque() const override { return Opaque; }

		App*								   owner = nullptr;
		A									   args{};
		std::vector<foundation::InputEvent>	   events;
		std::vector<foundation::InputSnapshot> ticks;
		std::vector<double>					   frames;

	  private:
		const Hooks* hooksFor() const {
			const auto it = hooks().find(std::string_view(Name));
			return it != hooks().end() ? &it->second : nullptr;
		}
		void run(std::function<void(App&)> Hooks::* which) {
			if (const Hooks* hook = hooksFor(); hook != nullptr && *hook.*which) {
				(*hook.*which)(*owner);
			}
		}
	};

	// An overlay that records its calls as "<name>.<call>", and consumes events when told to.
	class RecordingOverlay final : public Overlay {
	  public:
		explicit RecordingOverlay(std::string overlayName, bool consumeEvents = false)
			: label(std::move(overlayName)),
			  consume(consumeEvents) {}
		~RecordingOverlay() override { record(label, "destroyed"); }

		RecordingOverlay(const RecordingOverlay&) = delete;
		RecordingOverlay& operator=(const RecordingOverlay&) = delete;
		RecordingOverlay(RecordingOverlay&&) = delete;
		RecordingOverlay& operator=(RecordingOverlay&&) = delete;

		bool handleEvent(const foundation::InputEvent& /*event*/) override {
			record(label, "handleEvent");
			return consume;
		}
		void update(const FrameContext& /*frame*/) override { record(label, "update"); }
		void render() override { record(label, "render"); }
		void onResize(const Renderer::Viewport& viewport) override {
			record(label, "onResize " + std::to_string(viewport.logicalWidth) + "x" + std::to_string(viewport.logicalHeight));
		}
		void onExit() override { record(label, "onExit"); }

	  private:
		std::string label;
		bool		consume;
	};

	// An App on an 800x600 HeadlessPlatform, its frame stepped at kStep unless a test says otherwise.
	class AppTest : public ::testing::Test {
	  protected:
		void SetUp() override {
			journal().clear();
			hooks().clear();
		}
		void TearDown() override {
			app.reset();
			hooks().clear();
			journal().clear();
		}

		static AppConfig config(FrameConfig frame = {.fixedStep = kStep}) { return {.frame = frame}; }

		// `prepare` sets the platform up before the App is made on it (monitors, a window state).
		App& start(AppConfig appConfig = config(), const std::function<void(HeadlessPlatform&)>& prepare = {}) {
			auto headless = std::make_unique<HeadlessPlatform>(800, 600);
			platform = headless.get();
			if (prepare) {
				prepare(*headless);
			}
			app = App::create(std::move(appConfig), std::move(headless));
			EXPECT_NE(app, nullptr);
			return *app;
		}

		// Advance the clock by `seconds` and run a frame.
		bool frame(double seconds = kStep) {
			platform->clock().advance(seconds);
			return app->tick();
		}

		template <typename S>
		S& top() {
			Scene* scene = app->scenes().top();
			EXPECT_NE(scene, nullptr);
			return *static_cast<S*>(scene);
		}

		HeadlessPlatform*	 platform = nullptr;
		std::unique_ptr<App> app;
	};

} // namespace app::testing
