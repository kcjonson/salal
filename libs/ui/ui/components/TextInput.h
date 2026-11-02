#pragma once

// A single-line text field, after worldsim's TextInput (libs/ui/components/TextInput, a4226e17):
// its text clipped to its box and scrolled to keep the caret in view, a placeholder while empty, a
// selection, and the caret while it has focus. It edits with the keyboard (characters, Backspace
// and Delete, the arrows, Home and End, Shift to select, Ctrl for a word, Ctrl+A, and Ctrl+C, X and
// V through the scene's clipboard) and with the pointer (press to place the caret, drag to select).
// Up and down are left to directional focus; Enter calls onSubmit. Each edit calls onChange. Its
// theme style is ui::styles::textInput.

#include "ui/components/Control.h"
#include "ui/components/TextEditor.h"
#include "ui/shapes/Text.h"

#include <functional>
#include <string>

namespace ui {

	class TextInput : public Control {
	  public:
		struct Args {
			UI_COMMON_ARGS
			std::string								text;
			std::string								placeholder;
			std::size_t								maxLength = 0; // characters; 0: no limit
			std::function<void(const std::string&)> onChange;
			std::function<void(const std::string&)> onSubmit;
		};

		explicit TextInput(const Args& args);

		[[nodiscard]] const std::string& getText() const { return editor.text(); }
		// Replaces the text without calling onChange.
		void							setText(std::string value);
		[[nodiscard]] const TextEditor& getEditor() const { return editor; }
		void							setStyle(const Style& declared) override;

		[[nodiscard]] const char* typeName() const override { return "TextInput"; }
		[[nodiscard]] StyleId	  themeStyle() const override;
		void					  describe(nlohmann::json& node) const override;

	  protected:
		void onMount() override;
		void render() override;
		void handlePointer(PointerEvent& event) override;
		void handleKey(KeyEvent& event) override;

	  private:
		[[nodiscard]] float		  widthOf(std::size_t bytes) const;
		[[nodiscard]] std::size_t offsetAt(float x) const;
		void					  edited(bool changed);
		void					  show();

		TextEditor								editor;
		std::string								placeholder;
		std::function<void(const std::string&)> onChange;
		std::function<void(const std::string&)> onSubmit;
		float									scroll = 0.0F; // px the text is moved left to keep the caret in view
		bool									caretShown = false;
		Ref<Component>							field;
		Ref<Rectangle>							selectionBox;
		Ref<Text>								shownText;
		Ref<Text>								placeholderText;
		Ref<Rectangle>							caretBar;
	};

} // namespace ui
