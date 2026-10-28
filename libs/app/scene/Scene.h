#pragma once

// A scene is one screen of a game on the App's scene stack. Only the top scene gets events, fixed
// ticks and update; the scenes under it keep rendering from the topmost opaque one up. Every call
// the App makes into a scene is wrapped: an exception is logged and the frame goes on.

#include <CoordinateSystem/CoordinateSystem.h>
#include <input/InputTypes.h>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

namespace app {

	class App;
	class SceneStack;

	namespace detail {
		// A finished scene's result on its way to the callback its pusher gave.
		struct ResultBox {
			virtual ~ResultBox() = default;
		};

		template <typename R>
		struct TypedResultBox final : ResultBox {
			explicit TypedResultBox(R&& result)
				: value(std::move(result)) {}
			R value;
		};
	} // namespace detail

	// What a fixed tick reads, and all a deterministic sim may: the scene's own tick count since its
	// onEnter, from 0, the step in seconds, and the tick reader's snapshot. No clock.
	struct TickContext {
		std::uint64_t					 tick;
		double							 step;
		const foundation::InputSnapshot& input;
	};

	// What update reads: the frame's real delta in seconds, at most FrameConfig::maxFrameDelta, and the
	// frame reader's snapshot. The frame reader isn't dropped when the top scene changes, as the tick
	// reader is, so a scene's first update still sees the press that put it on top, as worldsim's polled
	// input does; a scene that should ignore that press reacts in fixedUpdate or to events.
	struct FrameContext {
		double							 dt;
		const foundation::InputSnapshot& input;
	};

	class Scene {
	  public:
		Scene() = default;
		virtual ~Scene() = default;

		Scene(const Scene&) = delete;
		Scene& operator=(const Scene&) = delete;
		Scene(Scene&&) = delete;
		Scene& operator=(Scene&&) = delete;

		virtual const char* name() const = 0;
		virtual void		onEnter(App& /*app*/) {}
		virtual void		onExit() {}
		virtual void		onCovered() {}	 // another scene was pushed over this one
		virtual void		onUncovered() {} // on top again; a result, if any, has already been delivered
		virtual bool		handleEvent(const foundation::InputEvent& /*event*/) { return false; }
		virtual void		fixedUpdate(const TickContext& /*tick*/) {}
		virtual void		update(const FrameContext& /*frame*/) {}
		virtual void		render(float alpha) = 0; // alpha into the current fixed step, 0 to 1
		virtual void		onResize(const Renderer::Viewport& /*viewport*/) {}
		virtual bool		opaque() const { return true; }	   // false: the scenes below keep rendering (a pause menu over the arena)
		virtual std::string exportState() const { return {}; } // JSON for /api/state?what=scene; only the dev server calls it

	  private:
		friend class SceneStack;
		template <typename A, typename R>
		friend class TypedScene;

		void finishWith(std::unique_ptr<detail::ResultBox> result);

		SceneStack*	  stack = nullptr;
		std::uint64_t stackId = 0; // which stack entry this is, never reused
	};

	struct NoArgs {};
	struct NoResult {};

	// A scene with typed arguments and a typed result. A scene with Args takes them in its constructor;
	// a NoArgs scene is built as S().
	template <typename A = NoArgs, typename R = NoResult>
	class TypedScene : public Scene {
	  public:
		using Args = A;
		using Result = R;

	  protected:
		// Pop this scene and hand `result` to the callback whoever pushed it gave, before the scene under
		// it is uncovered. Applies with the frame's other scene changes; a scene that isn't the top one
		// by then is left where it is, and its result dropped, logged.
		void finish(R result) { finishWith(std::make_unique<detail::TypedResultBox<R>>(std::move(result))); }
	};

} // namespace app
