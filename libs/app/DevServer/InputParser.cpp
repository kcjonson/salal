#include "DevServer/InputParser.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string_view>
#include <utility>

namespace app::detail {

	namespace {

		using foundation::DeviceId;
		using foundation::DeviceKind;
		using foundation::InputEvent;
		using foundation::Key;
		using Type = InputEvent::Type;

		static_assert(static_cast<int>(Key::Z) - static_cast<int>(Key::A) == 25, "A to Z are contiguous");
		static_assert(static_cast<int>(Key::Num9) - static_cast<int>(Key::Num0) == 9, "Num0 to Num9 are contiguous");
		static_assert(static_cast<int>(Key::F12) - static_cast<int>(Key::F1) == 11, "F1 to F12 are contiguous");

		constexpr DeviceId kKeyboard{.kind = DeviceKind::Keyboard};
		constexpr DeviceId kMouse{.kind = DeviceKind::Mouse};

		constexpr std::array<std::string_view, static_cast<std::size_t>(foundation::GamepadButton::Count)> kButtonNames{
			"a",
			"b",
			"x",
			"y",
			"leftbumper",
			"rightbumper",
			"back",
			"start",
			"guide",
			"leftthumb",
			"rightthumb",
			"dpadup",
			"dpadright",
			"dpaddown",
			"dpadleft",
		};
		constexpr std::array<std::string_view, static_cast<std::size_t>(foundation::GamepadAxis::Count)> kAxisNames{
			"leftx",
			"lefty",
			"rightx",
			"righty",
			"lefttrigger",
			"righttrigger",
		};
		constexpr std::array<std::pair<std::string_view, Key>, 18> kNamedKeys{{
			{"space", Key::Space},
			{"escape", Key::Escape},
			{"esc", Key::Escape},
			{"enter", Key::Enter},
			{"return", Key::Enter},
			{"tab", Key::Tab},
			{"backspace", Key::Backspace},
			{"delete", Key::Delete},
			{"del", Key::Delete},
			{"insert", Key::Insert},
			{"home", Key::Home},
			{"end", Key::End},
			{"pageup", Key::PageUp},
			{"pagedown", Key::PageDown},
			{"up", Key::Up},
			{"down", Key::Down},
			{"left", Key::Left},
			{"right", Key::Right},
		}};

		std::string lowered(std::string_view text) {
			std::string out(text);
			std::ranges::transform(out, out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return out;
		}

		std::vector<std::string_view> splitFields(std::string_view ev) {
			std::vector<std::string_view> fields;
			std::size_t					  start = 0;
			while (true) {
				const std::size_t comma = ev.find(',', start);
				fields.push_back(ev.substr(start, comma == std::string_view::npos ? std::string_view::npos : comma - start));
				if (comma == std::string_view::npos) {
					return fields;
				}
				start = comma + 1;
			}
		}

		std::optional<float> parseFloat(std::string_view text) {
			if (text.empty()) {
				return std::nullopt;
			}
			const std::string copy(text);
			char*			  end = nullptr;
			const float		  value = std::strtof(copy.c_str(), &end);
			if (end != copy.c_str() + copy.size() || !std::isfinite(value)) {
				return std::nullopt;
			}
			return value;
		}

		std::optional<Key> keyFromName(std::string_view name) {
			if (name.size() == 1) {
				const auto c = static_cast<unsigned char>(name[0]);
				if (std::isalpha(c) != 0) {
					return static_cast<Key>(static_cast<int>(Key::A) + (std::tolower(c) - 'a'));
				}
				if (std::isdigit(c) != 0) {
					return static_cast<Key>(static_cast<int>(Key::Num0) + (c - '0'));
				}
				return std::nullopt;
			}
			const std::string lower = lowered(name);
			const auto		  named = std::ranges::find(kNamedKeys, std::string_view(lower), &std::pair<std::string_view, Key>::first);
			if (named != kNamedKeys.end()) {
				return named->second;
			}
			int function = 0;
			if (lower.size() > 1 && lower[0] == 'f') {
				const auto [end, error] = std::from_chars(lower.data() + 1, lower.data() + lower.size(), function);
				if (error != std::errc{} || end != lower.data() + lower.size()) {
					function = 0;
				}
			}
			if (function >= 1 && function <= 12) {
				return static_cast<Key>(static_cast<int>(Key::F1) + (function - 1));
			}
			return std::nullopt;
		}

		// Decimal, or hex after 0x or U+; a Unicode scalar value, so no surrogates.
		std::optional<char32_t> parseCodepoint(std::string_view text) {
			int base = 10;
			if (text.size() > 2 && (text.starts_with("0x") || text.starts_with("0X") || text.starts_with("U+") || text.starts_with("u+"))) {
				text.remove_prefix(2);
				base = 16;
			}
			std::uint32_t value = 0;
			const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value, base);
			if (text.empty() || error != std::errc{} || end != text.data() + text.size() || value == 0 || value > 0x10FFFF ||
				(value >= 0xD800 && value <= 0xDFFF)) {
				return std::nullopt;
			}
			return static_cast<char32_t>(value);
		}

