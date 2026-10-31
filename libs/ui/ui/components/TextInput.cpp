#include "ui/components/TextInput.h"

#include "ui/Context.h"
#include "ui/theme/Tokens.h"
#include "ui/theme/Variants.h"

#include <font/FontRenderer.h>
#include <primitives/Primitives.h>
#include <utils/Utf8.h>

#include <algorithm>

namespace ui {

	namespace {
		constexpr float			kPadX = 8.0F;
		constexpr float			kWidth = 200.0F;
		constexpr float			kBaseFontSize = 16.0F; // FontRenderer's scale 1
		constexpr float			kCaretWidth = 1.0F;
		constexpr float			kCaretHeight = 1.3F; // of the font size
		constexpr float			kSelectionAlpha = 0.35F;
		const Foundation::Color kClear(0.0F, 0.0F, 0.0F, 0.0F);
	} // namespace

	TextInput::TextInput(const Args& args)
		: editor(args.text, args.maxLength),
		  placeholder(args.placeholder),
		  onChange(args.onChange),
		  onSubmit(args.onSubmit) {}

	StyleId TextInput::themeStyle() const {
		return styles::textInput.theme;
	}

	void TextInput::onMount() {
		if (!getCursor()) {
			setCursor(Cursor::Text);
		}
		Control::onMount();
		Layout row = getLayout();
		row.display = Display::Flex;
		row.flexDirection = FlexDirection::Row;
		row.alignItems = AlignItems::Center;
		row.padding = {0.0F, kPadX};
		setLayout(row);
		const float height = sizeMetrics(getContext()->getTheme(), ControlSize::Md).controlHeight;
		setSize(
			{getSizeSpec().width.isAuto() ? Length(kWidth) : getSizeSpec().width,
			 getSizeSpec().height.isAuto() ? Length(height) : getSizeSpec().height}
		);
		field = addChild<Component>({.size = {kAuto, percent(100.0F)}, .layout = {.flexGrow = 1.0F}, .overflow = Overflow::Clip});
		selectionBox = field.addChild<Rectangle>({});
		shownText = field.addChild<Text>({.size = {kAuto, percent(100.0F)}, .text = editor.text()});
		placeholderText = field.addChild<Text>({.size = {kAuto, percent(100.0F)}, .text = placeholder});
		caretBar = field.addChild<Rectangle>({.style = {.backgroundColor = kClear}});
		show();
	}

	void TextInput::setStyle(const Style& declared) {
		Control::setStyle(declared);
		show();
	}

	void TextInput::setText(std::string value) {
		editor.setText(std::move(value));
		scroll = 0.0F;
		show();
	}

	float TextInput::widthOf(std::size_t bytes) const {
		const Renderer::FontRenderer* fonts = Renderer::Primitives::getFontRenderer();
		if (fonts == nullptr || bytes == 0) {
			return 0.0F;
		}
		const float scale = style.fontSize.value_or(kBaseFontSize) / kBaseFontSize;
		return fonts
			->MeasureText(
				editor.text().substr(0, bytes),
				scale,
				style.fontFamily.value_or(Renderer::FontFamily::Default),
				style.letterSpacing.value_or(0.0F)
			)
			.x;
	}

	std::size_t TextInput::offsetAt(float x) const {
		const std::string& text = editor.text();
		std::size_t		   previous = 0;
		float			   previousWidth = 0.0F;
		for (std::size_t at = 0; at < text.size();) {
			at = foundation::UTF8::nextCharacterBoundary(text, at);
			const float width = widthOf(at);
			if (width >= x) {
				return x - previousWidth < width - x ? previous : at;
			}
			previous = at;
			previousWidth = width;
		}
		return text.size();
	}

	void TextInput::edited(bool changed) {
		show();
		if (changed && onChange) {
			onChange(editor.text());
		}
	}

