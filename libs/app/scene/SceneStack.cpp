#include "scene/SceneStack.h"

#include "detail/Guard.h"

#include <primitives/Primitives.h>
#include <utils/Log.h>

#include <algorithm>
#include <cstddef>
#include <utility>

namespace app {

	namespace {
		class FactoryBuilder final : public detail::SceneBuilder {
		  public:
			explicit FactoryBuilder(SceneStack::Factory sceneFactory)
				: factory(std::move(sceneFactory)) {}

			std::unique_ptr<Scene> build() override { return factory(); }

		  private:
			SceneStack::Factory factory;
		};
	} // namespace

	void Scene::finishWith(std::unique_ptr<detail::ResultBox> result) {
		if (stack == nullptr) {
			LOG_ERROR(Engine, "%s finished, but it isn't on a scene stack; its result is dropped", name());
			return;
		}
		stack->queueFinish(stackId, std::move(result));
	}

	SceneStack::SceneStack(App& owner)
		: app(owner) {}

	SceneStack::~SceneStack() = default;

	// --- Registering ---

	void SceneStack::add(std::string name, Factory factory) {
		addNamed(std::move(name), std::move(factory), nullptr);
	}

	void SceneStack::addNamed(std::string name, Factory factory, const void* type) {
		if (name.empty() || !factory) {
			LOG_ERROR(Engine, "SceneStack::add needs a name and a factory");
			return;
		}
		if (factories.contains(name)) {
			LOG_WARNING(Engine, "Scene '%s' added twice; the later factory replaces the earlier", name.c_str());
		}
		std::erase_if(typeNames, [&](const auto& entry) { return entry.second == name; });
		if (type != nullptr) {
			std::erase_if(typeNames, [&](const auto& entry) { return entry.first == type; });
			typeNames.emplace_back(type, name);
		}
		factories.insert_or_assign(std::move(name), std::move(factory));
	}

	std::string SceneStack::nameFor(const void* type) const {
		const auto it = std::ranges::find(typeNames, type, &std::pair<const void*, std::string>::first);
		return it != typeNames.end() ? it->second : std::string{};
	}

	// --- Asking for changes ---

	void SceneStack::queue(Change change) {
		if (change.kind == ChangeKind::Replace || change.kind == ChangeKind::Reset) {
			pending.clear();
		}
		pending.push_back(std::move(change));
	}

	void SceneStack::queueFinish(std::uint64_t finisher, std::unique_ptr<detail::ResultBox> result) {
		pending.push_back({.kind = ChangeKind::Finish, .finisher = finisher, .result = std::move(result)});
	}

	void SceneStack::pop() {
		pending.push_back({.kind = ChangeKind::Pop});
	}

	bool SceneStack::switchTo(std::string_view name) {
		const auto it = factories.find(name);
		if (it == factories.end()) {
			LOG_ERROR(Engine, "Unknown scene: %.*s", static_cast<int>(name.size()), name.data());
			return false;
		}
		queue({.kind = ChangeKind::Reset, .builder = std::make_unique<FactoryBuilder>(it->second), .name = it->first});
		return true;
	}

	bool SceneStack::reload() {
		const auto it = entries.empty() ? factories.end() : factories.find(entries.back().name);
		if (it == factories.end()) {
			LOG_WARNING(Engine, "SceneStack::reload: the top scene has no name to rebuild it by");
			return false;
		}
		queue({.kind = ChangeKind::Replace, .builder = std::make_unique<FactoryBuilder>(it->second), .name = it->first});
		return true;
	}

	Scene* SceneStack::top() {
		return entries.empty() ? nullptr : entries.back().scene.get();
	}

	std::string_view SceneStack::topName() const {
		return entries.empty() ? std::string_view{} : std::string_view(entries.back().name);
	}

	// --- Applying them ---

	void SceneStack::apply(float liveAlpha) {
		// What scenes ask for while these apply waits for the next frame.
		std::vector<Change> changes = std::exchange(pending, {});
		for (Change& change : changes) {
			switch (change.kind) {
				case ChangeKind::Push:
					pushBuilt(change, liveAlpha);
					break;
				case ChangeKind::Pop:
					popTop();
					break;
				case ChangeKind::Finish:
					finishTop(change);
					break;
				case ChangeKind::Replace:
					replaceTop(change);
					break;
				case ChangeKind::Reset:
					resetTo(change);
					break;
			}
		}
	}

	// What scenes ask for as they exit is dropped with the rest.
	void SceneStack::exitAll() {
		while (!entries.empty()) {
			exitTop();
		}
		pending.clear();
	}

	bool SceneStack::holds(std::uint64_t id) const {
		return std::ranges::any_of(entries, [&](const Entry& entry) { return entry.scene->stackId == id; });
	}

