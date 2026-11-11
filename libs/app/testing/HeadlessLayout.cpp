#include "testing/HeadlessLayout.h"

#include <ui/Context.h>
#include <ui/debug/LayoutLint.h>

#include <cmath>
#include <cstddef>
#include <sstream>

namespace app {

	namespace {

		std::string segment(const ui::Component& component, std::size_t index) {
			if (!component.getId().empty()) {
				return component.getId();
			}
			return std::string(component.typeName()) + "[" + std::to_string(index) + "]";
		}

		void collectBars(
			const ui::Context&		  context,
			ui::ComponentId			  id,
			std::size_t				  index,
			const std::string&		  parent,
			std::vector<std::string>& out
		) {
			const ui::Component* component = context.find(id);
			if (component == nullptr || !component->isShown()) {
				return;
			}
			const std::string path = parent.empty() ? segment(*component, index) : parent + "/" + segment(*component, index);
			if (component->showsScrollbar()) {
				out.push_back(path);
			}
			const std::vector<ui::ComponentId>& children = component->getChildren();
			for (std::size_t i = 0; i < children.size(); ++i) {
				collectBars(context, children[i], i, path, out);
			}
		}

	} // namespace

	std::string lintAt(App& app, HeadlessPlatform& platform, int width, int height) {
		const float ratio = platform.viewport().pixelRatio();
		platform.resize(
			static_cast<int>(std::lround(static_cast<float>(width) / ratio)),
			static_cast<int>(std::lround(static_cast<float>(height) / ratio))
		);
		app.tick();
		app.tick();
		std::ostringstream		 found;
		const Renderer::Viewport window = app.viewport();
		if (window.framebufferWidth != width || window.framebufferHeight != height) {
			found << "the window is " << window.framebufferWidth << "x" << window.framebufferHeight << ", not the " << width << "x"
				  << height << " asked for\n";
			return found.str();
		}
		Scene* top = app.scenes().top();
		if (top == nullptr || !top->hasUi()) {
			return {};
		}
		const ui::Context&	 context = top->ui();
		const ui::LintResult result = ui::lint(context, context.getCanvas());
		for (const ui::LintViolation& violation : result.violations) {
			const Foundation::Rect& box = violation.bounds;
			found << ui::lintRuleName(violation.rule) << " " << violation.path << " at " << box.x << "," << box.y << " " << box.width << "x"
				  << box.height << (violation.otherPath.empty() ? "" : " against " + violation.otherPath) << "\n";
		}
		return found.str();
	}

	std::vector<std::string> scrollbarsShown(App& app) {
		std::vector<std::string> shown;
		Scene*					 top = app.scenes().top();
		if (top == nullptr || !top->hasUi()) {
			return shown;
		}
		const ui::Context&					context = top->ui();
		const std::vector<ui::ComponentId>& roots = context.getRoots();
		for (std::size_t i = 0; i < roots.size(); ++i) {
			collectBars(context, roots[i], i, "", shown);
		}
		return shown;
	}

} // namespace app
