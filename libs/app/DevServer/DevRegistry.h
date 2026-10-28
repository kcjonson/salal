#pragma once

// The dev server's registrations: the game's routes, control actions and state providers, by name.
// Several may hold one name; the newest answers. Guarded by a mutex, since the HTTP threads ask
// whether a name is registered (an unregistered one is a 404 at once) while the game thread adds,
// removes and calls them. Handlers are handed out as copies, so one can remove itself, or another,
// while it runs.

#include "DevServer/DevServer.h"

#include <array>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace app::detail {

	enum class DevKind : std::uint8_t { Route, Control, State };

	// The control actions and the state the server answers itself.
	inline constexpr std::array<std::string_view, 7> kBuiltInControls{"exit", "scene", "pause", "resume", "reload", "vsync", "step"};
	inline constexpr std::string_view				 kSceneState = "scene";
	// State with a default answer when nothing is registered for it.
	inline constexpr std::string_view kUiTree = "ui.tree";
	inline constexpr std::string_view kUiLint = "ui.lint";

	bool isBuiltInControl(std::string_view action);

	class DevRegistry {
	  public:
		// The entry's id, never 0.
		std::uint64_t addHandler(DevKind kind, std::string name, DevServer::Handler handler);
		std::uint64_t addProvider(std::string what, DevServer::Provider provider);
		void		  remove(std::uint64_t id);

		bool has(DevKind kind, std::string_view name) const;
		// The newest of the name's, copied; empty when there is none.
		DevServer::Handler	handler(DevKind kind, std::string_view name) const;
		DevServer::Provider provider(std::string_view what) const;

	  private:
		template <typename F>
		using Entries = std::map<std::string, std::vector<std::pair<std::uint64_t, F>>, std::less<>>;

		const Entries<DevServer::Handler>& handlers(DevKind kind) const { return kind == DevKind::Control ? controls : routes; }

		mutable std::mutex			 mutex;
		Entries<DevServer::Handler>	 routes;
		Entries<DevServer::Handler>	 controls;
		Entries<DevServer::Provider> providers;
		std::uint64_t				 nextId = 1;
	};

} // namespace app::detail
