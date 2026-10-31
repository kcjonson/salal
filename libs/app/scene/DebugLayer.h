#pragma once

// Above the scene stack and across scene changes: dev menus, debug HUDs. The App owns its debug layers
// (App::addDebugLayer). They get each event before the top scene, the last added first; they update
// after the top scene and render above the stack, in the order added. Pause stops their update
// as it does the scenes'.

#include "scene/Scene.h"

#include <CoordinateSystem/CoordinateSystem.h>
#include <input/InputTypes.h>

namespace app {

	class DebugLayer {
	  public:
		virtual ~DebugLayer() = default;

		// True when the debug layer consumed the event, so nothing under it sees it.
		virtual bool handleEvent(const foundation::InputEvent& /*event*/) { return false; }
		virtual void update(const FrameContext& /*frame*/) {}
		virtual void render() = 0;
		virtual void onResize(const Renderer::Viewport& /*viewport*/) {}
		// At App::shutdown, after the scenes have exited.
		virtual void onExit() {}
	};

} // namespace app
