#pragma once

// The sandbox's scene picker, an overlay with a ui context of its own. Closed, it is a tab in the
// window's bottom-right corner; open, a panel of ListRows stands above the tab, one a scene, the
// running one selected. F1 or a press on the tab opens and closes it; a row switches to its scene
// and closes the list; Escape closes it. While it's open the arrows move focus along the rows and
// Enter picks one. Its state, with the bounds of the tab and of each row, is /api/state?what=picker.
//
// It takes what its ui takes: a press on the tab or the open list, with the rest of that gesture,
// and the wheel over them; every key while the list is open; and F1.

#include <application/App.h>
#include <scene/Overlay.h>
#include <ui/Context.h>
#include <ui/components/Button.h>
#include <ui/components/ListRow.h>

#include <string>
#include <vector>

namespace sandbox {

	class Picker final : public app::Overlay {
	  public:
		struct Args {
			std::vector<std::string> scenes; // in the order listed, each a name the App's stack has
			bool					 open = false;
		};

		Picker(app::App& owningApp, Args args);

		bool handleEvent(const foundation::InputEvent& event) override;
		void update(const app::FrameContext& frame) override;
		void render() override;
		void onResize(const Renderer::Viewport& viewport) override;

	  private:
		void		setOpen(bool value);
		void		choose(std::size_t index);
		std::size_t current() const; // the running scene's row; scenes.size() for none
		std::string stateJson() const;

		app::App&						  owner;
		std::vector<std::string>		  scenes;
		bool							  open = false;
		ui::Context						  context;
		ui::Ref<ui::Rectangle>			  panel;
		ui::Ref<ui::Button>				  tab;
		std::vector<ui::Ref<ui::ListRow>> rows;
		app::DevServer::Registration	  state;
	};

} // namespace sandbox