		template <std::size_t N>
		std::optional<std::size_t> indexOfName(const std::array<std::string_view, N>& names, std::string_view name) {
			const std::string lower = lowered(name);
			const auto		  found = std::ranges::find(names, std::string_view(lower));
			return found != names.end() ? std::optional(static_cast<std::size_t>(found - names.begin())) : std::nullopt;
		}

		// One event at a time, against the virtual pads as the events before it left them.
		class Parser {
		  public:
			explicit Parser(VirtualPads& virtualPads)
				: pads(virtualPads) {}

			// False, with `error` set, for an event that doesn't parse or doesn't apply.
			bool parse(std::string_view ev, std::vector<InputEvent>& out) {
				if (ev.empty()) {
					return fail("empty event");
				}
				fields = splitFields(ev);
				const std::string_view type = fields[0];
				if (type == "move" || type == "down" || type == "up" || type == "click" || type == "scroll") {
					return pointer(type, out);
				}
				if (type == "keydown" || type == "keyup") {
					return key(type == "keydown" ? Type::KeyDown : Type::KeyUp, out);
				}
				if (type == "char") {
					return character(out);
				}
				if (type == "pad") {
					return padButton(out);
				}
				if (type == "axis") {
					return padAxis(out);
				}
				if (type == "connect" || type == "disconnect") {
					return connection(type == "connect", out);
				}
				return fail("unknown event type (expected move|down|up|click|scroll|keydown|keyup|char|pad|axis|connect|disconnect)");
			}

			std::string error;

		  private:
			bool fail(std::string message) {
				error = std::move(message);
				return false;
			}

			bool fieldCount(std::size_t least, std::size_t most, const char* usage) {
				if (fields.size() < least || fields.size() > most) {
					return fail(std::string("expected ") + usage);
				}
				return true;
			}

			bool pointer(std::string_view type, std::vector<InputEvent>& out) {
				const bool scroll = type == "scroll";
				const bool takesButton = type == "down" || type == "up" || type == "click";
				if (scroll ? !fieldCount(4, 4, "scroll,x,y,delta")
						   : !fieldCount(3, takesButton ? 4 : 3, "type,x,y with an optional button")) {
					return false;
				}
				const std::optional<float> x = parseFloat(fields[1]);
				const std::optional<float> y = parseFloat(fields[2]);
				if (!x || !y) {
					return fail("non-numeric coordinates");
				}
				const InputEvent at{.device = kMouse, .position = {*x, *y}};
				if (scroll) {
					const std::optional<float> delta = parseFloat(fields[3]);
					if (!delta) {
						return fail("non-numeric scroll delta");
					}
					InputEvent wheel = at;
					wheel.type = Type::Scroll;
					wheel.scroll = {0.0F, *delta};
					out.push_back(wheel);
					return true;
				}
				foundation::MouseButton button = foundation::MouseButton::Left;
				if (fields.size() == 4) {
					if (fields[3] == "right") {
						button = foundation::MouseButton::Right;
					} else if (fields[3] == "middle") {
						button = foundation::MouseButton::Middle;
					} else if (fields[3] != "left") {
						return fail("unknown button (expected left|right|middle)");
					}
				}
				const auto make = [&](Type eventType) {
					InputEvent event = at;
					event.type = eventType;
					event.mouseButton = button;
					return event;
				};
				if (type == "move" || type == "click") {
					out.push_back(make(Type::PointerMove));
				}
				if (type == "down" || type == "click") {
					out.push_back(make(Type::PointerDown));
				}
				if (type == "up" || type == "click") {
					out.push_back(make(Type::PointerUp));
				}
				return true;
			}

			bool key(Type type, std::vector<InputEvent>& out) {
				if (fields.size() < 2 || fields[1].empty()) {
					return fail("key event requires a key name (e.g. keydown,R)");
				}
				if (!fieldCount(2, 2, "keydown,<key> or keyup,<key>")) {
					return false;
				}
				const std::optional<Key> found = keyFromName(fields[1]);
				if (!found) {
					return fail("unknown key '" + std::string(fields[1]) + "'");
				}
				out.push_back({.type = type, .device = kKeyboard, .key = *found});
				return true;
			}

