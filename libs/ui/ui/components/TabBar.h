#pragma once

// A row of tabs over a hairline, after worldsim's TabBar (libs/ui/components/tabbar, a4226e17): the
// selected tab's label in the primary text colour over an accent underline, the others secondary.
// The bar is one Tab stop whose arrows move among the tabs; a press selects a tab and calls
// onSelect with its id. A tab is ui::styles::tab.

#include "ui/components/Control.h"

#include <functional>
#include <string>
#include <vector>

namespace ui {

	struct Tab {
		std::string id;
		std::string label;
	};

	class TabBar : public Component {
	  public:
		struct Args {
			UI_COMMON_ARGS
			std::vector<Tab>						tabs;
			std::string								selectedId; // empty: the first tab
			std::function<void(const std::string&)> onSelect;
		};

		explicit TabBar(const Args& args);

		[[nodiscard]] const std::string& getSelectedId() const { return selectedId; }
		// Selects without calling onSelect.
		void setSelectedId(std::string id);
		void setStyle(const Style& declared) override;
		// A tab, by the bar.
		void choose(const std::string& id);

		[[nodiscard]] const char* typeName() const override { return "TabBar"; }
		void					  describe(nlohmann::json& node) const override;

	  protected:
		void onMount() override;

	  private:
		void show();

		std::vector<Tab>						tabs;
		std::string								selectedId;
		std::function<void(const std::string&)> onSelect;
		std::vector<Ref<Control>>				tabControls;
	};

} // namespace ui
