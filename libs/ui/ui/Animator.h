#pragma once

// Time in the ui (R8.17, R8.28): one clock a context advances in its update phase, an animator of
// tweens over it, and timers. Nothing in ui or a game's ui code reads a platform timer, so a test
// moves the clock exactly as far as it says and a frozen clock holds a frame still (R13.37).
//
// A tween interpolates a float, a Vec2 or a Color from `from` to `to`; duration and easing default to
// the theme's motion.duration.normal and motion.easing.standard. Tweens tick in creation order; one
// started during a tick waits for the next. Retargeted mid-flight, a tween heads for its new target
// from where it is, and a reversal takes only as long as the way back (CSS Transitions' reversing
// shortening), so a quick hover in and out never snaps. Under reduced motion every tween completes
// on its first tick. A tween with an owner is cancelled when the owner is removed.
//
// Tween<T> owns its tween: destroying the handle cancels it, so a component holds the tweens it
// retargets as members, and detach() lets one run out on its own.
//
//   lift = tween<float>({.onUpdate = [this](float px) { setTransform({.translate = {0.0F, -px}}); }});
//   lift.retarget(isHovered() ? 6.0F : 0.0F);

#include "ui/Registry.h"

#include <graphics/Color.h>
#include <math/Types.h>

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <vector>

namespace ui {

	namespace dtcg {
		struct CubicBezierValue;
	}

	// Milliseconds of frame time, the unit of the motion tokens.
	class Clock {
	  public:
		[[nodiscard]] double		now() const { return elapsed; }
		[[nodiscard]] double		dt() const { return delta; }
		[[nodiscard]] std::uint64_t frame() const { return frames; }
		// Frozen, a frame still counts but no time passes.
		[[nodiscard]] bool frozen() const { return isFrozen; }
		void			   setFrozen(bool value) { isFrozen = value; }
		// A negative or non-finite step is taken as none.
		void advance(double milliseconds);

	  private:
		double		  elapsed = 0.0;
		double		  delta = 0.0;
		std::uint64_t frames = 0;
		bool		  isFrozen = false;
	};

	// CSS's cubic-bezier(x1, y1, x2, y2): x(t) solved for the progress, then y(t). Default, linear. x
	// outside [0, 1] is clamped, since a curve there isn't a function of time.
	class Easing {
	  public:
		Easing() = default;
		Easing(float x1, float y1, float x2, float y2);
		explicit Easing(const dtcg::CubicBezierValue& points);

		// 0 and below is 0, 1 and above is 1.
		[[nodiscard]] float operator()(float progress) const;

	  private:
		[[nodiscard]] double solve(double x) const;

		double ax = 0.0;
		double bx = 0.0;
		double cx = 0.0;
		double ay = 0.0;
		double by = 0.0;
		double cy = 0.0;
		bool   linear = true;
	};

	// Slots with generations, as ComponentId is: 0 is none.
	struct TweenId {
		std::uint32_t index = 0;
		std::uint32_t gen = 0;
		bool		  operator==(const TweenId&) const = default;
	};

	struct TimerId {
		std::uint32_t	   index = 0;
		std::uint32_t	   gen = 0;
		[[nodiscard]] bool valid() const { return gen != 0; }
		bool			   operator==(const TimerId&) const = default;
	};

	namespace detail {
		template <typename T>
		struct TweenValue;
		template <>
		struct TweenValue<float> {
			static constexpr int kCount = 1;
			static void			 pack(float value, float* out) { out[0] = value; }
			static float		 unpack(const float* in) { return in[0]; }
		};
		template <>
		struct TweenValue<Foundation::Vec2> {
			static constexpr int kCount = 2;
			static void			 pack(Foundation::Vec2 value, float* out) {
				 out[0] = value.x;
				 out[1] = value.y;
			}
			static Foundation::Vec2 unpack(const float* in) { return {in[0], in[1]}; }
		};
		template <>
		struct TweenValue<Foundation::Color> {
			static constexpr int kCount = 4;
			static void			 pack(const Foundation::Color& value, float* out) {
				 out[0] = value.r;
				 out[1] = value.g;
				 out[2] = value.b;
				 out[3] = value.a;
			}
			static Foundation::Color unpack(const float* in) { return {in[0], in[1], in[2], in[3]}; }
		};
	} // namespace detail