	void TextInput::show() {
		if (getContext() == nullptr || shownText.get() == nullptr) {
			return;
		}
		const Theme& theme = getContext()->getTheme();
		const float	 fontSize = style.fontSize.value_or(kBaseFontSize);
		const Style	 textStyle{
			 .color = style.color,
			 .fontFamily = style.fontFamily,
			 .fontSize = fontSize,
			 .verticalAlign = Foundation::VerticalAlign::Middle,
			 .letterSpacing = style.letterSpacing,
		 };

		// Scroll so the caret stays inside the field, once layout has given the field a width.
		const float visible = field.get()->getSize().x;
		float		caretX = widthOf(editor.caret()) - scroll;
		if (visible > 0.0F) {
			if (caretX < 0.0F) {
				scroll += caretX;
			} else if (caretX > visible - kCaretWidth) {
				scroll += caretX - (visible - kCaretWidth);
			}
			scroll = std::max(0.0F, scroll);
			caretX = widthOf(editor.caret()) - scroll;
		}

		shownText.get()->setStyle(textStyle);
		shownText.get()->setText(editor.text());
		shownText.get()->setPosition({-scroll, 0.0F});
		placeholderText.get()->setStyle(textStyle.extend({.color = theme.get(tokens::color::text::disabled)}));
		placeholderText.get()->setVisible(editor.text().empty());

		const float fieldHeight =
			field.get()->getSize().y > 0.0F ? field.get()->getSize().y : sizeMetrics(theme, ControlSize::Md).controlHeight;
		const float caretHeight = fontSize * kCaretHeight;
		caretBar.get()->setPosition({caretX, (fieldHeight - caretHeight) * 0.5F});
		caretBar.get()->setSize({kCaretWidth, caretHeight});

		const auto [start, finish] = editor.selection();
		Foundation::Color selection = toneColors(theme, Tone::Accent).color;
		selection.a *= kSelectionAlpha;
		selectionBox.get()->setVisible(editor.hasSelection());
		selectionBox.get()->setStyle({.backgroundColor = selection});
		selectionBox.get()->setPosition({widthOf(start) - scroll, (fieldHeight - caretHeight) * 0.5F});
		selectionBox.get()->setSize({widthOf(finish) - widthOf(start), caretHeight});
	}

	// The caret shows only while it has focus; checked as it draws, since focus has no hook of its own.
	void TextInput::render() {
		if (isFocused() != caretShown && caretBar.get() != nullptr) {
			caretShown = isFocused();
			caretBar.get()->setStyle({.backgroundColor = caretShown ? style.color.value_or(Foundation::Color::white()) : kClear});
		}
		Control::render();
	}

	void TextInput::handlePointer(PointerEvent& event) {
		const Component* area = field.get();
		if (area == nullptr || !isEnabledInTree()) {
			return;
		}
		const float x = event.local.x - area->getBounds().x + scroll;
		if (event.type == PointerEvent::Type::Down && event.button == foundation::MouseButton::Left) {
			capturePointer();
			editor.moveTo(offsetAt(x), (event.mods & foundation::ModShift) != 0);
			show();
			event.consume();
		} else if (event.type == PointerEvent::Type::Move && hasPointerCapture()) {
			editor.moveTo(offsetAt(x), true);
			show();
			event.consume();
		} else if (event.type == PointerEvent::Type::Up && hasPointerCapture()) {
			releasePointer();
			event.consume();
		}
	}

	void TextInput::handleKey(KeyEvent& event) {
		if (!isEnabledInTree()) {
			return;
		}
		const bool shift = (event.mods & foundation::ModShift) != 0;
		const bool command = (event.mods & (foundation::ModControl | foundation::ModSuper)) != 0;
		if (event.type == KeyEvent::Type::Char) {
			if (event.codepoint >= 0x20 && !command) {
				edited(editor.insert(foundation::UTF8::encode(event.codepoint)));
				event.consume();
			}
			return;
		}
		if (event.type != KeyEvent::Type::Down) {
			return;
		}
		const Context::Clipboard& clipboard = getContext()->clipboard();
		using foundation::Key;
		switch (event.key) {
			case Key::Left:
				editor.left(shift, command);
				show();
				break;
			case Key::Right:
				editor.right(shift, command);
				show();
				break;
			case Key::Home:
				editor.home(shift);
				show();
				break;
			case Key::End:
				editor.end(shift);
				show();
				break;
			case Key::Backspace:
				edited(editor.backspace());
				break;
			case Key::Delete:
				edited(editor.deleteForward());
				break;
			case Key::Enter:
			case Key::KpEnter:
				if (onSubmit) {
					onSubmit(editor.text());
				}
				break;
			case Key::Space:
				break; // the character types it; this keeps Space from activating
			case Key::A:
				if (!command) {
					return;
				}
				editor.selectAll();
				show();
				break;
			case Key::C:
			case Key::X: {
				if (!command) {
					return;
				}
				if (editor.hasSelection() && clipboard.write) {
					clipboard.write(editor.selectedText());
				}
				if (event.key == Key::X) {
					std::string removed;
					edited(editor.cut(removed));
				}
				break;
			}
			case Key::V: {
				if (!command) {
					return;
				}
				std::string pasted = clipboard.read ? clipboard.read() : std::string();
				std::erase_if(pasted, [](char c) { return c == '\n' || c == '\r'; });
				edited(editor.insert(pasted));
				break;
			}
			default:
				return;
		}
		event.consume();
	}

} // namespace ui
