// The clock, the animator and timers on a manual clock: curve values, interpolation, defaults from
// the theme, creation order, reduced motion, retargeting and reversal, handles, owners removed
// mid-tween, timers, requestUpdate, settle, and a state colour easing between states.

#include "ui/Context.h"
#include "ui/shapes/Rectangle.h"
#include "ui/theme/StateColor.h"
#include "ui/theme/Tokens.h"

#include <gtest/gtest.h>

#include <cmath>
#include <string>
#include <vector>

namespace ui {

	namespace {

		const Easing kLinear;

		class UiAnimatorTest : public ::testing::Test {
		  protected:
			void frame(double milliseconds) {
				clock.advance(milliseconds);
				animator.tick();
			}

			Tween<float> linear(float from, float to, float durationMs, float& value, int* completions = nullptr) {
				return animator.tween<float>({
					.from = from,
					.to = to,
					.durationMs = durationMs,
					.easing = kLinear,
					.onUpdate = [&value](const float& next) { value = next; },
					.onComplete =
						[completions] {
							if (completions != nullptr) {
								++*completions;
							}
						},
				});
			}

			Clock	 clock;
			Animator animator{clock};
		};

		// Counts its update calls and asks for the next one while it should.
		class Ticker : public Component {
		  public:
			int	 updates = 0;
			bool keepGoing = false;

		  protected:
			void update(float /*dt*/) override {
				++updates;
				if (keepGoing) {
					requestUpdate();
				}
			}
		};

	} // namespace

	// --- Curves ---

	TEST_F(UiAnimatorTest, ACubicBezierMatchesCssAndPinsItsEnds) {
		const Easing standard(0.4F, 0.0F, 0.2F, 1.0F);
		// Reference values from a 100-round bisection solve of x(t).
		EXPECT_NEAR(standard(0.25F), 0.236587F, 1e-5F);
		EXPECT_NEAR(standard(0.5F), 0.775561F, 1e-5F);
		EXPECT_NEAR(standard(0.75F), 0.959368F, 1e-5F);
		EXPECT_EQ(standard(0.0F), 0.0F);
		EXPECT_EQ(standard(1.0F), 1.0F);
		EXPECT_EQ(standard(-0.5F), 0.0F);
		EXPECT_EQ(standard(1.5F), 1.0F);
		EXPECT_FLOAT_EQ(Easing(0.25F, 0.25F, 0.75F, 0.75F)(0.3F), 0.3F);
		EXPECT_TRUE(std::isfinite(Easing(-1.0F, 0.0F, 2.0F, 1.0F)(0.5F)));
	}

	// --- Tweens ---

	TEST_F(UiAnimatorTest, ATweenAppliesFromAtOnceInterpolatesAndEndsOnTo) {
		float		 value = -1.0F;
		int			 completions = 0;
		Tween<float> tween = linear(0.0F, 100.0F, 200.0F, value, &completions);
		EXPECT_EQ(value, 0.0F);
		frame(50.0);
		EXPECT_FLOAT_EQ(value, 25.0F);
		clock.setFrozen(true);
		frame(100.0);
		EXPECT_FLOAT_EQ(value, 25.0F); // a frozen clock holds it
		clock.setFrozen(false);
		frame(150.0);
		EXPECT_EQ(value, 100.0F);
		EXPECT_EQ(completions, 1);
		EXPECT_FALSE(tween.running());
		frame(50.0);
		EXPECT_EQ(completions, 1);
	}

	TEST_F(UiAnimatorTest, ATweenWithNoDurationOrEasingTakesTheThemesMotion) {
		Context		 context({800.0F, 600.0F});
		const Theme& theme = context.getTheme();
		const float	 normal = theme.get(tokens::motion::duration::normal);
		const Easing standard(theme.get(tokens::motion::easing::standard));
		float		 value = 0.0F;
		Tween<float> tween =
			context.getAnimator().tween<float>({.from = 0.0F, .to = 1.0F, .onUpdate = [&](const float& next) { value = next; }});
		context.update(normal * 0.5 / 1000.0);
		EXPECT_FLOAT_EQ(value, standard(0.5F));
		context.update(normal * 0.5 / 1000.0);
		EXPECT_EQ(value, 1.0F);
	}