	void SceneStack::resized(const Renderer::Viewport& viewport) {
		for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
			Scene& scene = *it->scene;
			detail::guard(scene.name(), "onResize", [&] { scene.onResize(viewport); });
		}
	}

	// Each scene is its own sort domain, so what a covered scene draws at a high z stays under the
	// scene over it.
	void SceneStack::render(float liveAlpha) {
		std::size_t first = entries.size();
		while (first > 0) {
			Scene& scene = *entries[first - 1].scene;
			bool   opaque = true;
			detail::guard(scene.name(), "opaque", [&] { opaque = scene.opaque(); });
			--first;
			if (opaque) {
				break;
			}
		}
		for (std::size_t i = first; i < entries.size(); ++i) {
			Scene&		scene = *entries[i].scene;
			const float alpha = i + 1 == entries.size() ? liveAlpha : entries[i].coveredAlpha;
			Renderer::Primitives::sortBarrier();
			detail::guard(scene.name(), "render", [&] { scene.render(alpha); });
		}
	}

	void SceneStack::pushBuilt(Change& change, float liveAlpha) {
		if (change.requester != 0 && !holds(change.requester)) {
			LOG_WARNING(Engine, "A push with a result callback was dropped: the scene that asked for it has left the stack");
			return;
		}
		std::unique_ptr<Scene> scene = build(*change.builder, change.name);
		if (scene == nullptr) {
			return;
		}
		if (!entries.empty()) {
			Entry& covered = entries.back();
			covered.coveredAlpha = liveAlpha;
			detail::guard(covered.scene->name(), "onCovered", [&] { covered.scene->onCovered(); });
		}
		enter(std::move(scene), std::move(change.name), std::move(change.onResult));
	}

	void SceneStack::popTop() {
		if (entries.empty()) {
			LOG_WARNING(Engine, "SceneStack::pop with no scene on the stack");
			return;
		}
		exitTop();
		uncoverTop();
	}

	void SceneStack::finishTop(Change& change) {
		if (entries.empty() || entries.back().scene->stackId != change.finisher) {
			LOG_ERROR(Engine, "A scene finished that isn't the top one; it stays, and its result is dropped");
			return;
		}
		// Off the stack before its callback runs, so top() is the pusher, but alive until after, so a
		// result that points into the scene is still good in the callback.
		Entry finished = exitTop();
		if (finished.onResult != nullptr) {
			detail::guard(finished.scene->name(), "onResult", [&] { finished.onResult->deliver(*change.result); });
		}
		finished.scene.reset();
		uncoverTop();
	}

	void SceneStack::replaceTop(Change& change) {
		if (!entries.empty()) {
			exitTop();
		}
		std::unique_ptr<Scene> scene = build(*change.builder, change.name);
		if (scene == nullptr) {
			uncoverTop();
			return;
		}
		enter(std::move(scene), std::move(change.name), nullptr);
	}

	void SceneStack::resetTo(Change& change) {
		while (!entries.empty()) {
			exitTop();
		}
		std::unique_ptr<Scene> scene = build(*change.builder, change.name);
		if (scene != nullptr) {
			enter(std::move(scene), std::move(change.name), nullptr);
		}
	}

	std::unique_ptr<Scene> SceneStack::build(detail::SceneBuilder& builder, const std::string& name) {
		const char*			   label = name.empty() ? "Scene" : name.c_str();
		std::unique_ptr<Scene> scene;
		detail::guard(label, "build", [&] { scene = builder.build(); });
		if (scene == nullptr) {
			LOG_ERROR(Engine, "Could not build scene %s", label);
		}
		return scene;
	}

	void SceneStack::enter(std::unique_ptr<Scene> scene, std::string name, std::unique_ptr<detail::ResultCallback> onResult) {
		Scene& entered = *scene;
		entered.stack = this;
		entered.stackId = nextId++;
		entries.push_back({.scene = std::move(scene), .name = std::move(name), .onResult = std::move(onResult)});
		LOG_INFO(Engine, "Entering scene: %s", entered.name());
		detail::guard(entered.name(), "onEnter", [&] { entered.onEnter(app); });
	}

	// The scene exits while still on top, then leaves the stack; it is destroyed with the entry this
	// returns, which a caller that ignores it drops at once.
	SceneStack::Entry SceneStack::exitTop() {
		Scene& exiting = *entries.back().scene;
		LOG_INFO(Engine, "Exiting scene: %s", exiting.name());
		detail::guard(exiting.name(), "onExit", [&] { exiting.onExit(); });
		exiting.stack = nullptr;
		Entry entry = std::move(entries.back());
		entries.pop_back();
		return entry;
	}

	void SceneStack::uncoverTop() {
		if (entries.empty()) {
			return;
		}
		Scene& uncovered = *entries.back().scene;
		detail::guard(uncovered.name(), "onUncovered", [&] { uncovered.onUncovered(); });
	}

} // namespace app