	template <typename T>
	struct TweenArgs {
		T							  from{};
		T							  to{};
		std::optional<float>		  durationMs; // the theme's motion.duration.normal when unset
		std::optional<Easing>		  easing;	  // the theme's motion.easing.standard when unset
		std::function<void(const T&)> onUpdate;	  // each tick, and once with `from` as it starts
		std::function<void()>		  onComplete; // after the last onUpdate, when it runs to its end
		ComponentId					  owner;	  // cancelled with it; Component::tween sets it
	};

	class Animator;

	template <typename T>
	class Tween {
	  public:
		Tween() = default;
		~Tween() { reset(); }
		Tween(const Tween&) = delete;
		Tween& operator=(const Tween&) = delete;
		Tween(Tween&& other) noexcept
			: animator(other.animator),
			  id(other.id) {
			other.animator = nullptr;
		}
		Tween& operator=(Tween&& other) noexcept {
			if (this != &other) {
				reset();
				animator = other.animator;
				id = other.id;
				other.animator = nullptr;
			}
			return *this;
		}

		[[nodiscard]] bool running() const;
		// What onUpdate was last handed.
		[[nodiscard]] T value() const;
		// Stops where it is: no more onUpdate, no onComplete.
		void cancel();
		// Heads for `to` from the current value; restarts a finished tween unless its owner is gone. A
		// no-op when running towards `to` already, or idle at it, so a component can retarget to its
		// state every frame. The duration and easing are the ones it was made with unless given.
		void retarget(const T& to, std::optional<float> durationMs = std::nullopt, std::optional<Easing> easing = std::nullopt);
		// Lets it run out without the handle; it can't be retargeted after.
		void detach();

	  private:
		friend class Animator;
		Tween(Animator* owner, TweenId tweenId)
			: animator(owner),
			  id(tweenId) {}
		void reset();

		Animator* animator = nullptr;
		TweenId	  id;
	};

	class Animator {
	  public:
		explicit Animator(const Clock& clock);

		template <typename T>
		[[nodiscard]] Tween<T> tween(const TweenArgs<T>& args);

		// `callback` once, `delayMs` from now, or every `intervalMs`; dropped with `owner`.
		TimerId after(double delayMs, std::function<void()> callback, ComponentId owner = {});
		TimerId every(double intervalMs, std::function<void()> callback, ComponentId owner = {});
		void	cancel(TimerId timer);

		// The update phase, after the clock advanced: due timers in due order, then each running tween
		// in creation order.
		void tick();
		// Runs every tween to its end, the ones their completions start included, for a capture that
		// shows where animations land; a chain still going after 64 rounds is logged and left. How many
		// it finished.
		int settle();
		// Owners about to be removed: their tweens are cancelled and their timers dropped.
		void ownersRemoved(const std::vector<ComponentId>& owners);

		void			   setReducedMotion(bool value) { reducedMotion = value; }
		[[nodiscard]] bool isReducedMotion() const { return reducedMotion; }
		void			   setDefaults(float durationMs, Easing easing);
		// Tweens running, and timers waiting.
		[[nodiscard]] int activeTweens() const;
		[[nodiscard]] int activeTimers() const;

	  private:
		template <typename>
		friend class Tween;

		static constexpr int kMaxComponents = 4;
		using Values = std::array<float, kMaxComponents>;

		struct Slot {
			Values							  from{};
			Values							  to{};
			Values							  values{};
			Values							  reversingFrom{};
			int								  count = 1;
			std::function<void(const float*)> publish;
			std::function<void()>			  onComplete;
			ComponentId						  owner;
			Easing							  easing;
			double							  startedAt = 0.0;
			float							  duration = 0.0F;
			float							  baseDuration = 0.0F;
			float							  shortening = 1.0F;
			float							  eased = 0.0F;
			std::uint32_t					  gen = 1;
			bool							  used = false;
			bool							  running = false;
			bool							  listed = false;	// in `order`, which drops ended ones between passes
			bool							  detached = false; // freed when it ends
			bool							  ownerGone = false;
		};
		struct Timer {
			std::function<void()> callback;
			ComponentId			  owner;
			double				  due = 0.0;
			double				  interval = 0.0; // 0 for once
			std::uint64_t		  sequence = 0;	  // creation, to break ties in due order
			std::uint32_t		  gen = 1;
			bool				  used = false;
		};

