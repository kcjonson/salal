#include "ui/Animator.h"

#include "ui/theme/dtcg/Values.h"

#include <utils/Log.h>

#include <algorithm>
#include <cmath>
#include <tuple>

namespace ui {

	namespace {
		constexpr int	 kNewtonSteps = 8;
		constexpr int	 kBisectionSteps = 32;
		constexpr double kEpsilon = 1e-7;
		constexpr int	 kSettleRounds = 64;

		// A user callback, counted so a slot it frees isn't reused while it runs.
		template <typename F>
		void call(int& calling, const F& callback) {
			++calling;
			try {
				callback();
			} catch (...) {
				--calling;
				throw;
			}
			--calling;
		}

		std::uint32_t nextGen(std::uint32_t gen) {
			return gen + 1 == 0 ? 1 : gen + 1;
		}
	} // namespace

	// --- Clock ---

	void Clock::advance(double milliseconds) {
		const double step = std::isfinite(milliseconds) && milliseconds > 0.0 ? milliseconds : 0.0;
		++frames;
		delta = isFrozen ? 0.0 : step;
		elapsed += delta;
	}

	// --- Easing ---

	// The curve's polynomial coefficients on each axis, with its ends at (0, 0) and (1, 1).
	Easing::Easing(float x1, float y1, float x2, float y2) {
		const double px1 = std::clamp(static_cast<double>(x1), 0.0, 1.0);
		const double px2 = std::clamp(static_cast<double>(x2), 0.0, 1.0);
		linear = px1 == static_cast<double>(y1) && px2 == static_cast<double>(y2);
		cx = 3.0 * px1;
		bx = (3.0 * (px2 - px1)) - cx;
		ax = 1.0 - cx - bx;
		cy = 3.0 * y1;
		by = (3.0 * (y2 - y1)) - cy;
		ay = 1.0 - cy - by;
	}

	Easing::Easing(const dtcg::CubicBezierValue& points)
		: Easing(points.x1, points.y1, points.x2, points.y2) {}

	float Easing::operator()(float progress) const {
		if (progress <= 0.0F) {
			return 0.0F;
		}
		if (progress >= 1.0F || linear) {
			return std::min(progress, 1.0F);
		}
		const double t = solve(progress);
		return static_cast<float>(((ay * t + by) * t + cy) * t);
	}

	// Newton's method converges in a few steps on the usual curves; bisection takes the flat spots,
	// where the slope is near zero.
	double Easing::solve(double x) const {
		const auto sampleX = [&](double t) {
			return ((ax * t + bx) * t + cx) * t;
		};
		double t = x;
		for (int i = 0; i < kNewtonSteps; ++i) {
			const double error = sampleX(t) - x;
			if (std::abs(error) < kEpsilon) {
				return t;
			}
			const double slope = (3.0 * ax * t + 2.0 * bx) * t + cx;
			if (std::abs(slope) < kEpsilon) {
				break;
			}
			t -= error / slope;
		}
		double low = 0.0;
		double high = 1.0;
		t = x;
		for (int i = 0; i < kBisectionSteps; ++i) {
			const double sample = sampleX(t);
			if (std::abs(sample - x) < kEpsilon) {
				return t;
			}
			(sample < x ? low : high) = t;
			t = (low + high) * 0.5;
		}
		return t;
	}

	// --- Animator ---

	Animator::Animator(const Clock& owner)
		: clock(owner) {}

	void Animator::setDefaults(float durationMs, Easing easing) {
		defaultDuration = std::max(durationMs, 0.0F);
		defaultEasing = easing;
	}

	TweenId Animator::create(
		int								  count,
		const float*					  from,
		const float*					  to,
		std::optional<float>			  durationMs,
		std::optional<Easing>			  easing,
		std::function<void(const float*)> publisher,
		std::function<void()>			  onComplete,
		ComponentId						  owner
	) {
		reclaim();
		std::uint32_t index = 0;
		if (!freeSlots.empty()) {
			index = freeSlots.back();
			freeSlots.pop_back();
		} else {
			index = static_cast<std::uint32_t>(slots.size());
			slots.emplace_back();
		}
		Slot&				slot = slots[index];
		const std::uint32_t gen = slot.gen;
		const bool			listed = slot.listed;
		slot = Slot{};
		slot.gen = gen;
		slot.listed = listed;
		slot.used = true;
		slot.count = count;
		std::copy_n(from, count, slot.from.begin());
		std::copy_n(to, count, slot.to.begin());
		slot.values = slot.from;
		slot.reversingFrom = slot.from;
		slot.publish = std::move(publisher);
		slot.onComplete = std::move(onComplete);
		slot.owner = owner;
		slot.easing = easing.value_or(defaultEasing);
		slot.baseDuration = std::max(durationMs.value_or(defaultDuration), 0.0F);
		const Values start = slot.from;
		this->start(index, start, slot.baseDuration);
		return {.index = index, .gen = gen};
	}

