// The frame: exact tick counts, the delta clamp and the tick cap, alpha, pause and step, focus,
// input through the platform to dispatch and one tick, setDispatch, exit and window close,
// shutdown's order, resize, metrics, fonts, the context object, and what create refuses.

#include "application/App.h"
#include "tests/AppHarness.h"

#include <backend/NullBackend.h>
#include <primitives/Primitives.h>
#include <utils/Log.h>
#include <utils/ResourcePath.h>

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace app::testing {

	namespace {

		inline constexpr char kA[] = "A";
		inline constexpr char kB[] = "B";

		using SceneA = Recording<kA>;
		using SceneB = Recording<kB>;

		using foundation::InputEvent;
		using foundation::Key;

		InputEvent keyDown(Key key) {
			return {.type = InputEvent::Type::KeyDown, .key = key};
		}

		InputEvent keyUp(Key key) {
			return {.type = InputEvent::Type::KeyUp, .key = key};
		}

		const foundation::ButtonState& keyIn(const foundation::InputSnapshot& snapshot, Key key) {
			return snapshot.keys.at(static_cast<std::size_t>(key));
		}

		// A platform whose window can be closed, which counts its presents, keeps its vsync, can take
		// time to present and can throw from its poll.
		class ClosablePlatform final : public Platform {
		  public:
			const Clock&								clock() const override { return time; }
			Renderer::Viewport							viewport() const override { return {800, 600, 800, 600}; }
			std::unique_ptr<Renderer::backend::Backend> createBackend() override {
				return std::make_unique<Renderer::backend::NullBackend>();
			}
			void pollEvents(PlatformEvents& /*events*/) override {
				if (throwOnPoll) {
					throw std::runtime_error("poll");
				}
				closing = closing || closeOnNextPoll;
			}
			bool focused() const override { return true; }
			bool closeRequested() const override { return closing; }
			void present() override {
				++presents;
				time.advance(presentSeconds);
			}
			void					   setVsync(bool enabled) override { vsync = enabled; }
			std::optional<std::string> keyLabel(foundation::Key /*key*/) const override { return std::nullopt; }
			std::string				   clipboardText() const override { return {}; }
			void					   setClipboardText(const std::string& /*text*/) override {}

			ManualClock time;
			bool		closeOnNextPoll = false;
			bool		closing = false;
			bool		throwOnPoll = false;
			int			presents = 0;
			double		presentSeconds = 0.0;
			bool		vsync = true;
		};

#ifdef DEVELOPMENT_BUILD
		void countDroppedSteps(void* context, const foundation::LogRecord& record) {
			if (record.level == foundation::LogLevel::Warning && std::strstr(record.message, "were dropped") != nullptr) {
				++*static_cast<int*>(context);
			}
		}
#endif

		struct Game {
			int score = 0;
		};
		struct Other {};

	} // namespace

	// --- Fixed ticks ---

	TEST_F(AppTest, TicksFollowTheClockExactly) {
		start();
		app->scenes().push<SceneA>();
		frame(0.0);
		EXPECT_EQ(count("A.fixedUpdate"), 0);
		frame(kStep);
		EXPECT_EQ(count("A.fixedUpdate"), 1);
		frame(3 * kStep);
		EXPECT_EQ(count("A.fixedUpdate"), 4);
		frame(kStep / 2);
		EXPECT_EQ(count("A.fixedUpdate"), 4);
		frame(kStep / 2);
		EXPECT_EQ(count("A.fixedUpdate"), 5);
		for (int tick = 0; tick < 5; ++tick) {
			EXPECT_EQ(count("A.fixedUpdate " + std::to_string(tick)), 1) << tick;
		}
	}

	TEST_F(AppTest, TheClockStartsAtTheFirstFrameHoweverLongTheLoad) {
		start();
		platform->clock().advance(2.0);
		app->scenes().push<SceneA>();
		frame(2 * kStep);
		EXPECT_EQ(count("A.fixedUpdate"), 0);
		EXPECT_EQ(top<SceneA>().frames.back(), 0.0);
		frame(kStep);
		EXPECT_EQ(count("A.fixedUpdate"), 1);
	}

	TEST_F(AppTest, AFrameLongerThanMaxFrameDeltaCountsAsThatLong) {
		start(config({.fixedStep = kStep, .maxFrameDelta = 0.25, .maxTicksPerFrame = 100}));
		app->scenes().push<SceneA>();
		frame(0.0);
		frame(1.0);
		EXPECT_EQ(count("A.fixedUpdate"), 16);
		ASSERT_FALSE(top<SceneA>().frames.empty());
		EXPECT_EQ(top<SceneA>().frames.back(), 0.25);
	}

	TEST_F(AppTest, PastMaxTicksPerFrameTheWholeStepsLeftAreDropped) {
		start(config({.fixedStep = kStep, .maxTicksPerFrame = 4}));
		app->scenes().push<SceneA>();
		frame(0.0);
		take();

		frame((10 * kStep) + (kStep / 2));
		EXPECT_EQ(count("A.fixedUpdate"), 4);
		EXPECT_EQ(count("A.render 0.5"), 1);

		frame(0.0);
		EXPECT_EQ(count("A.fixedUpdate"), 4);

		frame(kStep / 2);
		EXPECT_EQ(count("A.fixedUpdate"), 5);
		EXPECT_EQ(journal().back(), "A.render 0");
	}

	TEST_F(AppTest, RenderAlphaIsHowFarIntoTheStepTheAccumulatorIs) {
		start();
		app->scenes().push<SceneA>();
		frame(0.0);
		frame(kStep / 4);
		EXPECT_EQ(journal().back(), "A.render 0.25");
		frame(kStep / 2);
		EXPECT_EQ(journal().back(), "A.render 0.75");
		frame(kStep / 2);
		EXPECT_EQ(journal().back(), "A.render 0.25");
		EXPECT_EQ(count("A.fixedUpdate"), 1);
	}

	// --- Pause and step ---

	TEST_F(AppTest, PauseStopsTicksAndUpdateAndRenderGoesOn) {
		start();
		app->scenes().push<SceneA>();
		frame(0.0);
		take();

		app->pause();
		EXPECT_TRUE(app->paused());
		frame(4 * kStep);
		EXPECT_EQ(count("A.fixedUpdate"), 0);
		EXPECT_EQ(count("A.update"), 0);
		EXPECT_EQ(count("A.render"), 1);

		app->resume();
		EXPECT_FALSE(app->paused());
		frame(kStep);
		EXPECT_EQ(count("A.fixedUpdate"), 1); // the paused frame's four steps aren't made up
		EXPECT_EQ(count("A.update"), 1);
	}

	TEST_F(AppTest, SceneChangesWaitForResume) {
		start();
		app->scenes().push<SceneA>();
		frame(0.0);
		app->pause();
		app->scenes().push<SceneB>();
		frame(kStep);
		frame(kStep);
		EXPECT_EQ(count("B.built"), 0);
		EXPECT_EQ(app->scenes().top()->name(), std::string("A"));

		app->resume();
		frame(kStep);
		EXPECT_EQ(count("B.onEnter"), 1);
		EXPECT_EQ(count("B.fixedUpdate"), 1);
	}

	TEST_F(AppTest, WhilePausedDispatchReachesOnlyOverlays) {
		start();
		app->addOverlay(std::make_unique<RecordingOverlay>("O"));
		app->scenes().push<SceneA>();
		frame(0.0);
		take();
		app->pause();
		platform->push(keyDown(Key::Space));
		frame(0.0);
		EXPECT_EQ(count("O.handleEvent"), 1);
		EXPECT_EQ(count("A.handleEvent"), 0);
		EXPECT_EQ(count("O.update"), 0);
	}

	TEST_F(AppTest, APressDuringAPauseDoesNotFireOnResume) {
		start();
		app->scenes().push<SceneA>();
		frame(0.0);
		app->pause();
		platform->push(keyDown(Key::Space));
		frame(kStep);
		app->resume();
		frame(kStep);
		ASSERT_EQ(top<SceneA>().ticks.size(), 1U);
		EXPECT_TRUE(keyIn(top<SceneA>().ticks[0], Key::Space).down);
		EXPECT_FALSE(keyIn(top<SceneA>().ticks[0], Key::Space).pressed());
	}

	TEST_F(AppTest, StepRunsExactlyThatManyTicksWhilePausedWhateverTheClock) {
		start(config({.fixedStep = kStep, .maxTicksPerFrame = 2}));
		app->scenes().push<SceneA>();
		frame(0.0);
		app->pause();
		take();

		app->step(5);
		frame(0.0);
		EXPECT_EQ(count("A.fixedUpdate"), 5);
		EXPECT_EQ(count("A.update"), 0);
		EXPECT_EQ(count("A.render"), 1);

		frame(10.0);
		EXPECT_EQ(count("A.fixedUpdate"), 5);

		app->step(2);
		app->step(1);
		frame(kStep);
		EXPECT_EQ(count("A.fixedUpdate"), 8);
		EXPECT_EQ(count("A.fixedUpdate 7"), 1);
	}

	TEST_F(AppTest, StepWhileNotPausedDoesNothing) {
		start();
		app->scenes().push<SceneA>();
		frame(0.0);
		app->step(3);
		frame(0.0);
		app->pause();
		frame(0.0);
		EXPECT_EQ(count("A.fixedUpdate"), 0);
	}

	TEST_F(AppTest, AResumeInsideAFrameTakesEffectOnceTheFrameIsOver) {
		start();
		app->scenes().push<SceneA>();
		frame(0.0);
		app->pause();
		take();

		bool pausedAfterResume = false;
		app->setDispatch([&](App& dispatching) {
			for (const InputEvent& event : dispatching.input().events()) {
				if (event.type == InputEvent::Type::KeyDown) {
					dispatching.resume();
					pausedAfterResume = dispatching.paused();
					dispatching.scenes().push<SceneB>();
				}
			}
		});
		platform->push(keyDown(Key::Escape));
		frame(kStep);
		EXPECT_TRUE(pausedAfterResume);
		EXPECT_FALSE(app->paused());
		EXPECT_EQ(count("A.fixedUpdate"), 0); // the frame that resumed stayed paused
		EXPECT_EQ(count("A.update"), 0);
		EXPECT_EQ(count("B.built"), 0);

		app->setDispatch({});
		frame(kStep);
		EXPECT_EQ(count("B.fixedUpdate"), 1);
		ASSERT_EQ(top<SceneB>().ticks.size(), 1U);
		EXPECT_TRUE(keyIn(top<SceneB>().ticks[0], Key::Escape).down);
		EXPECT_FALSE(keyIn(top<SceneB>().ticks[0], Key::Escape).pressed()); // the key that resumed fires nothing
	}

	TEST_F(AppTest, AResumeInsideAFrameDropsStepsNotRunYet) {
		start();
		app->scenes().push<SceneA>();
		frame(0.0);
		app->pause();
		app->step(3);
		app->setDispatch([](App& dispatching) {
			if (!dispatching.input().events().empty()) {
				dispatching.resume();
			}
		});
		platform->push(keyDown(Key::Escape));
		take();
		frame(0.0);
		EXPECT_EQ(count("A.fixedUpdate"), 0);
		EXPECT_FALSE(app->paused());

		app->setDispatch({});
		frame(0.0);
		EXPECT_EQ(count("A.fixedUpdate"), 0); // nothing left over for later
	}

	TEST_F(AppTest, AStepAskedForAfterAnInFrameResumeIsDroppedAtTheFramesEnd) {
		start();
		app->scenes().push<SceneA>();
		frame(0.0);
		app->pause();
		app->setDispatch([](App& dispatching) {
			if (!dispatching.input().events().empty()) {
				dispatching.resume();
				dispatching.step(2);
			}
		});
		[[maybe_unused]] int dropped = 0;
#ifdef DEVELOPMENT_BUILD
		foundation::Logger::setSink(countDroppedSteps, &dropped);
#endif
		platform->push(keyDown(Key::Escape));
		take();
		frame(0.0);
#ifdef DEVELOPMENT_BUILD
		foundation::Logger::setSink(nullptr, nullptr);
		EXPECT_EQ(dropped, 1);
#endif
		EXPECT_EQ(count("A.fixedUpdate"), 0);
		EXPECT_FALSE(app->paused());

		app->setDispatch({});
		frame(0.0);
		EXPECT_EQ(count("A.fixedUpdate"), 0);
	}

	TEST_F(AppTest, APauseAndResumeInOneFrameLeaveItPausedForTheFrame) {
		start();
		app->scenes().push<SceneA>();
		frame(0.0);
		hooks()["A"].update = [](App& running, const FrameContext& /*frame*/) {
			running.pause();
			running.resume();
		};
		frame(kStep);
		EXPECT_FALSE(app->paused());
		hooks()["A"].update = [](App& running, const FrameContext& /*frame*/) {
			running.resume();
			running.pause();
		};
		frame(kStep);
		EXPECT_TRUE(app->paused());
	}

	TEST_F(AppTest, APauseFromATickEndsTheFramesTicks) {
		start();
		hooks()["A"].fixedUpdate = [](App& running, const TickContext& tick) {
			if (tick.tick == 1) {
				running.pause();
			}
		};
		app->scenes().push<SceneA>();
		frame(0.0);
		take();
		frame((4 * kStep) + (kStep / 2));
		EXPECT_EQ(count("A.fixedUpdate"), 2);
		EXPECT_EQ(count("A.update"), 0);
		EXPECT_EQ(journal().back(), "A.render 0.5"); // the steps left went, the part of one stayed

		app->resume();
		frame(0.0);
		EXPECT_EQ(count("A.fixedUpdate"), 2); // nothing replayed on resume
		EXPECT_EQ(journal().back(), "A.render 0.5");
	}

	TEST_F(AppTest, ResumeDropsStepsNotRunYet) {
		start();
		app->scenes().push<SceneA>();
		frame(0.0);
		app->pause();
		app->step(3);
		app->resume();
		app->pause();
		frame(0.0);
		EXPECT_EQ(count("A.fixedUpdate"), 0);
	}

	// --- Focus ---

	TEST_F(AppTest, FocusLossFreezesTicksAndUpdateGoesOn) {
		start();
		app->scenes().push<SceneA>();
		frame(0.0);
		take();

		platform->setFocused(false);
		frame(2 * kStep);
		frame(2 * kStep);
		EXPECT_EQ(count("A.fixedUpdate"), 0);
		EXPECT_EQ(count("A.update"), 2);

		platform->setFocused(true);
		frame(kStep);
		EXPECT_EQ(count("A.fixedUpdate"), 1); // no catching up
	}

	TEST_F(AppTest, WithoutTheFreezeTicksRunUnfocused) {
		start(config({.fixedStep = kStep, .freezeTicksOnFocusLoss = false}));
		app->scenes().push<SceneA>();
		frame(0.0);
		platform->setFocused(false);
		frame(2 * kStep);
		EXPECT_EQ(count("A.fixedUpdate"), 2);
	}

	// --- Input ---

	TEST_F(AppTest, APushedEventReachesDispatchAndExactlyOneTick) {
		start();
		app->scenes().push<SceneA>();
		frame(0.0);

		platform->clock().advance(kStep);
		platform->push(keyDown(Key::A));
		platform->clock().advance(kStep);
		app->tick();

		SceneA& scene = top<SceneA>();
		ASSERT_EQ(scene.events.size(), 1U);
		EXPECT_EQ(scene.events[0].type, InputEvent::Type::KeyDown);
		EXPECT_EQ(scene.events[0].time, kStep);
		ASSERT_EQ(app->input().events().size(), 1U);
		EXPECT_TRUE(keyIn(app->input().frame(), Key::A).pressed());

		ASSERT_EQ(scene.ticks.size(), 2U);
		EXPECT_TRUE(keyIn(scene.ticks[0], Key::A).pressed());
		EXPECT_FALSE(keyIn(scene.ticks[1], Key::A).pressed());
		EXPECT_TRUE(keyIn(scene.ticks[1], Key::A).down);

		platform->push(keyUp(Key::A));
		frame(0.0);
		EXPECT_TRUE(app->input().events().size() == 1U && keyIn(app->input().frame(), Key::A).released());
		frame(kStep);
		ASSERT_EQ(scene.ticks.size(), 3U);
		EXPECT_TRUE(keyIn(scene.ticks[2], Key::A).released());
		EXPECT_TRUE(app->input().events().empty());
	}

	TEST_F(AppTest, APressInAFrameWithNoTickWaitsForTheNextTick) {
		start();
		app->scenes().push<SceneA>();
		frame(0.0);
		platform->push(keyDown(Key::A));
		frame(kStep / 2);
		EXPECT_TRUE(top<SceneA>().ticks.empty());
		frame(kStep / 2);
		ASSERT_EQ(top<SceneA>().ticks.size(), 1U);
		EXPECT_TRUE(keyIn(top<SceneA>().ticks[0], Key::A).pressed());
	}

	TEST_F(AppTest, APressThatChangesTheTopDoesNotReachTheNewTopsFirstTick) {
		start();
		hooks()["A"].handleEvent = [](App& running, const InputEvent& event) {
			if (event.key == Key::Space) {
				running.scenes().push<SceneB>();
			}
			return true;
		};
		hooks()["B"].handleEvent = [](App& running, const InputEvent& event) {
			if (event.key == Key::Escape) {
				running.scenes().pop();
			}
			return true;
		};
		app->scenes().push<SceneA>();
		frame(0.0);

		platform->push(keyDown(Key::Space));
		frame(kStep);
		ASSERT_EQ(top<SceneB>().ticks.size(), 1U);
		EXPECT_TRUE(keyIn(top<SceneB>().ticks[0], Key::Space).down);
		EXPECT_FALSE(keyIn(top<SceneB>().ticks[0], Key::Space).pressed());

		platform->push(keyDown(Key::Escape));
		frame(kStep);
		ASSERT_EQ(top<SceneA>().ticks.size(), 1U); // A's first tick since it was uncovered
		EXPECT_FALSE(keyIn(top<SceneA>().ticks[0], Key::Escape).pressed());

		platform->push(keyDown(Key::A));
		frame(kStep);
		ASSERT_EQ(top<SceneA>().ticks.size(), 2U); // with no change of top, a press reaches the tick
		EXPECT_TRUE(keyIn(top<SceneA>().ticks[1], Key::A).pressed());
	}

	TEST_F(AppTest, EventsGoToOverlaysLastAddedFirstThenTheTopScene) {
		start();
		app->addOverlay(std::make_unique<RecordingOverlay>("O1"));
		app->addOverlay(std::make_unique<RecordingOverlay>("O2"));
		app->scenes().push<SceneA>();
		frame(0.0);
		take();
		platform->push(keyDown(Key::A));
		frame(0.0);
		const std::vector<std::string> expected{"O2.handleEvent", "O1.handleEvent", "A.handleEvent"};
		EXPECT_EQ(head(3), expected);
	}

	TEST_F(AppTest, AConsumingOverlayKeepsTheEventFromWhatIsUnderIt) {
		start();
		app->addOverlay(std::make_unique<RecordingOverlay>("Under"));
		app->addOverlay(std::make_unique<RecordingOverlay>("Menu", true));
		app->scenes().push<SceneA>();
		frame(0.0);
		platform->push(keyDown(Key::A));
		frame(0.0);
		EXPECT_EQ(count("Menu.handleEvent"), 1);
		EXPECT_EQ(count("Under.handleEvent"), 0);
		EXPECT_EQ(count("A.handleEvent"), 0);
	}

	TEST_F(AppTest, SetDispatchReplacesTheDefaultEvenWhilePaused) {
		start();
		app->addOverlay(std::make_unique<RecordingOverlay>("O"));
		app->scenes().push<SceneA>();
		frame(0.0);

		int							  calls = 0;
		std::vector<InputEvent::Type> seen;
		app->setDispatch([&](App& dispatching) {
			++calls;
			for (const InputEvent& event : dispatching.input().events()) {
				seen.push_back(event.type);
			}
		});
		platform->push(keyDown(Key::A));
		frame(0.0);
		EXPECT_EQ(calls, 1);
		EXPECT_EQ(seen, std::vector<InputEvent::Type>{InputEvent::Type::KeyDown});
		EXPECT_EQ(count("O.handleEvent"), 0);
		EXPECT_EQ(count("A.handleEvent"), 0);

		app->pause();
		frame(0.0);
		EXPECT_EQ(calls, 2);

		app->resume();
		app->setDispatch({});
		platform->push(keyDown(Key::B));
		frame(0.0);
		EXPECT_EQ(calls, 2);
		EXPECT_EQ(count("A.handleEvent"), 1);
	}

	// --- Exit, close and shutdown ---

	TEST_F(AppTest, RequestExitFinishesTheFrameAndTickIsFalseFromThen) {
		start();
		hooks()["A"].update = [](App& running, const FrameContext& /*frame*/) {
			running.requestExit();
		};
		app->scenes().push<SceneA>();
		EXPECT_FALSE(frame(0.0));
		EXPECT_EQ(count("A.render"), 1);
		EXPECT_FALSE(frame(kStep));
		EXPECT_EQ(count("A.update"), 1);
		EXPECT_EQ(count("A.render"), 1);
	}

	// Step 2's onResize comes before the dev server's step 3, whose exit alone ends a frame early.
	TEST_F(AppTest, RequestExitFromAnOnResizeFinishesTheFrame) {
		class ExitingOverlay final : public Overlay {
		  public:
			explicit ExitingOverlay(App& owner)
				: running(owner) {}
			void render() override {}
			void onResize(const Renderer::Viewport& /*viewport*/) override { running.requestExit(); }

		  private:
			App& running;
		};
		start();
		app->addOverlay(std::make_unique<ExitingOverlay>(*app));
		app->scenes().push<SceneA>();
		frame();
		take();
		platform->resize(640, 480);
		EXPECT_FALSE(frame());
		EXPECT_EQ(count("A.onResize"), 1);
		EXPECT_EQ(count("A.update"), 1);
		EXPECT_EQ(count("A.render"), 1);
	}

	TEST_F(AppTest, RequestExitDuringTheFirstOnEnterFinishesThatFrame) {
		start();
		hooks()["A"].onEnter = [](App& running) {
			running.requestExit();
		};
		app->scenes().push<SceneA>();
		EXPECT_FALSE(frame(kStep));
		EXPECT_EQ(count("A.update"), 1);
		EXPECT_EQ(count("A.render"), 1);
		EXPECT_EQ(count("A.onExit"), 0);
		app->shutdown();
		EXPECT_EQ(count("A.onExit"), 1);
	}

	TEST(AppWindowTest, CloseFinishesTheFrameAndTickIsFalseFromThen) {
		journal().clear();
		auto				 owned = std::make_unique<ClosablePlatform>();
		ClosablePlatform&	 window = *owned;
		std::unique_ptr<App> app = App::create({}, std::move(owned));
		ASSERT_NE(app, nullptr);
		app->scenes().push<SceneA>();
		EXPECT_TRUE(app->tick());
		EXPECT_EQ(window.presents, 1);

		window.closeOnNextPoll = true;
		EXPECT_FALSE(app->tick());
		EXPECT_EQ(window.presents, 2);
		EXPECT_EQ(count("A.render"), 2);

		EXPECT_FALSE(app->tick());
		EXPECT_EQ(window.presents, 2);

		app->setVsync(false);
		EXPECT_FALSE(window.vsync);
		app.reset();
		journal().clear();
	}

	TEST(AppWindowTest, AThrowOutOfAFrameLeavesTheAppAbleToShutDown) {
		journal().clear();
		auto				 owned = std::make_unique<ClosablePlatform>();
		ClosablePlatform&	 window = *owned;
		std::unique_ptr<App> app = App::create({}, std::move(owned));
		ASSERT_NE(app, nullptr);
		app->scenes().push<SceneA>();
		EXPECT_TRUE(app->tick());

		window.throwOnPoll = true;
		EXPECT_THROW(app->tick(), std::runtime_error);
		app->shutdown();
		EXPECT_EQ(count("A.onExit"), 1);
		app.reset();
		journal().clear();
	}

	TEST_F(AppTest, ATickOrRunFromInsideAFrameIsRefused) {
		start();
		bool innerTick = true;
		hooks()["A"].update = [&](App& running, const FrameContext& /*frame*/) {
			innerTick = running.tick();
			running.run();
		};
		app->scenes().push<SceneA>();
		EXPECT_TRUE(frame(0.0));
		EXPECT_FALSE(innerTick);
		EXPECT_EQ(count("A.update"), 1);
		EXPECT_EQ(count("A.render"), 1);
		hooks()["A"].update = nullptr;
		EXPECT_TRUE(frame(0.0)); // and the outer frame left the App as it should
		EXPECT_EQ(count("A.render"), 2);
	}

	TEST_F(AppTest, RunWithAFrameCapDoesNotWaitOnAClockThatDoesNotRunInRealTime) {
		start(config({.fixedStep = kStep, .frameCapHz = 120}));
		hooks()["A"].update = [](App& running, const FrameContext& /*frame*/) {
			if (count("A.update") == 3) {
				running.requestExit();
			}
		};
		app->scenes().push<SceneA>();
		app->run();
		EXPECT_EQ(count("A.render"), 3);

		// A clock that moves only when a scene moves it stands still through pace()'s sleep.
		app.reset();
		journal().clear();
		start(config({.fixedStep = kStep, .frameCapHz = 120}));
		hooks()["A"].update = [this](App& running, const FrameContext& /*frame*/) {
			platform->clock().advance(kStep / 4);
			if (count("A.update") == 3) {
				running.requestExit();
			}
		};
		app->scenes().push<SceneA>();
		app->run();
		EXPECT_EQ(count("A.render"), 3);
	}

	TEST_F(AppTest, RunTicksUntilExit) {
		start();
		hooks()["A"].update = [](App& running, const FrameContext& /*frame*/) {
			if (count("A.update") == 3) {
				running.requestExit();
			}
		};
		app->scenes().push<SceneA>();
		app->run();
		EXPECT_EQ(count("A.update"), 3);
		EXPECT_EQ(count("A.render"), 3);
	}

	TEST_F(AppTest, ShutdownExitsScenesTopDownThenOverlaysLastAddedFirst) {
		start();
		app->addOverlay(std::make_unique<RecordingOverlay>("O1"));
		app->addOverlay(std::make_unique<RecordingOverlay>("O2"));
		app->scenes().push<SceneA>();
		app->scenes().push<SceneB>();
		frame(0.0);
		take();

		app->shutdown();
		const std::vector<std::string> expected{
			"B.onExit", "B.destroyed", "A.onExit", "A.destroyed", "O2.onExit", "O2.destroyed", "O1.onExit", "O1.destroyed"
		};
		EXPECT_EQ(take(), expected);
		EXPECT_EQ(app->scenes().top(), nullptr);
		EXPECT_FALSE(frame(kStep));
		EXPECT_TRUE(journal().empty());
	}

	TEST_F(AppTest, ShutdownQueuedChangesNeverApply) {
		start();
		app->scenes().push<SceneA>();
		app->shutdown();
		EXPECT_EQ(count("A.built"), 0); // scenes are built when their change applies
		EXPECT_FALSE(frame(kStep));
		EXPECT_EQ(count("A.built"), 0);
	}

	TEST_F(AppTest, ShutdownInsideAFrameAsksForExitInstead) {
		start();
		hooks()["A"].update = [](App& running, const FrameContext& /*frame*/) {
			running.shutdown();
		};
		app->scenes().push<SceneA>();
		EXPECT_FALSE(frame(0.0));
		EXPECT_EQ(count("A.onExit"), 0);
		EXPECT_EQ(count("A.render"), 1);
		app->shutdown();
		EXPECT_EQ(count("A.onExit"), 1);
	}

	// --- Resize ---

	TEST_F(AppTest, ResizeReachesOverlaysThenEveryStackedSceneOnce) {
		start();
		app->addOverlay(std::make_unique<RecordingOverlay>("O1"));
		app->addOverlay(std::make_unique<RecordingOverlay>("O2"));
		app->scenes().push<SceneA>();
		app->scenes().push<SceneB>();
		frame(0.0);
		take();

		platform->resize(1000, 700);
		platform->resize(1024, 768);
		EXPECT_EQ(app->viewport().logicalWidth, 800);
		frame(0.0);
		const std::vector<std::string> expected{
			"O2.onResize 1024x768", "O1.onResize 1024x768", "B.onResize 1024x768", "A.onResize 1024x768"
		};
		EXPECT_EQ(head(4), expected);
		EXPECT_EQ(count("A.onResize"), 1);

		EXPECT_EQ(app->viewport().logicalWidth, 1024);
		EXPECT_EQ(app->viewport().framebufferHeight, 768);
		EXPECT_EQ(app->coordinateSystem().getWindowSize(), glm::vec2(1024.0F, 768.0F));
		int width = 0;
		int height = 0;
		Renderer::Primitives::getViewport(width, height);
		EXPECT_EQ(width, 1024);
		EXPECT_EQ(height, 768);
	}

	TEST_F(AppTest, AMinimizedWindowsEmptyFramebufferResizesNothing) {
		start();
		app->addOverlay(std::make_unique<RecordingOverlay>("O"));
		app->scenes().push<SceneA>();
		frame(0.0);
		take();

		platform->resize(0, 0);
		frame(0.0);
		EXPECT_EQ(count("O.onResize"), 0);
		EXPECT_EQ(count("A.onResize"), 0);
		EXPECT_EQ(app->viewport().logicalWidth, 800);
		EXPECT_EQ(app->viewport().framebufferHeight, 600);

		platform->resize(640, 480);
		frame(0.0);
		EXPECT_EQ(count("O.onResize 640x480"), 1);
		EXPECT_EQ(count("A.onResize 640x480"), 1);
	}

	// --- Overlays ---

	TEST_F(AppTest, AnOverlayAddedDuringAFrameJoinsOnceItIsOver) {
		start();
		hooks()["A"].update = [](App& running, const FrameContext& /*frame*/) {
			if (count("A.update") == 1) {
				running.addOverlay(std::make_unique<RecordingOverlay>("Late"));
			}
		};
		app->scenes().push<SceneA>();
		frame(0.0);
		EXPECT_EQ(count("Late."), 0);
		frame(0.0);
		EXPECT_EQ(count("Late.update"), 1);
		EXPECT_EQ(count("Late.render"), 1);
	}

	TEST_F(AppTest, OverlaysUpdateAfterTheTopSceneAndRenderAboveTheStack) {
		start();
		app->addOverlay(std::make_unique<RecordingOverlay>("O1"));
		app->addOverlay(std::make_unique<RecordingOverlay>("O2"));
		app->scenes().push<SceneA>();
		frame(0.0);
		const std::vector<std::string> expected{"A.update", "O1.update", "O2.update", "A.render 0", "O1.render", "O2.render"};
		EXPECT_EQ(tail(6), expected);
	}

	// --- Exceptions ---

	TEST_F(AppTest, WhatASceneThrowsIsLoggedAndTheFrameGoesOn) {
		start();
		Hooks& hook = hooks()["A"];
		hook.onEnter = [](App& /*app*/) {
			throw std::runtime_error("onEnter");
		};
		hook.handleEvent = [](App& /*app*/, const InputEvent& /*event*/) -> bool {
			throw std::runtime_error("handleEvent");
		};
		hook.fixedUpdate = [](App& /*app*/, const TickContext& /*tick*/) {
			throw 7;
		};
		hook.update = [](App& /*app*/, const FrameContext& /*frame*/) {
			throw std::runtime_error("update");
		};
		hook.render = [](App& /*app*/) {
			throw std::runtime_error("render");
		};
		app->scenes().push<SceneA>();
		EXPECT_TRUE(frame(kStep));
		platform->push(keyDown(Key::A));
		EXPECT_TRUE(frame(kStep));
		EXPECT_EQ(count("A.handleEvent"), 1);
		EXPECT_EQ(count("A.render"), 2);
		EXPECT_EQ(count("A.fixedUpdate"), 1);
		EXPECT_EQ(app->scenes().top()->name(), std::string("A"));
	}

	// --- Metrics ---

	TEST_F(AppTest, MetricsTimeTheFramesSectionsThroughTheClock) {
		AppConfig withMetrics = config();
		withMetrics.metrics = true;
		start(std::move(withMetrics));
		ASSERT_NE(app->metrics(), nullptr);
		Hooks& hook = hooks()["A"];
		hook.handleEvent = [this](App& /*app*/, const InputEvent& /*event*/) {
			platform->clock().advance(0.001);
			return false;
		};
		hook.update = [this](App& /*app*/, const FrameContext& /*frame*/) {
			platform->clock().advance(0.004);
		};
		hook.render = [this](App& /*app*/) {
			platform->clock().advance(0.002);
		};
		app->scenes().push<SceneA>();
		frame(0.0);

		platform->push(keyDown(Key::A));
		frame(0.0);
		frame(0.008); // pacing between frames is in no section

		const Foundation::PerformanceMetrics metrics = app->metrics()->getCurrentMetrics();
		EXPECT_FLOAT_EQ(metrics.pollEventsMs, 0.0F);
		EXPECT_FLOAT_EQ(metrics.inputHandleMs, 1.0F);
		EXPECT_FLOAT_EQ(metrics.sceneUpdateMs, 4.0F);
		EXPECT_FLOAT_EQ(metrics.sceneRenderMs, 2.0F);
		EXPECT_FLOAT_EQ(metrics.swapBuffersMs, 0.0F);
	}

	TEST(AppWindowTest, ThePresentSectionTimesPresentAlone) {
		auto			  owned = std::make_unique<ClosablePlatform>();
		ClosablePlatform& window = *owned;
		window.presentSeconds = 0.003;
		std::unique_ptr<App> app = App::create({.metrics = true}, std::move(owned));
		ASSERT_NE(app, nullptr);
		EXPECT_TRUE(app->tick());
		window.time.advance(0.008);
		EXPECT_TRUE(app->tick());
		EXPECT_FLOAT_EQ(app->metrics()->getCurrentMetrics().swapBuffersMs, 3.0F);
	}

	TEST_F(AppTest, NoMetricsUnlessAsked) {
		start();
		EXPECT_EQ(app->metrics(), nullptr);
	}

	// --- Fonts and resources ---

	TEST_F(AppTest, NoFontAtlasesIsNoTextAndNotAFailure) {
		start();
		EXPECT_EQ(Renderer::Primitives::getFontRenderer(), nullptr);
	}

	TEST_F(AppTest, FontAtlasesWithoutTheDefaultFamilyFailCreate) {
		constexpr Renderer::FontFamily				   kBody{0};
		const std::array<Renderer::FontAtlasSource, 1> atlases{
			{{.family = kBody, .pngPath = "missing-atlas.png", .jsonPath = "missing-atlas.json"}}
		};
		AppConfig withFonts = config();
		withFonts.fontAtlases = atlases;
		withFonts.defaultFont = kBody;
		EXPECT_EQ(App::create(std::move(withFonts), std::make_unique<HeadlessPlatform>(800, 600)), nullptr);
		EXPECT_NE(App::create(config(), std::make_unique<HeadlessPlatform>(800, 600)), nullptr);
	}

	TEST_F(AppTest, ResourceRootsComeFromTheConfig) {
		const std::filesystem::path root =
			std::filesystem::temp_directory_path() /
			("salal-app-root-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
		std::filesystem::create_directories(root);
		std::ofstream(root / "probe.txt") << "probe";

		AppConfig withRoots = config();
		withRoots.resourceRoots = {root};
		start(std::move(withRoots));
		EXPECT_EQ(Foundation::findResource("probe.txt"), root / "probe.txt");
		app.reset();
		EXPECT_EQ(Foundation::findResource("probe.txt"), std::nullopt); // ~App clears them

		constexpr Renderer::FontFamily				   kBody{0};
		const std::array<Renderer::FontAtlasSource, 1> atlases{
			{{.family = kBody, .pngPath = "missing-atlas.png", .jsonPath = "missing-atlas.json"}}
		};
		AppConfig failing = config();
		failing.resourceRoots = {root};
		failing.fontAtlases = atlases;
		failing.defaultFont = kBody;
		EXPECT_EQ(App::create(std::move(failing), std::make_unique<HeadlessPlatform>(800, 600)), nullptr);
		EXPECT_EQ(Foundation::findResource("probe.txt"), std::nullopt); // and so does a create that fails

		std::error_code ignored;
		std::filesystem::remove_all(root, ignored);
	}

	// --- What create refuses ---

	TEST_F(AppTest, OneAppAtATime) {
		start();
		EXPECT_EQ(App::create(config(), std::make_unique<HeadlessPlatform>(800, 600)), nullptr);
		app.reset();
		EXPECT_NE(App::create(config(), std::make_unique<HeadlessPlatform>(800, 600)), nullptr);
	}

	TEST_F(AppTest, CreateRefusesNoPlatformAndABadFrame) {
		EXPECT_EQ(App::create(config(), nullptr), nullptr);
		for (const FrameConfig& bad : {
				 FrameConfig{.fixedStep = 0.0},
				 FrameConfig{.fixedStep = -kStep},
				 FrameConfig{.maxFrameDelta = 0.0},
				 FrameConfig{.maxTicksPerFrame = 0},
				 FrameConfig{.frameCapHz = -1},
			 }) {
			EXPECT_EQ(App::create(config(bad), std::make_unique<HeadlessPlatform>(800, 600)), nullptr);
		}
	}

	// --- The context ---

	TEST_F(AppTest, TheContextIsTheObjectSetAndScenesReachIt) {
		start();
		Game game;
		app->setContext(game);
		EXPECT_EQ(&app->context<Game>(), &game);
		EXPECT_EQ(&app->context<const Game>(), &game);
		hooks()["A"].onEnter = [](App& running) {
			running.context<Game>().score = 7;
		};
		app->scenes().push<SceneA>();
		frame(0.0);
		EXPECT_EQ(game.score, 7);
	}

	using AppDeathTest = AppTest;

	TEST_F(AppDeathTest, ContextOfAnotherTypeAborts) {
		start();
		Game game;
		app->setContext(game);
		EXPECT_DEATH(app->context<Other>(), "a type other than the one setContext was given");
	}

	TEST_F(AppDeathTest, ContextBeforeSetContextAborts) {
		start();
		EXPECT_DEATH(app->context<Game>(), "no context set");
	}

	TEST_F(AppDeathTest, SetContextTwiceAborts) {
		start();
		Game first;
		Game second;
		app->setContext(first);
		EXPECT_DEATH(app->setContext(second), "setContext called twice");
	}

} // namespace app::testing
