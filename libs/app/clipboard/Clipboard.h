#pragma once

// The system clipboard's text: GLFW's on the GLFW platform, an in-memory string on the headless one.
// Concrete, since app names no ui type; worldsim adapts it to UI::IClipboard until the interface
// moves into salal's ui, and the names match that interface's.

#include <string>

namespace app {

	class Platform;

	class Clipboard {
	  public:
		explicit Clipboard(Platform& platform);

		// Empty when the clipboard holds no text.
		std::string getText() const;
		void		setText(const std::string& text);

	  private:
		Platform& platform;
	};

} // namespace app