	TEST_F(UiAnimatorTest, TweensTickInCreationOrderAndOneStartedInATickWaitsForTheNext) {
		std::vector<std::string> log;
		Tween<float>			 late;
		const auto				 make = [&](const char* name, std::function<void()> onFirst) {
			  return animator.tween<float>({
							  .from = 0.0F,
							  .to = 1.0F,
							  .durationMs = 100.0F,
							  .onUpdate = [&log, name, onFirst, seen = 0](const float& /*value*/) mutable {
					  if (seen++ == 1 && onFirst) {
						  onFirst();
					  }
					  log.push_back(name);
				  },
			  });
		};
		Tween<float> first = make("first", [&] { late = make("late", nullptr); });
		Tween<float> second = make("second", nullptr);
		log.clear();
		frame(10.0);
		EXPECT_EQ(log, (std::vector<std::string>{"late", "first", "second"})); // late's own start, then the tick
		log.clear();
		frame(10.0);
		EXPECT_EQ(log, (std::vector<std::string>{"first", "second", "late"}));
	}

	TEST_F(UiAnimatorTest, ReducedMotionCompletesATweenOnItsFirstTickWithNoTimePassing) {
		float		 value = 0.0F;
		int			 completions = 0;
		Tween<float> running = linear(0.0F, 100.0F, 200.0F, value, &completions);
		frame(50.0);
		animator.setReducedMotion(true);
		frame(0.0);
		EXPECT_EQ(value, 100.0F);
		float		 other = 0.0F;
		Tween<float> fresh = linear(0.0F, 10.0F, 500.0F, other, &completions);
		frame(0.0);
		EXPECT_EQ(other, 10.0F);
		EXPECT_EQ(completions, 2);
	}

	// --- Retargeting ---

	TEST_F(UiAnimatorTest, AReversalTakesTheTimeTheWayBackTakesAndShorteningCompounds) {
		float		 value = 0.0F;
		Tween<float> tween = linear(0.0F, 100.0F, 200.0F, value);
		frame(50.0);
		tween.retarget(0.0F); // a quarter of the way out, so a quarter of the duration back
		frame(25.0);
		EXPECT_FLOAT_EQ(value, 12.5F);
		tween.retarget(100.0F); // CSS's factor: 0.5 * 0.25 + (1 - 0.25) = 0.875 of 200 ms
		frame(87.5);
		EXPECT_NEAR(value, 56.25F, 1e-4F);
		frame(87.5);
		EXPECT_EQ(value, 100.0F);
		EXPECT_FALSE(tween.running());
	}

	TEST_F(UiAnimatorTest, RetargetingToWhereItIsGoingIsQuietAndRestartsAFinishedTween) {
		float		 value = 0.0F;
		int			 completions = 0;
		Tween<float> tween = linear(0.0F, 100.0F, 200.0F, value, &completions);
		for (int i = 0; i < 4; ++i) {
			tween.retarget(100.0F); // every frame, as a component following its state does
			frame(50.0);
		}
		EXPECT_EQ(value, 100.0F);
		EXPECT_EQ(completions, 1);
		tween.retarget(100.0F);
		EXPECT_FALSE(tween.running());
		tween.retarget(0.0F); // from the current value over the full duration
		frame(100.0);
		EXPECT_FLOAT_EQ(value, 50.0F);
	}

	// --- Handles and owners ---

	TEST_F(UiAnimatorTest, DroppingAHandleCancelsItsTweenAndADetachedOneRunsOut) {
		float value = 0.0F;
		int	  completions = 0;
		{
			Tween<float> dropped = linear(0.0F, 100.0F, 200.0F, value, &completions);
			frame(50.0);
		}
		frame(200.0);
		EXPECT_FLOAT_EQ(value, 25.0F);
		linear(0.0F, 10.0F, 100.0F, value, &completions).detach();
		frame(100.0);
		EXPECT_EQ(value, 10.0F);
		EXPECT_EQ(completions, 1);
		EXPECT_EQ(animator.activeTweens(), 0);
	}

