#pragma once

// The sandbox's scene picker, an overlay drawn with Primitives (the harness's ui comes with salal's ui
// in phase 3). Closed, it is a tab in the window's bottom-right corner; open, a list of every scene
// stands above the tab with the running one marked. F1 or a click on the tab opens and closes it.
// While it is open, Up and Down or the pointer over a row move the selection, Enter or a click on a row
// switches to that scene and closes the list, and Escape closes it. Its state, with the bounds of the
// tab and of each row, is /api/state?what=picker.
//
// It consumes by press, not by place. A button pressed over the tab or the open list is the picker's,
// and so is its release wherever it lands; a click acts when it is released over what it was pressed
// on. A press that began in the scene keeps its release, over the picker too. A key the picker took
// keeps its repeats and its release, and the wheel over the tab or the open list is the picker's.
// Consuming only stops handleEvent below it: FrameContext::input and App::input().events() still show
// every key and button the picker used.

#include <application/App.h>
#include <graphics/Rect.h>
#include <input/InputTypes.h>
#include <math/Types.h>
#include <scene/Overlay.h>

#include <bitset>
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
		// Where things are, in logical pixels, for the window as it is now.
		Foundation::Rect tabBounds() const;
		Foundation::Rect panelBounds() const;
		Foundation::Rect rowBounds(std::size_t index) const;
		// The row under `point` while the list is open; scenes.size() for none.
		std::size_t rowAt(const Foundation::Vec2& point) const;
		// Over the tab, or over the list while it is open.
		bool over(const Foundation::Vec2& point) const;

		bool keyDown(const foundation::InputEvent& event);
		bool keyUp(const foundation::InputEvent& event);
		bool pointerDown(const foundation::InputEvent& event);
		bool pointerUp(const foundation::InputEvent& event);
		// A key going down: true when the picker acted on it.
		bool		pressKey(foundation::Key key);
		void		moveSelection(bool up);
		void		setOpen(bool value);
		void		choose(std::size_t index);
		std::size_t current() const; // the running scene's row; scenes.size() for none
		std::string stateJson() const;

		using Keys = std::bitset<static_cast<std::size_t>(foundation::Key::Count)>;
		using Buttons = std::bitset<static_cast<std::size_t>(foundation::MouseButton::Count)>;

		app::App&				 owner;
		std::vector<std::string> scenes;
		bool					 open = false;
		std::size_t				 selected = 0;
		// Went down on the picker and haven't come up yet.
		Keys	heldKeys;
		Buttons heldButtons;
		// What the left button's press, if the picker has it, was on: the tab, or a row (scenes.size() for none).
		bool						 pressedTab = false;
		std::size_t					 pressedRow = 0;
		app::DevServer::Registration state;
	};

} // namespace sandbox