	Animator::Slot* Animator::slotOf(TweenId id) {
		if (id.gen == 0 || id.index >= slots.size()) {
			return nullptr;
		}
		Slot& slot = slots[id.index];
		return slot.used && slot.gen == id.gen ? &slot : nullptr;
	}

	const Animator::Slot* Animator::slotOf(TweenId id) const {
		return const_cast<Animator*>(this)->slotOf(id); // NOLINT(cppcoreguidelines-pro-type-const-cast)
	}

	void Animator::start(std::uint32_t index, const Values& from, float duration) {
		Slot& slot = slots[index];
		slot.from = from;
		slot.values = from;
		slot.duration = duration;
		slot.startedAt = clock.now();
		slot.eased = 0.0F;
		slot.running = true;
		if (!slot.listed) {
			slot.listed = true;
			order.push_back(index);
		}
		publish(index);
	}

	void Animator::publish(std::uint32_t index) {
		Slot& slot = slots[index];
		if (slot.publish) {
			call(calling, [&] { slot.publish(slot.values.data()); });
		}
	}

	void Animator::advance(std::uint32_t index) {
		Slot&		slot = slots[index];
		const float linearProgress = reducedMotion || slot.duration <= 0.0F
										 ? 1.0F
										 : std::min(static_cast<float>((clock.now() - slot.startedAt) / slot.duration), 1.0F);
		if (linearProgress >= 1.0F) {
			finish(index);
			return;
		}
		slot.eased = slot.easing(linearProgress);
		for (int i = 0; i < slot.count; ++i) {
			slot.values[i] = slot.from[i] + ((slot.to[i] - slot.from[i]) * slot.eased);
		}
		publish(index);
	}

	// To the end value, then the completion, which may restart it.
	void Animator::finish(std::uint32_t index) {
		Slot&				slot = slots[index];
		const std::uint32_t gen = slot.gen;
		slot.eased = 1.0F;
		slot.values = slot.to;
		slot.running = false;
		publish(index);
		if (!slot.used || slot.gen != gen || slot.running) {
			return;
		}
		if (slot.onComplete) {
			call(calling, [&] { slot.onComplete(); });
		}
		if (slot.used && slot.gen == gen && slot.detached && !slot.running) {
			free(index);
		}
	}

	void Animator::retarget(TweenId id, const float* to, std::optional<float> durationMs, std::optional<Easing> easing) {
		Slot* slot = slotOf(id);
		if (slot == nullptr || slot->ownerGone) {
			return;
		}
		Values target{};
		std::copy_n(to, slot->count, target.begin());
		// CSS leaves a running transition alone when asked for its end again, and starts nothing when the
		// value is there already.
		if (slot->running ? same(target, slot->to, slot->count) : same(target, slot->values, slot->count)) {
			return;
		}
		const float	 duration = std::max(durationMs.value_or(slot->baseDuration), 0.0F);
		const Values current = slot->values;
		if (slot->running && same(target, slot->reversingFrom, slot->count)) {
			// CSS Transitions 3.1: a reversal's duration shrinks by how far the run it reverses had got.
			slot->shortening = std::clamp(std::abs((slot->eased * slot->shortening) + 1.0F - slot->shortening), 0.0F, 1.0F);
			slot->reversingFrom = slot->to;
		} else {
			slot->shortening = 1.0F;
			slot->reversingFrom = current;
		}
		slot->to = target;
		if (easing) {
			slot->easing = *easing;
		}
		start(id.index, current, duration * slot->shortening);
	}

	void Animator::cancel(TweenId id) {
		if (Slot* slot = slotOf(id); slot != nullptr && slot->running) {
			slot->running = false;
			if (slot->detached) {
				free(id.index);
			}
		}
	}

	void Animator::release(TweenId id) {
		if (slotOf(id) != nullptr) {
			free(id.index);
		}
	}

	void Animator::detach(TweenId id) {
		if (Slot* slot = slotOf(id)) {
			slot->detached = true;
			if (!slot->running) {
				free(id.index);
			}
		}
	}

	// Its callbacks stay put until the slot is reused, which waits for every running callback to return.
	void Animator::free(std::uint32_t index) {
		Slot& slot = slots[index];
		slot.used = false;
		slot.running = false;
		slot.gen = nextGen(slot.gen);
		pendingSlots.push_back(index);
	}

	void Animator::freeTimer(std::uint32_t index) {
		Timer& timer = timers[index];
		timer.used = false;
		timer.gen = nextGen(timer.gen);
		pendingTimers.push_back(index);
	}

	void Animator::reclaim() {
		if (calling > 0) {
			return;
		}
		freeSlots.insert(freeSlots.end(), pendingSlots.begin(), pendingSlots.end());
		pendingSlots.clear();
		freeTimers.insert(freeTimers.end(), pendingTimers.begin(), pendingTimers.end());
		pendingTimers.clear();
	}

