#pragma once

// The App's scenes. Changes are queued and apply together once a frame, after dispatch and before
// update (step 6 of the frame), in the order they were asked for, except that a replace, reset or
// switchTo discards whatever was queued before it that frame. Changes asked for while they apply
// (from an onEnter or a result callback, say) wait for the next frame; while the App is paused they
// all wait for resume.
//
// Scenes are built when their change applies, so of two switches in one frame only the last is
// built. Arguments, results and result callbacks are typed per scene and may be move-only. A push
// with a result callback remembers the scene on top when it was asked for, whoever asks (an overlay
// or game code too), and is dropped, logged, if that scene has left the stack by the time it applies,
// so a callback never outlives its pusher. A push without a callback queues in order regardless, so
// it can land over a scene that replaced the one that asked.
//
// Names are for the command line, the dev server and reload. A scene added by name has a factory
// that takes no arguments. A NoArgs scene pushed by type is known by the name its type was added
// under, if any; a scene pushed with arguments has no name, so reload never rebuilds it without them.

#include "detail/TypeKey.h"
#include "scene/Scene.h"

#include <concepts>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace app {

	namespace detail {
		struct SceneBuilder {
			virtual ~SceneBuilder() = default;
			virtual std::unique_ptr<Scene> build() = 0;
		};

		template <typename S>
		class TypedSceneBuilder final : public SceneBuilder {
		  public:
			explicit TypedSceneBuilder(typename S::Args&& sceneArgs)
				: args(std::move(sceneArgs)) {}

			std::unique_ptr<Scene> build() override {
				if constexpr (std::same_as<typename S::Args, NoArgs>) {
					return std::make_unique<S>();
				} else {
					return std::make_unique<S>(std::move(args));
				}
			}

		  private:
			typename S::Args args;
		};

		struct ResultCallback {
			virtual ~ResultCallback() = default;
			virtual void deliver(ResultBox& result) = 0;
		};

		template <typename R, typename F>
		class TypedResultCallback final : public ResultCallback {
		  public:
			explicit TypedResultCallback(F&& callback)
				: onResult(std::move(callback)) {}

			// The box is the one this callback's scene filled: both come from the same push, typed by its S.
			void deliver(ResultBox& result) override {
				std::invoke(std::move(onResult), std::move(static_cast<TypedResultBox<R>&>(result).value));
			}

		  private:
			F onResult;
		};

		template <typename S>
		constexpr bool buildable() {
			if constexpr (std::same_as<typename S::Args, NoArgs>) {
				return std::default_initializable<S>;
			} else {
				return std::constructible_from<S, typename S::Args&&>;
			}
		}
	} // namespace detail

	// A scene the stack can build by type: a TypedScene whose Args and Result are its TypedScene base's,
	// built from its Args (or with none, for NoArgs).
	template <typename S>
	concept StackScene = requires {
		typename S::Args;
		typename S::Result;
	} && std::derived_from<S, TypedScene<typename S::Args, typename S::Result>> && detail::buildable<S>();

	class SceneStack {
	  public:
		using Factory = std::function<std::unique_ptr<Scene>()>;

		~SceneStack();

		SceneStack(const SceneStack&) = delete;
		SceneStack& operator=(const SceneStack&) = delete;
		SceneStack(SceneStack&&) = delete;
		SceneStack& operator=(SceneStack&&) = delete;

		// By name, for what worldsim registers (its scene classes live in .cpp files). A second add
		// under one name replaces the first.
		void add(std::string name, Factory factory);
		// The same, for a complete type whose Args default: the name builds it with Args{}. A NoArgs
		// scene pushed by type is known by this name.
		template <StackScene S>
			requires std::default_initializable<typename S::Args>
		void add(std::string name);

		// Covers the top scene with S.
		template <StackScene S>
		void push(typename S::Args args = {});
		// The same, and when S finishes, onResult(S::Result) runs before the scene under it is uncovered.
		template <StackScene S, typename F>
			requires std::invocable<F, typename S::Result&&>
		void push(typename S::Args args, F onResult);
		// Replaces the top scene with S (pushes it on an empty stack). The replaced scene's result
		// callback is dropped without running.
		template <StackScene S>
		void replace(typename S::Args args = {});
		// Clears the stack, top down, then pushes S.
		template <StackScene S>
		void reset(typename S::Args args = {});
		// Pops the top scene with no result; its pusher's callback doesn't run. Nothing on an empty stack.
		void pop();
		// Resets to the scene added under `name`; false, logged, with nothing queued or discarded, for a
		// name nothing was added under.
		bool switchTo(std::string_view name);
		// Rebuilds the top scene by its name, as a replace, so a result callback it was pushed with is
		// dropped without running; false for an empty stack or an unnamed scene.
		bool reload();

		// The top scene as of the last applied change; null when the stack is empty.
		Scene*			 top();
		std::string_view topName() const;

	  private:
		friend class App;
		friend class DevServer; // /api/state?what=scene reads every entry
		friend class Scene;

		explicit SceneStack(App& owner);

		enum class ChangeKind : std::uint8_t { Push, Pop, Finish, Replace, Reset };

		struct Change {
			ChangeKind								kind = ChangeKind::Push;
			std::unique_ptr<detail::SceneBuilder>	builder;
			std::string								name;
			std::unique_ptr<detail::ResultCallback> onResult;
			std::uint64_t							requester = 0; // a push with a callback: the scene on top when it was asked for
			std::uint64_t							finisher = 0;
			std::unique_ptr<detail::ResultBox>		result;
		};

		struct Entry {
			std::unique_ptr<Scene>					scene;
			std::string								name;
			std::unique_ptr<detail::ResultCallback> onResult;
			std::uint64_t							ticks = 0;			 // fixed ticks run since onEnter
			float									coveredAlpha = 0.0F; // the alpha it had when covered, which it renders with
		};

		template <StackScene S>
		Change buildChange(ChangeKind kind, typename S::Args&& args) const {
			return {
				.kind = kind,
				.builder = std::make_unique<detail::TypedSceneBuilder<S>>(std::move(args)),
				.name = std::same_as<typename S::Args, NoArgs> ? nameFor(detail::typeKey<S>()) : std::string{},
			};
		}

		void		addNamed(std::string name, Factory factory, const void* type);
		std::string nameFor(const void* type) const;
		void		queue(Change change);
		void		queueFinish(std::uint64_t finisher, std::unique_ptr<detail::ResultBox> result);

		std::uint64_t topId() const { return entries.empty() ? 0 : entries.back().scene->stackId; }
		bool		  holds(std::uint64_t id) const;

		// For the App. Step 6, with the top scene's alpha now, which a scene it covers keeps.
		void   apply(float liveAlpha);
		Entry* topEntry() { return entries.empty() ? nullptr : &entries.back(); }
		// Every stacked scene's onResize, top down.
		void resized(const Renderer::Viewport& viewport);
		// The stack from its topmost opaque scene up, the top at liveAlpha and each covered scene at the
		// alpha it had when covered.
		void render(float liveAlpha);
		// App::shutdown: every scene exits, top down, and nothing queued applies.
		void exitAll();

		void				   pushBuilt(Change& change, float liveAlpha);
		void				   popTop();
		void				   finishTop(Change& change);
		void				   replaceTop(Change& change);
		void				   resetTo(Change& change);
		std::unique_ptr<Scene> build(detail::SceneBuilder& builder, const std::string& name);
		void				   enter(std::unique_ptr<Scene> scene, std::string name, std::unique_ptr<detail::ResultCallback> onResult);
		Entry				   exitTop();
		void				   uncoverTop();

		App&											 app;
		std::map<std::string, Factory, std::less<>>		 factories;
		std::vector<std::pair<const void*, std::string>> typeNames; // add<S>'s, so S pushed by type has its name
		std::vector<Entry>								 entries;
		std::vector<Change>								 pending;
		std::uint64_t									 nextId = 1;
	};

	template <StackScene S>
		requires std::default_initializable<typename S::Args>
	void SceneStack::add(std::string name) {
		if constexpr (std::same_as<typename S::Args, NoArgs>) {
			addNamed(std::move(name), [] { return std::make_unique<S>(); }, detail::typeKey<S>());
		} else {
			addNamed(std::move(name), [] { return std::make_unique<S>(typename S::Args{}); }, nullptr);
		}
	}

	template <StackScene S>
	void SceneStack::push(typename S::Args args) {
		queue(buildChange<S>(ChangeKind::Push, std::move(args)));
	}

	template <StackScene S, typename F>
		requires std::invocable<F, typename S::Result&&>
	void SceneStack::push(typename S::Args args, F onResult) {
		Change change = buildChange<S>(ChangeKind::Push, std::move(args));
		change.onResult = std::make_unique<detail::TypedResultCallback<typename S::Result, F>>(std::move(onResult));
		change.requester = topId();
		queue(std::move(change));
	}

	template <StackScene S>
	void SceneStack::replace(typename S::Args args) {
		queue(buildChange<S>(ChangeKind::Replace, std::move(args)));
	}

	template <StackScene S>
	void SceneStack::reset(typename S::Args args) {
		queue(buildChange<S>(ChangeKind::Reset, std::move(args)));
	}

} // namespace app
