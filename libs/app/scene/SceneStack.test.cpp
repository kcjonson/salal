// The scene stack through a running App: push, pop, finish with a move-only result, replace, reset,
// switching by name and reload, change order within a frame, covered scenes and their alpha, and
// what the stack does where the design leaves it open.

#include "scene/SceneStack.h"
#include "tests/AppHarness.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace app::testing {

	namespace {

		inline constexpr char kA[] = "A";
		inline constexpr char kB[] = "B";
		inline constexpr char kC[] = "C";
		inline constexpr char kD[] = "D";
		inline constexpr char kPicker[] = "Picker";
		inline constexpr char kArena[] = "Arena";
		inline constexpr char kMenu[] = "Menu";

		using SceneA = Recording<kA>;
		using SceneB = Recording<kB>;
		using SceneC = Recording<kC, NoArgs, NoResult, false>;
		using SceneD = Recording<kD, NoArgs, NoResult, false>;
		using Picker = Recording<kPicker, NoArgs, std::unique_ptr<int>>;
		using PauseMenu = Recording<kMenu, NoArgs, NoResult, false>;

		struct Loadout {
			std::unique_ptr<int> ships;
		};
		using Arena = Recording<kArena, Loadout>;

		// What a frame did, less its updates and renders, which most tests here don't look at.
		std::vector<std::string> changes() {
			std::vector<std::string> kept;
			for (std::string& entry : take()) {
				if (entry.find(".update") == std::string::npos && entry.find(".render") == std::string::npos &&
					entry.find(".fixedUpdate") == std::string::npos) {
					kept.push_back(std::move(entry));
				}
			}
			return kept;
		}

		// A result callback that records what it got.
		auto recordResult(std::unique_ptr<int>& into) {
			return [&into, owned = std::make_unique<int>(0)](std::unique_ptr<int> result) {
				record("callback", std::to_string(*result));
				into = std::move(result);
			};
		}

	} // namespace

	TEST_F(AppTest, PushCoversTheTopAndTicksUpdatesAndRendersOnlyTheNewTop) {
		start();
		app->scenes().push<SceneA>();
		frame(0.0);
		take();

		app->scenes().push<SceneB>();
		frame(kStep);
		const std::vector<std::string> expected{"B.built", "A.onCovered", "B.onEnter", "B.fixedUpdate 0", "B.update", "B.render 0"};
		EXPECT_EQ(take(), expected);
	}

	TEST_F(AppTest, OnlyTheTopSceneGetsEvents) {
		start();
		app->scenes().push<SceneA>();
		app->scenes().push<SceneB>();
		frame(0.0);
		take();
		platform->push({.type = foundation::InputEvent::Type::KeyDown, .key = foundation::Key::A});
		frame(kStep);
		EXPECT_EQ(count("B.handleEvent"), 1);
		EXPECT_EQ(count("A.handleEvent"), 0);
		EXPECT_EQ(count("A.fixedUpdate"), 0);
		EXPECT_EQ(count("A.update"), 0);
	}

	TEST_F(AppTest, ChangesAskedForInADispatchApplyThatFrame) {
		start();
		hooks()["A"].handleEvent = [](App& running, const foundation::InputEvent& /*event*/) {
			running.scenes().push<SceneB>();
			return true;
		};
		app->scenes().push<SceneA>();
		frame(0.0);
		take();
		platform->push({.type = foundation::InputEvent::Type::PointerDown});
		frame(0.0);
		const std::vector<std::string> expected{"A.handleEvent", "B.built", "A.onCovered", "B.onEnter", "B.update", "B.render 0"};
		EXPECT_EQ(take(), expected);
	}

	TEST_F(AppTest, ChangesAskedForWhileChangesApplyWaitForTheNextFrame) {
		start();
		hooks()["A"].onEnter = [](App& running) {
			running.scenes().push<SceneB>();
		};
		app->scenes().push<SceneA>();
		frame(0.0);
		EXPECT_EQ(count("B.built"), 0);
		frame(0.0);
		EXPECT_EQ(count("B.onEnter"), 1);
	}

	TEST_F(AppTest, CoveredScenesRenderFromTheTopmostOpaqueOneUp) {
		start();
		app->scenes().push<SceneA>();
		app->scenes().push<SceneB>();
		app->scenes().push<SceneC>();
		app->scenes().push<SceneD>();
		frame(0.0);
		take();
		frame(0.0);
		const std::vector<std::string> expected{"D.update", "B.render 0", "C.render 0", "D.render 0"};
		EXPECT_EQ(take(), expected);
	}

	TEST_F(AppTest, ACoveredSceneRendersWithTheAlphaItHadWhenCovered) {
		start();
		app->scenes().push<SceneA>();
		frame(kStep / 4);
		EXPECT_EQ(journal().back(), "A.render 0.25");

		app->scenes().push<PauseMenu>();
		frame(kStep / 2);
		EXPECT_EQ(count("A.render 0.25"), 2);
		EXPECT_EQ(journal().back(), "Menu.render 0.75");

		frame(kStep / 2); // the menu ticks once; the arena under it holds still
		EXPECT_EQ(count("A.render 0.25"), 3);
		EXPECT_EQ(count("Menu.fixedUpdate 0"), 1);
		EXPECT_EQ(journal().back(), "Menu.render 0.25");

		app->scenes().pop();
		frame(kStep / 2);
		EXPECT_EQ(journal().back(), "A.render 0.75");
	}

	TEST_F(AppTest, EachScenesTickCountIsItsOwn) {
		start();
		app->scenes().push<SceneA>();
		frame(2 * kStep);
		app->scenes().push<SceneB>();
		frame(kStep);
		app->scenes().pop();
		frame(kStep);
		EXPECT_EQ(count("A.fixedUpdate 0"), 1);
		EXPECT_EQ(count("A.fixedUpdate 1"), 1);
		EXPECT_EQ(count("B.fixedUpdate 0"), 1);
		EXPECT_EQ(count("A.fixedUpdate 2"), 1);
	}

	// --- Order within a frame ---

	TEST_F(AppTest, TwoSwitchesInOneFrameBuildAndEnterOnlyTheLast) {
		start();
		app->scenes().add("a", [] { return std::make_unique<SceneA>(); });
		app->scenes().add("b", [] { return std::make_unique<SceneB>(); });
		app->scenes().add<SceneC>("c");
		ASSERT_TRUE(app->scenes().switchTo("a"));
		frame(0.0);
		take();

		ASSERT_TRUE(app->scenes().switchTo("b"));
		ASSERT_TRUE(app->scenes().switchTo("c"));
		frame(0.0);
		const std::vector<std::string> expected{"A.onExit", "A.destroyed", "C.built", "C.onEnter"};
		EXPECT_EQ(changes(), expected);
		EXPECT_EQ(app->scenes().topName(), "c");
	}

	TEST_F(AppTest, PushesAndPopsApplyInTheOrderAskedFor) {
		start();
		app->scenes().push<SceneA>();
		frame(0.0);
		take();

		app->scenes().push<SceneB>();
		app->scenes().pop();
		frame(0.0);
		const std::vector<std::string> pushThenPop{"B.built", "A.onCovered", "B.onEnter", "B.onExit", "B.destroyed", "A.onUncovered"};
		EXPECT_EQ(changes(), pushThenPop);

		app->scenes().pop();
		app->scenes().push<SceneB>();
		frame(0.0);
		const std::vector<std::string> popThenPush{"A.onExit", "A.destroyed", "B.built", "B.onEnter"};
		EXPECT_EQ(changes(), popThenPush);
	}

	TEST_F(AppTest, ASwitchDiscardsWhatWasQueuedBeforeItAndKeepsWhatComesAfter) {
		start();
		app->scenes().add<SceneC>("c");
		app->scenes().push<SceneA>();
		app->scenes().push<SceneB>();
		ASSERT_TRUE(app->scenes().switchTo("c"));
		app->scenes().push<SceneD>();
		frame(0.0);
		const std::vector<std::string> expected{"C.built", "C.onEnter", "D.built", "C.onCovered", "D.onEnter"};
		EXPECT_EQ(changes(), expected);
	}

	// --- Finish, replace, reset ---

	TEST_F(AppTest, FinishHandsAMoveOnlyResultToTheCallbackBeforeOnUncovered) {
		start();
		app->scenes().push<SceneA>();
		std::unique_ptr<int> got;
		app->scenes().push<Picker>({}, recordResult(got));
		frame(0.0);
		take();

		top<Picker>().finish(std::make_unique<int>(42));
		frame(0.0);
		const std::vector<std::string> expected{"Picker.onExit", "Picker.destroyed", "callback.42", "A.onUncovered"};
		EXPECT_EQ(changes(), expected);
		ASSERT_NE(got, nullptr);
		EXPECT_EQ(*got, 42);
	}

	TEST_F(AppTest, MoveOnlyArgsReachTheScene) {
		start();
		app->scenes().push<Arena>({.ships = std::make_unique<int>(3)});
		frame(0.0);
		ASSERT_NE(top<Arena>().args.ships, nullptr);
		EXPECT_EQ(*top<Arena>().args.ships, 3);
	}

	TEST_F(AppTest, PopRunsNoResultCallback) {
		start();
		app->scenes().push<SceneA>();
		std::unique_ptr<int> got;
		app->scenes().push<Picker>({}, recordResult(got));
		frame(0.0);
		app->scenes().pop();
		frame(0.0);
		EXPECT_EQ(count("callback"), 0);
		EXPECT_EQ(count("A.onUncovered"), 1);
	}

	TEST_F(AppTest, ReplaceSwapsTheTopAndLeavesTheSceneUnderItCovered) {
		start();
		app->scenes().push<SceneA>();
		app->scenes().push<SceneB>();
		frame(0.0);
		take();

		app->scenes().replace<SceneC>();
		frame(0.0);
		const std::vector<std::string> expected{"B.onExit", "B.destroyed", "C.built", "C.onEnter"};
		EXPECT_EQ(changes(), expected);

		app->scenes().pop();
		frame(0.0);
		EXPECT_EQ(count("A.onUncovered"), 1);
	}

	TEST_F(AppTest, ReplaceDropsTheReplacedScenesResultCallback) {
		start();
		app->scenes().push<SceneA>();
		std::unique_ptr<int> got;
		app->scenes().push<Picker>({}, recordResult(got));
		frame(0.0);
		app->scenes().replace<SceneB>();
		frame(0.0);
		app->scenes().pop();
		frame(0.0);
		EXPECT_EQ(count("callback"), 0);
		EXPECT_EQ(app->scenes().top()->name(), std::string("A"));
	}

	TEST_F(AppTest, ReplaceOnAnEmptyStackPushes) {
		start();
		app->scenes().replace<SceneA>();
		frame(0.0);
		EXPECT_EQ(count("A.onEnter"), 1);
	}

	TEST_F(AppTest, ResetClearsTheStackTopDownThenPushes) {
		start();
		app->scenes().push<SceneA>();
		app->scenes().push<SceneB>();
		frame(0.0);
		take();

		app->scenes().reset<SceneD>();
		frame(0.0);
		const std::vector<std::string> expected{"B.onExit", "B.destroyed", "A.onExit", "A.destroyed", "D.built", "D.onEnter"};
		EXPECT_EQ(changes(), expected);
	}

	// --- Names ---

	TEST_F(AppTest, ScenesAddedByNameSwitchAndReload) {
		start();
		app->scenes().add("a", [] { return std::make_unique<SceneA>(); });
		app->scenes().add<SceneB>("b");
		EXPECT_TRUE(app->scenes().topName().empty());
		EXPECT_FALSE(app->scenes().reload());

		ASSERT_TRUE(app->scenes().switchTo("a"));
		frame(0.0);
		EXPECT_EQ(app->scenes().topName(), "a");
		take();

		ASSERT_TRUE(app->scenes().reload());
		frame(kStep);
		const std::vector<std::string> rebuilt{"A.onExit", "A.destroyed", "A.built", "A.onEnter", "A.fixedUpdate 0"};
		EXPECT_EQ(std::vector<std::string>(journal().begin(), journal().begin() + 5), rebuilt);

		app->scenes().push<SceneB>(); // pushed by type, known by the name its type was added under
		frame(0.0);
		EXPECT_EQ(app->scenes().topName(), "b");
		take();
		ASSERT_TRUE(app->scenes().reload());
		frame(0.0);
		const std::vector<std::string> rebuiltB{"B.onExit", "B.destroyed", "B.built", "B.onEnter"};
		EXPECT_EQ(changes(), rebuiltB);
	}

	TEST_F(AppTest, AnUnknownNameIsRefusedAndDiscardsNothing) {
		start();
		app->scenes().push<SceneA>();
		EXPECT_FALSE(app->scenes().switchTo("missing"));
		frame(0.0);
		EXPECT_EQ(count("A.onEnter"), 1);
	}

	TEST_F(AppTest, AScenePushedWithArgumentsHasNoNameAndReloadRefusesIt) {
		start();
		app->scenes().push<Arena>({.ships = std::make_unique<int>(1)});
		app->scenes().push<SceneC>();
		frame(0.0);
		EXPECT_TRUE(app->scenes().topName().empty()); // C was never added by name
		EXPECT_FALSE(app->scenes().reload());
		app->scenes().pop();
		frame(0.0);
		EXPECT_TRUE(app->scenes().topName().empty());
		EXPECT_FALSE(app->scenes().reload());
	}

	TEST_F(AppTest, ALaterAddUnderANameReplacesTheFactory) {
		start();
		app->scenes().add("a", [] { return std::make_unique<SceneA>(); });
		app->scenes().add("a", [] { return std::make_unique<SceneB>(); });
		ASSERT_TRUE(app->scenes().switchTo("a"));
		frame(0.0);
		EXPECT_EQ(count("A.built"), 0);
		EXPECT_EQ(count("B.onEnter"), 1);
	}

	// --- Where the design leaves it open ---

	TEST_F(AppTest, FinishFromASceneThatIsNotOnTopWhenItAppliesIsIgnored) {
		start();
		app->scenes().push<SceneA>();
		std::unique_ptr<int> got;
		app->scenes().push<Picker>({}, recordResult(got));
		frame(0.0);
		take();

		Picker& picker = top<Picker>();
		app->scenes().push<SceneB>();
		picker.finish(std::make_unique<int>(1));
		frame(0.0);
		EXPECT_EQ(app->scenes().top()->name(), std::string("B"));
		EXPECT_EQ(count("Picker.onExit"), 0);
		EXPECT_EQ(count("callback"), 0);

		app->scenes().pop();
		frame(0.0);
		EXPECT_EQ(app->scenes().top(), &picker);
	}

	TEST_F(AppTest, FinishingTwiceFinishesOnce) {
		start();
		app->scenes().push<SceneA>();
		std::unique_ptr<int> got;
		app->scenes().push<Picker>({}, recordResult(got));
		frame(0.0);

		Picker& picker = top<Picker>();
		picker.finish(std::make_unique<int>(1));
		picker.finish(std::make_unique<int>(2));
		frame(0.0);
		EXPECT_EQ(count("callback"), 1);
		EXPECT_EQ(count("callback.1"), 1);
		EXPECT_EQ(app->scenes().top()->name(), std::string("A"));
	}

	TEST_F(AppTest, FinishFromASceneNeverStackedIsIgnored) {
		start();
		Picker loose;
		loose.finish(std::make_unique<int>(1));
		frame(0.0);
		EXPECT_EQ(app->scenes().top(), nullptr);
	}

	TEST_F(AppTest, PopOnAnEmptyStackDoesNothing) {
		start();
		app->scenes().pop();
		EXPECT_TRUE(frame(0.0));
		EXPECT_EQ(app->scenes().top(), nullptr);
		app->scenes().push<SceneA>();
		app->scenes().pop();
		app->scenes().pop();
		EXPECT_TRUE(frame(0.0));
		EXPECT_EQ(app->scenes().top(), nullptr);
	}

} // namespace app::testing
