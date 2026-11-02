#include "ui/components/TextEditor.h"

#include <utils/Utf8.h>

#include <algorithm>
#include <cctype>

namespace ui {

	namespace UTF8 = foundation::UTF8;

	namespace {
		bool isWordByte(unsigned char byte) {
			return std::isalnum(byte) != 0 || byte == '_' || byte >= 0x80;
		}
	} // namespace

	TextEditor::TextEditor(std::string initial, std::size_t maxLength)
		: maxLength(maxLength) {
		setText(std::move(initial));
	}

	std::pair<std::size_t, std::size_t> TextEditor::selection() const {
		if (!hasSelection()) {
			return {caretAt, caretAt};
		}
		return {std::min(*anchor, caretAt), std::max(*anchor, caretAt)};
	}

	std::string TextEditor::selectedText() const {
		const auto [start, finish] = selection();
		return content.substr(start, finish - start);
	}

	void TextEditor::setText(std::string value) {
		content = UTF8::sanitize(value);
		if (maxLength > 0 && UTF8::characterCount(content) > maxLength) {
			std::size_t at = 0;
			for (std::size_t kept = 0; kept < maxLength && at < content.size(); ++kept) {
				at = UTF8::nextCharacterBoundary(content, at);
			}
			content.resize(at);
		}
		caretAt = content.size();
		anchor.reset();
	}

	bool TextEditor::removeSelection() {
		if (!hasSelection()) {
			anchor.reset();
			return false;
		}
		const auto [start, finish] = selection();
		content.erase(start, finish - start);
		caretAt = start;
		anchor.reset();
		return true;
	}

	bool TextEditor::insert(std::string_view typed) {
		bool		changed = removeSelection();
		std::string adding = UTF8::sanitize(typed);
		if (maxLength > 0) {
			const std::size_t room = maxLength > UTF8::characterCount(content) ? maxLength - UTF8::characterCount(content) : 0;
			std::size_t		  at = 0;
			for (std::size_t kept = 0; kept < room && at < adding.size(); ++kept) {
				at = UTF8::nextCharacterBoundary(adding, at);
			}
			adding.resize(at);
		}
		if (adding.empty()) {
			return changed;
		}
		content.insert(caretAt, adding);
		caretAt += adding.size();
		return true;
	}

	bool TextEditor::backspace() {
		if (removeSelection()) {
			return true;
		}
		if (caretAt == 0) {
			return false;
		}
		const std::size_t start = UTF8::previousCharacterBoundary(content, caretAt);
		content.erase(start, caretAt - start);
		caretAt = start;
		return true;
	}

	bool TextEditor::deleteForward() {
		if (removeSelection()) {
			return true;
		}
		if (caretAt >= content.size()) {
			return false;
		}
		content.erase(caretAt, UTF8::nextCharacterBoundary(content, caretAt) - caretAt);
		return true;
	}

	bool TextEditor::cut(std::string& removed) {
		removed = selectedText();
		return removeSelection();
	}

	void TextEditor::place(std::size_t offset, bool select) {
		if (select) {
			if (!anchor) {
				anchor = caretAt;
			}
		} else {
			anchor.reset();
		}
		caretAt = std::min(offset, content.size());
	}

	std::size_t TextEditor::wordLeft() const {
		std::size_t at = caretAt;
		while (at > 0 && !isWordByte(static_cast<unsigned char>(content[at - 1]))) {
			--at;
		}
		while (at > 0 && isWordByte(static_cast<unsigned char>(content[at - 1]))) {
			--at;
		}
		return at;
	}

	std::size_t TextEditor::wordRight() const {
		std::size_t at = caretAt;
		while (at < content.size() && !isWordByte(static_cast<unsigned char>(content[at]))) {
			++at;
		}
		while (at < content.size() && isWordByte(static_cast<unsigned char>(content[at]))) {
			++at;
		}
		return at;
	}

	void TextEditor::left(bool select, bool word) {
		if (!select && hasSelection() && !word) {
			place(selection().first, false);
			return;
		}
		place(word ? wordLeft() : (caretAt > 0 ? UTF8::previousCharacterBoundary(content, caretAt) : 0), select);
	}

	void TextEditor::right(bool select, bool word) {
		if (!select && hasSelection() && !word) {
			place(selection().second, false);
			return;
		}
		place(word ? wordRight() : (caretAt < content.size() ? UTF8::nextCharacterBoundary(content, caretAt) : caretAt), select);
	}

	void TextEditor::home(bool select) {
		place(0, select);
	}

	void TextEditor::end(bool select) {
		place(content.size(), select);
	}

	void TextEditor::selectAll() {
		anchor = 0;
		caretAt = content.size();
	}

	void TextEditor::moveTo(std::size_t offset, bool select) {
		place(UTF8::characterStart(content, offset), select);
	}

} // namespace ui
