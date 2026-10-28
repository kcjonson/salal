#include "clipboard/Clipboard.h"

#include "platform/Platform.h"

namespace app {

	Clipboard::Clipboard(Platform& clipboardPlatform)
		: platform(clipboardPlatform) {}

	std::string Clipboard::getText() const {
		return platform.clipboardText();
	}

	void Clipboard::setText(const std::string& text) {
		platform.setClipboardText(text);
	}

} // namespace app
