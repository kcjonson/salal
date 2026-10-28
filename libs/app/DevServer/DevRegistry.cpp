#include "DevServer/DevRegistry.h"

#include <algorithm>
#include <type_traits>

namespace app::detail {

	namespace {
		// The entry's callable is swapped into `taken`, so it dies with the caller's local, not here.
		template <typename Map, typename Callable>
		bool takeFrom(Map& entries, std::uint64_t id, Callable& taken) {
			for (auto it = entries.begin(); it != entries.end(); ++it) {
				auto&	   stack = it->second;
				const auto found = std::ranges::find(stack, id, [](const auto& entry) { return entry.first; });
				if (found != stack.end()) {
					taken.swap(found->second);
					stack.erase(found);
					if (stack.empty()) {
						entries.erase(it);
					}
					return true;
				}
			}
			return false;
		}

		template <typename Map>
		auto newest(const Map& entries, std::string_view name) {
			const auto it = entries.find(name);
			using Callable = std::decay_t<decltype(it->second.back().second)>;
			return it != entries.end() ? it->second.back().second : Callable{};
		}
	} // namespace

	bool isBuiltInControl(std::string_view action) {
		return std::ranges::find(kBuiltInControls, action) != kBuiltInControls.end();
	}

	std::uint64_t DevRegistry::addHandler(DevKind kind, std::string name, DevServer::Handler handler) {
		const std::lock_guard lock(mutex);
		const std::uint64_t	  id = nextId++;
		(kind == DevKind::Control ? controls : routes)[std::move(name)].emplace_back(id, std::move(handler));
		return id;
	}

	std::uint64_t DevRegistry::addProvider(std::string what, DevServer::Provider provider) {
		const std::lock_guard lock(mutex);
		const std::uint64_t	  id = nextId++;
		providers[std::move(what)].emplace_back(id, std::move(provider));
		return id;
	}

	// The removed callable is destroyed after the lock is let go: its captures may own a Registration,
	// whose reset comes back here on the same thread.
	void DevRegistry::remove(std::uint64_t id) {
		DevServer::Handler	  handler;
		DevServer::Provider	  provider;
		const std::lock_guard lock(mutex);
		if (!takeFrom(routes, id, handler) && !takeFrom(controls, id, handler)) {
			takeFrom(providers, id, provider);
		}
	}

	bool DevRegistry::has(DevKind kind, std::string_view name) const {
		const std::lock_guard lock(mutex);
		return kind == DevKind::State ? providers.contains(name) : handlers(kind).contains(name);
	}

	DevServer::Handler DevRegistry::handler(DevKind kind, std::string_view name) const {
		const std::lock_guard lock(mutex);
		return newest(handlers(kind), name);
	}

	DevServer::Provider DevRegistry::provider(std::string_view what) const {
		const std::lock_guard lock(mutex);
		return newest(providers, what);
	}

} // namespace app::detail