		TweenId create(
			int								  count,
			const float*					  from,
			const float*					  to,
			std::optional<float>			  durationMs,
			std::optional<Easing>			  easing,
			std::function<void(const float*)> publish,
			std::function<void()>			  onComplete,
			ComponentId						  owner
		);
		[[nodiscard]] Slot*		  slotOf(TweenId id);
		[[nodiscard]] const Slot* slotOf(TweenId id) const;
		// By index, not reference: a callback may start tweens, and a slot one frees isn't reused
		// until no callback is running.
		void					  start(std::uint32_t index, const Values& from, float duration);
		void					  advance(std::uint32_t index);
		void					  finish(std::uint32_t index);
		void					  publish(std::uint32_t index);
		void					  retarget(TweenId id, const float* to, std::optional<float> durationMs, std::optional<Easing> easing);
		void					  cancel(TweenId id);
		void					  release(TweenId id);
		void					  detach(TweenId id);
		void					  free(std::uint32_t index);
		void					  freeTimer(std::uint32_t index);
		void					  reclaim();
		void					  compact();
		TimerId					  schedule(double delay, double interval, std::function<void()> callback, ComponentId owner);
		[[nodiscard]] static bool same(const Values& a, const Values& b, int count);

		const Clock&			   clock;
		std::deque<Slot>		   slots; // a deque, so a callback creating tweens moves no slot
		std::vector<std::uint32_t> freeSlots;
		std::vector<std::uint32_t> pendingSlots;
		std::vector<std::uint32_t> order; // running tweens, in creation order
		std::deque<Timer>		   timers;
		std::vector<std::uint32_t> freeTimers;
		std::vector<std::uint32_t> pendingTimers;
		std::uint64_t			   timerSequence = 0;
		float					   defaultDuration = 200.0F;
		Easing					   defaultEasing;
		int						   calling = 0; // user callbacks running
		bool					   reducedMotion = false;
	};

	// --- Templates ---

	template <typename T>
	Tween<T> Animator::tween(const TweenArgs<T>& args) {
		using Value = detail::TweenValue<T>;
		Values from{};
		Values to{};
		Value::pack(args.from, from.data());
		Value::pack(args.to, to.data());
		std::function<void(const float*)> publisher;
		if (args.onUpdate) {
			publisher = [onUpdate = args.onUpdate](const float* values) {
				onUpdate(Value::unpack(values));
			};
		}
		return {
			this,
			create(Value::kCount, from.data(), to.data(), args.durationMs, args.easing, std::move(publisher), args.onComplete, args.owner)
		};
	}

	template <typename T>
	bool Tween<T>::running() const {
		const Animator::Slot* slot = animator != nullptr ? animator->slotOf(id) : nullptr;
		return slot != nullptr && slot->running;
	}

	template <typename T>
	T Tween<T>::value() const {
		const Animator::Slot* slot = animator != nullptr ? animator->slotOf(id) : nullptr;
		return slot != nullptr ? detail::TweenValue<T>::unpack(slot->values.data()) : T{};
	}

	template <typename T>
	void Tween<T>::cancel() {
		if (animator != nullptr) {
			animator->cancel(id);
		}
	}

	template <typename T>
	void Tween<T>::retarget(const T& to, std::optional<float> durationMs, std::optional<Easing> easing) {
		if (animator == nullptr) {
			return;
		}
		Animator::Values target{};
		detail::TweenValue<T>::pack(to, target.data());
		animator->retarget(id, target.data(), durationMs, easing);
	}

	template <typename T>
	void Tween<T>::detach() {
		if (animator != nullptr) {
			animator->detach(id);
			animator = nullptr;
		}
	}

	template <typename T>
	void Tween<T>::reset() {
		if (animator != nullptr) {
			animator->release(id);
			animator = nullptr;
		}
	}

} // namespace ui