	TEST_F(UiAnimatorTest, RemovingAnOwnerCancelsItsTweensAndDropsItsTimers) {
		Context				 context({800.0F, 600.0F});
		const Ref<Rectangle> box = context.root().addChild<Rectangle>({.size = {10.0F, 10.0F}});
		float				 value = 0.0F;
		int					 events = 0;
		Tween<float>		 fade = box.get()->tween<float>({
					.from = 0.0F,
					.to = 1.0F,
					.durationMs = 100.0F,
					.onUpdate = [&](const float& next) { value = next; },
					.onComplete = [&] { ++events; },
		});
		box.get()->after(50.0, [&] { ++events; });
		context.update(0.025);
		const float before = value;
		context.root().get()->removeChild(box);
		context.update(0.2);
		EXPECT_EQ(value, before);
		EXPECT_EQ(events, 0);
		fade.retarget(0.0F);
		EXPECT_FALSE(fade.running());
		EXPECT_EQ(context.getAnimator().activeTimers(), 0);
	}

	// --- Timers ---

	TEST_F(UiAnimatorTest, TimersFireInDueOrderRepeatAndCanCancelThemselves) {
		std::vector<std::string> log;
		animator.after(30.0, [&] { log.push_back("late"); });
		animator.after(10.0, [&] { log.push_back("early"); });
		TimerId ticking;
		int		ticks = 0;
		ticking = animator.every(20.0, [&] {
			log.push_back("tick");
			if (++ticks == 2) {
				animator.cancel(ticking);
			}
		});
		const TimerId cancelled = animator.after(5.0, [&] { log.push_back("cancelled"); });
		animator.cancel(cancelled);
		frame(35.0); // early, tick and late are all due: in due order
		frame(5.0);	 // tick again at 40, and it cancels itself
		frame(20.0);
		frame(100.0);
		EXPECT_EQ(log, (std::vector<std::string>{"early", "tick", "late", "tick"}));
		EXPECT_EQ(animator.activeTimers(), 0);
	}

	// --- Updates and settling ---

	TEST_F(UiAnimatorTest, RequestUpdateIsOneShotAndSkipsAHiddenComponent) {
		Context			  context({800.0F, 600.0F});
		const Ref<Ticker> ticker = context.root().addChild<Ticker>({});
		context.update(0.016);
		EXPECT_EQ(ticker.get()->updates, 0);
		ticker.get()->requestUpdate();
		ticker.get()->requestUpdate();
		context.update(0.016);
		context.update(0.016);
		EXPECT_EQ(ticker.get()->updates, 1);
		ticker.get()->keepGoing = true;
		ticker.get()->requestUpdate();
		context.update(0.016);
		context.update(0.016);
		EXPECT_EQ(ticker.get()->updates, 3);
		context.root().get()->setVisible(false);
		context.update(0.016);
		context.update(0.016);
		EXPECT_EQ(ticker.get()->updates, 3);
	}

	TEST_F(UiAnimatorTest, SettleRunsEveryTweenToItsEndChainsIncluded) {
		float		 value = 0.0F;
		Tween<float> second;
		Tween<float> first = animator.tween<float>({
			.from = 0.0F,
			.to = 1.0F,
			.durationMs = 1000.0F,
			.onComplete = [&] { second = linear(1.0F, 5.0F, 1000.0F, value); },
		});
		EXPECT_EQ(animator.settle(), 2);
		EXPECT_EQ(value, 5.0F);
		EXPECT_EQ(animator.activeTweens(), 0);
	}

	// --- State transitions ---

	TEST_F(UiAnimatorTest, AStateColorEasesBetweenStatesOverTheFastDuration) {
		Context					context({800.0F, 600.0F});
		const Ref<Rectangle>	box = context.root().addChild<Rectangle>({.size = {10.0F, 10.0F}});
		const Theme&			theme = context.getTheme();
		const Foundation::Color base(0.2F, 0.2F, 0.2F, 1.0F);
		StateColor				color;
		color.attach(*box.get(), base);
		context.update(0.0);
		const Foundation::Color hovered = applyStateLayers(theme, base, {.hovered = true});
		color.setState({.hovered = true});
		const double fast = theme.get(tokens::motion::duration::fast) / 1000.0;
		context.update(fast * 0.5);
		EXPECT_GT(color.value().r, base.r);
		EXPECT_LT(color.value().r, hovered.r);
		context.update(fast * 0.5);
		EXPECT_FLOAT_EQ(color.value().r, hovered.r);

		context.setReducedMotion(true);
		color.setState({});
		context.update(0.0);
		EXPECT_FLOAT_EQ(color.value().r, base.r);
	}

} // namespace ui