			bool character(std::vector<InputEvent>& out) {
				if (!fieldCount(2, 2, "char,<codepoint>")) {
					return false;
				}
				const std::optional<char32_t> codepoint = parseCodepoint(fields[1]);
				if (!codepoint) {
					return fail("a codepoint is a Unicode scalar value, decimal or hex after 0x or U+");
				}
				out.push_back({.type = Type::Char, .device = kKeyboard, .codepoint = *codepoint});
				return true;
			}

			// vpad<n>, which must be connected unless this is its connect.
			std::optional<int> virtualPad(std::string_view name) {
				int vpad = -1;
				if (name.starts_with("vpad")) {
					const std::string_view digits = name.substr(4);
					const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), vpad);
					if (digits.empty() || error != std::errc{} || end != digits.data() + digits.size()) {
						vpad = -1;
					}
				}
				if (vpad < 0 || vpad >= foundation::kMaxVirtualGamepads) {
					fail("unknown device '" + std::string(name) + "' (expected vpad0 to vpad3)");
					return std::nullopt;
				}
				return vpad;
			}

			static DeviceId padDevice(int vpad) {
				return {.kind = DeviceKind::Gamepad, .index = static_cast<std::uint8_t>(foundation::kMaxRealGamepads + vpad)};
			}

			bool connectedPad(std::string_view name, int& vpad) {
				const std::optional<int> found = virtualPad(name);
				if (!found) {
					return false;
				}
				vpad = *found;
				if (!pads[static_cast<std::size_t>(vpad)]) {
					return fail(std::string(name) + " isn't connected (connect," + std::string(name) + " first)");
				}
				return true;
			}

			bool padButton(std::vector<InputEvent>& out) {
				int vpad = 0;
				if (!fieldCount(4, 4, "pad,vpad<n>,<button>,down|up") || !connectedPad(fields[1], vpad)) {
					return false;
				}
				const std::optional<std::size_t> button = indexOfName(kButtonNames, fields[2]);
				if (!button) {
					return fail("unknown pad button '" + std::string(fields[2]) + "'");
				}
				if (fields[3] != "down" && fields[3] != "up") {
					return fail("a pad button goes down or up");
				}
				out.push_back(
					{.type = fields[3] == "down" ? Type::ButtonDown : Type::ButtonUp,
					 .device = padDevice(vpad),
					 .button = static_cast<foundation::GamepadButton>(*button)}
				);
				return true;
			}

			bool padAxis(std::vector<InputEvent>& out) {
				int vpad = 0;
				if (!fieldCount(4, 4, "axis,vpad<n>,<axis>,<value>") || !connectedPad(fields[1], vpad)) {
					return false;
				}
				const std::optional<std::size_t> axis = indexOfName(kAxisNames, fields[2]);
				if (!axis) {
					return fail("unknown pad axis '" + std::string(fields[2]) + "'");
				}
				const std::optional<float> value = parseFloat(fields[3]);
				if (!value || *value < -1.0F || *value > 1.0F) {
					return fail("an axis value is a number from -1 to 1");
				}
				out.push_back(
					{.type = Type::Axis, .device = padDevice(vpad), .axis = static_cast<foundation::GamepadAxis>(*axis), .value = *value}
				);
				return true;
			}

			bool connection(bool connect, std::vector<InputEvent>& out) {
				if (!fieldCount(2, 2, connect ? "connect,vpad<n>" : "disconnect,vpad<n>")) {
					return false;
				}
				const std::optional<int> vpad = virtualPad(fields[1]);
				if (!vpad) {
					return false;
				}
				bool& connected = pads[static_cast<std::size_t>(*vpad)];
				if (connected == connect) {
					return fail(std::string(fields[1]) + (connect ? " is already connected" : " isn't connected"));
				}
				connected = connect;
				out.push_back({.type = connect ? Type::DeviceConnected : Type::DeviceDisconnected, .device = padDevice(*vpad)});
				return true;
			}

			VirtualPads&				  pads;
			std::vector<std::string_view> fields;
		};

	} // namespace

	ParsedInput parseInputEvents(std::span<const std::string> evs, VirtualPads& pads) {
		ParsedInput parsed;
		VirtualPads after = pads;
		Parser		parser(after);
		for (const std::string& ev : evs) {
			if (!parser.parse(ev, parsed.events)) {
				return {.error = std::move(parser.error), .failed = ev};
			}
		}
		pads = after;
		return parsed;
	}

} // namespace app::detail
