#pragma once

// A single line of editable text: its string, a caret and an optional selection, all byte offsets
// on UTF-8 character boundaries. The string is kept well-formed, each ill-formed sequence given to it
// stored as U+FFFD, so no edit can join bytes into a character a caret sits inside. TextInput's
// editing, apart from the tree so it tests alone; the rules are worldsim's TextInput's
// (libs/ui/components/TextInput, a4226e17).

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace ui {

	class TextEditor {
	  public:
		explicit TextEditor(std::string initial = {}, std::size_t maxLength = 0);

		[[nodiscard]] const std::string& text() const { return content; }
		[[nodiscard]] std::size_t		 caret() const { return caretAt; }
		[[nodiscard]] bool				 hasSelection() const { return anchor.has_value() && *anchor != caretAt; }
		// The selection's [start, end), or the caret twice.
		[[nodiscard]] std::pair<std::size_t, std::size_t> selection() const;
		[[nodiscard]] std::string						  selectedText() const;

		// Replaces it, the caret at the end and nothing selected.
		void setText(std::string value);
		// Over the selection, or at the caret; past maxLength characters, cut short. False if nothing
		// changed.
		bool insert(std::string_view typed);
		bool backspace();
		bool deleteForward();
		// The selection's text, which goes; false if none.
		bool cut(std::string& removed);

		// Moves by a character, or a word with `word`; `select` extends the selection, otherwise a
		// selection collapses to its edge in that direction.
		void left(bool select, bool word);
		void right(bool select, bool word);
		void home(bool select);
		void end(bool select);
		void selectAll();
		// To a byte offset, snapped to a character boundary.
		void moveTo(std::size_t offset, bool select);

	  private:
		void		place(std::size_t offset, bool select);
		bool		removeSelection();
		std::size_t wordLeft() const;
		std::size_t wordRight() const;

		std::string				   content;
		std::size_t				   caretAt = 0;
		std::optional<std::size_t> anchor;
		std::size_t				   maxLength;
	};

} // namespace ui
