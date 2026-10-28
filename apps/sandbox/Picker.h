#pragma once

// The sandbox's scene picker, an overlay drawn with Primitives (the harness's ui comes with salal's ui
// in phase 3). Closed, it is a tab in the window's bottom-right corner; open, a list of every scene
// stands above the tab with the running one marked. F1 or a click on the tab opens and closes it.
// While it is open, Up and Down or the pointer over a row move the selection, Enter or a click on a row
// switches to that scene and closes the list, and Escape closes it. Its state, with the bounds of the
// tab and of each row, is /api/state?what=picker.

#include <application/App.h>
#include <graphics/Rect.h>
#include <input/InputTypes.h>
#include <scene/Overlay.h>

#include <cstddef>
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
		void render() override;

	  private:
		// Where everything is in logical pixels, for the window as it is now.
		struct Layout {
			Foundation::Rect			  tab;
			Foundation::Rect			  panel;
			std::vector<Foundation::Rect> rows;
		};

		Layout		layout() const;
		bool		handleKey(const foundation::InputEvent& event);
		bool		handlePointer(const foundation::InputEvent& event);
		void		setOpen(bool value);
		void		choose(std::size_t index);
		std::size_t current() const; // the running scene's row; scenes.size() for none
		std::string stateJson() const;

		app::App&					 owner;
		std::vector<std::string>	 scenes;
		bool						 open = false;
		std::size_t					 selected = 0;
		app::DevServer::Registration state;
	};

} // namespace sandbox