	void Animator::compact() {
		std::erase_if(order, [&](std::uint32_t index) {
			Slot& slot = slots[index];
			if (slot.used && slot.running) {
				return false;
			}
			slot.listed = false;
			return true;
		});
	}

	bool Animator::same(const Values& a, const Values& b, int count) {
		for (int i = 0; i < count; ++i) {
			if (std::abs(a[i] - b[i]) > 1e-6F) {
				return false;
			}
		}
		return true;
	}

	void Animator::tick() {
		const double																 now = clock.now();
		std::vector<std::tuple<double, std::uint64_t, std::uint32_t, std::uint32_t>> due;
		for (std::uint32_t i = 0; i < timers.size(); ++i) {
			const Timer& timer = timers[i];
			if (timer.used && timer.due <= now) {
				due.emplace_back(timer.due, timer.sequence, i, timer.gen);
			}
		}
		std::ranges::sort(due);
		for (const auto& [at, sequence, index, gen] : due) {
			Timer& timer = timers[index];
			if (!timer.used || timer.gen != gen) {
				continue; // cancelled by an earlier one
			}
			if (timer.interval > 0.0) {
				timer.due += timer.interval;
				if (timer.due <= now) {
					timer.due = now + timer.interval; // a long frame fires it once, not once per interval missed
				}
			} else {
				freeTimer(index);
			}
			call(calling, [&] { timer.callback(); });
		}

		const std::size_t count = order.size();
		for (std::size_t i = 0; i < count; ++i) {
			const std::uint32_t index = order[i];
			if (slots[index].used && slots[index].running) {
				advance(index);
			}
		}
		compact();
		reclaim();
	}

	int Animator::settle() {
		int finished = 0;
		for (int round = 0;; ++round) {
			compact();
			if (order.empty()) {
				break;
			}
			if (round == kSettleRounds) {
				LOG_ERROR(
					UI,
					"Animator::settle: %zu tweens still running after %d rounds; a completion keeps starting new ones",
					order.size(),
					kSettleRounds
				);
				break;
			}
			const std::vector<std::uint32_t> running = order;
			for (const std::uint32_t index : running) {
				if (slots[index].used && slots[index].running) {
					finish(index);
					++finished;
				}
			}
		}
		reclaim();
		return finished;
	}

	void Animator::ownersRemoved(const std::vector<ComponentId>& owners) {
		if (owners.empty()) {
			return;
		}
		const auto owned = [&](ComponentId owner) {
			return owner.valid() && std::ranges::find(owners, owner) != owners.end();
		};
		for (std::uint32_t i = 0; i < slots.size(); ++i) {
			Slot& slot = slots[i];
			if (slot.used && owned(slot.owner)) {
				slot.ownerGone = true;
				slot.running = false;
				if (slot.detached) {
					free(i);
				}
			}
		}
		for (std::uint32_t i = 0; i < timers.size(); ++i) {
			if (timers[i].used && owned(timers[i].owner)) {
				freeTimer(i);
			}
		}
	}

	// --- Timers ---

	TimerId Animator::schedule(double delay, double interval, std::function<void()> callback, ComponentId owner) {
		reclaim();
		std::uint32_t index = 0;
		if (!freeTimers.empty()) {
			index = freeTimers.back();
			freeTimers.pop_back();
		} else {
			index = static_cast<std::uint32_t>(timers.size());
			timers.emplace_back();
		}
		Timer& timer = timers[index];
		timer.callback = std::move(callback);
		timer.owner = owner;
		timer.due = clock.now() + std::max(delay, 0.0);
		timer.interval = interval;
		timer.sequence = ++timerSequence;
		timer.used = true;
		return {.index = index, .gen = timer.gen};
	}

	TimerId Animator::after(double delayMs, std::function<void()> callback, ComponentId owner) {
		return schedule(delayMs, 0.0, std::move(callback), owner);
	}

	// An interval under a millisecond is a millisecond, so a timer can't fire every frame by accident.
	TimerId Animator::every(double intervalMs, std::function<void()> callback, ComponentId owner) {
		const double interval = std::max(intervalMs, 1.0);
		return schedule(interval, interval, std::move(callback), owner);
	}

	void Animator::cancel(TimerId timer) {
		if (timer.gen != 0 && timer.index < timers.size() && timers[timer.index].used && timers[timer.index].gen == timer.gen) {
			freeTimer(timer.index);
		}
	}

	int Animator::activeTweens() const {
		return static_cast<int>(std::ranges::count_if(slots, [](const Slot& slot) { return slot.used && slot.running; }));
	}

	int Animator::activeTimers() const {
		return static_cast<int>(std::ranges::count_if(timers, [](const Timer& timer) { return timer.used; }));
	}

} // namespace ui
