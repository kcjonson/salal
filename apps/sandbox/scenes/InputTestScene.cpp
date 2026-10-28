// input_test: the frame's input as a scene sees it. The pointer, its delta, the mouse buttons, scroll
// and a row of keys come from the frame reader's InputSnapshot; the drag (from the left button's press
// to now) and the list of recent events come from the frame's events, App::input().events(), which
// hold what an overlay consumed too. exportState gives the same as JSON, for /api/state?what=scene.

#include "Scenes.h"

#include <application/App.h>
#include <graphics/Color.h>
#include <input/InputTypes.h>
#include <primitives/Primitives.h>

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <sstream>
#include <string>
#include <utility>

namespace {

	using foundation::InputEvent;
	using foundation::InputSnapshot;
	using foundation::Key;
	using foundation::MouseButton;

	constexpr const char* kSceneName = "input_test";
	constexpr std::size_t kRecentEvents = 12;
	constexpr float		  kLeft = 50.0F;
	constexpr float		  kEventsLeft = 720.0F;
	constexpr float		  kTop = 50.0F;
	constexpr float		  kLineHeight = 35.0F;
	constexpr float		  kTitleSize = 28.0F;
	constexpr float		  kTextSize = 20.0F;
	constexpr float		  kHintSize = 16.0F;

	const Foundation::Color kBackground(0.15F, 0.15F, 0.15F, 1.0F);
	const Foundation::Color kText = Foundation::Color::white();
	const Foundation::Color kHeld = Foundation::Color::green();
	const Foundation::Color kEdge = Foundation::Color::yellow();
	const Foundation::Color kHeading = Foundation::Color::cyan();
	const Foundation::Color kHint(0.7F, 0.7F, 0.7F, 1.0F);

	struct KeyRow {
		Key			key;
		const char* label;
	};

	constexpr std::array<KeyRow, 11> kKeys{{
		{.key = Key::W, .label = "W"},
		{.key = Key::A, .label = "A"},
		{.key = Key::S, .label = "S"},
		{.key = Key::D, .label = "D"},
		{.key = Key::Space, .label = "SPACE"},
		{.key = Key::Enter, .label = "ENTER"},
		{.key = Key::Escape, .label = "ESC"},
		{.key = Key::Up, .label = "UP"},
		{.key = Key::Down, .label = "DOWN"},
		{.key = Key::Left, .label = "LEFT"},
		{.key = Key::Right, .label = "RIGHT"},
	}};

	const foundation::ButtonState& button(const InputSnapshot& input, MouseButton which) {
		return input.mouse[static_cast<std::size_t>(which)];
	}

	const foundation::ButtonState& key(const InputSnapshot& input, Key which) {
		return input.keys[static_cast<std::size_t>(which)];
	}

	const char* buttonName(MouseButton which) {
		constexpr std::array<const char*, static_cast<std::size_t>(MouseButton::Count)> kNames = {
			"left", "right", "middle", "button4", "button5", "button6", "button7", "button8"
		};
		const auto index = static_cast<std::size_t>(which);
		return index < kNames.size() ? kNames[index] : "button?";
	}

	std::string point(const glm::vec2& value) {
		return "(" + std::to_string(static_cast<int>(value.x)) + ", " + std::to_string(static_cast<int>(value.y)) + ")";
	}

	std::string hex(char32_t codepoint) {
		std::ostringstream digits;
		digits << std::uppercase << std::hex << static_cast<std::uint32_t>(codepoint);
		const std::string out = digits.str();
		return std::string(out.size() < 4 ? 4 - out.size() : 0, '0') + out;
	}

	struct Line {
		std::string		  text;
		float			  size = kTextSize;
		Foundation::Color color = kText;
	};

	// Lines down the window from a top-left corner, kLineHeight apart.
	class Column {
	  public:
		Column(float left, float top)
			: x(left),
			  y(top) {}

		void line(const Line& args) {
			Renderer::Primitives::drawText({.text = args.text, .position = {x, y}, .scale = args.size / 16.0F, .color = args.color});
			y += kLineHeight;
		}

		void gap(float lines) { y += kLineHeight * lines; }

	  private:
		float x;
		float y;
	};

	// One line of the recent-events list. A key's repeats, and the characters they type, fold into the
	// line of its press.
	struct Recent {
		std::string		 text;
		InputEvent::Type type = InputEvent::Type::KeyDown;
		Key				 key = Key::Unknown;
		int				 repeats = 0;

		std::string line() const {
			if (repeats == 0) {
				return text;
			}
			return text + " (" + std::to_string(repeats) + (repeats == 1 ? " repeat)" : " repeats)");
		}
	};

	class InputTestScene final : public app::Scene {
	  public:
		const char* name() const override { return kSceneName; }

		void onEnter(app::App& app) override { owner = &app; }

		void update(const app::FrameContext& frame) override {
			for (const InputEvent& event : owner->input().events()) {
				if (event.type == InputEvent::Type::PointerDown && event.mouseButton == MouseButton::Left) {
					dragStart = event.position;
				}
				remember(event);
			}
			if (!button(frame.input, MouseButton::Left).down) {
				dragStart.reset();
			}
		}

		void render(float /*alpha*/) override {
			const InputSnapshot& input = owner->input().frame();
			Renderer::Primitives::clear(kBackground);

			Column left(kLeft, kTop);
			left.line({.text = "Input Test Scene", .size = kTitleSize});
			left.gap(0.5F);
			left.line({.text = "Pointer: " + point(input.pointer)});
			left.line({.text = "Pointer Delta: " + point(input.pointerDelta)});

			const bool leftDown = button(input, MouseButton::Left).down;
			const bool rightDown = button(input, MouseButton::Right).down;
			const bool middleDown = button(input, MouseButton::Middle).down;
			left.line(
				{.text = std::string("Mouse Buttons: L:") + (leftDown ? "DOWN" : "UP") + "  R:" + (rightDown ? "DOWN" : "UP") +
						 "  M:" + (middleDown ? "DOWN" : "UP"),
				 .color = leftDown || rightDown || middleDown ? kHeld : kText}
			);

			// update doesn't run while the App is paused, so the drag shows only while the button is down now.
			if (dragStart && leftDown) {
				left.line({.text = "Dragging: Start" + point(*dragStart) + " Delta" + point(input.pointer - *dragStart), .color = kEdge});
			} else {
				left.line({.text = "Dragging: No"});
			}

			std::ostringstream scroll;
			scroll << "Scroll: (" << input.scroll.x << ", " << input.scroll.y << ")";
			left.line({.text = scroll.str(), .color = input.scroll != glm::vec2(0.0F) ? kEdge : kText});
			left.line(
				{.text = std::string("Pointer In Window: ") + (input.pointerInWindow ? "YES" : "NO"),
				 .color = input.pointerInWindow ? kHeld : kEdge}
			);
			left.gap(0.5F);

			left.line({.text = "Keyboard (Try WASD, Arrow Keys, Space, Enter):", .color = kHeading});
			for (const KeyRow& row : kKeys) {
				const foundation::ButtonState& state = key(input, row.key);
				if (state.pressed()) {
					left.line({.text = std::string(row.label) + ": PRESSED", .color = kHeld});
				} else if (state.released()) {
					left.line({.text = std::string(row.label) + ": RELEASED", .color = kEdge});
				} else if (state.down) {
					left.line({.text = std::string(row.label) + ": DOWN", .color = kHeading});
				} else {
					left.line({.text = std::string(row.label) + ": UP"});
				}
			}
			left.gap(1.0F);
			left.line(
				{.text = "Try moving the mouse, clicking, dragging, scrolling, and pressing keys!", .size = kHintSize, .color = kHint}
			);

			Column events(kEventsLeft, kTop + (kLineHeight * 1.5F));
			events.line({.text = "Recent events, newest first:", .color = kHeading});
			for (const Recent& entry : recent) {
				events.line({.text = entry.line()});
			}
		}

		std::string exportState() const override {
			const InputSnapshot& input = owner->input().frame();
			const auto			 pair = [](const glm::vec2& value) {
				  return nlohmann::ordered_json::array({value.x, value.y});
			};
			nlohmann::ordered_json keysDown = nlohmann::ordered_json::array();
			for (std::size_t index = 1; index < input.keys.size(); ++index) {
				if (input.keys[index].down) {
					keysDown.push_back(owner->input().keyName(static_cast<Key>(index)));
				}
			}
			const bool			   leftDown = button(input, MouseButton::Left).down;
			nlohmann::ordered_json drag = nullptr;
			if (dragStart && leftDown) {
				drag = {{"start", pair(*dragStart)}, {"delta", pair(input.pointer - *dragStart)}};
			}
			nlohmann::ordered_json lines = nlohmann::ordered_json::array();
			for (const Recent& entry : recent) {
				lines.push_back(entry.line());
			}
			return nlohmann::ordered_json{
				{"pointer", pair(input.pointer)},
				{"pointerDelta", pair(input.pointerDelta)},
				{"buttons",
				 {{"left", leftDown},
				  {"right", button(input, MouseButton::Right).down},
				  {"middle", button(input, MouseButton::Middle).down}}},
				{"drag", drag},
				{"scroll", pair(input.scroll)},
				{"pointerInWindow", input.pointerInWindow},
				{"keysDown", keysDown},
				{"recent", lines},
			}
				.dump();
		}

	  private:
		void remember(const InputEvent& event) {
			using Type = InputEvent::Type;
			// GLFW reports a repeat's character straight after the repeat.
			const bool repeatsCharacter = repeatCharacterNext && event.type == Type::Char;
			repeatCharacterNext = event.type == Type::KeyDown && event.repeat;
			if (repeatsCharacter) {
				return;
			}
			if (repeatCharacterNext) {
				// The newest line but characters, if it is this key's press, takes the repeat.
				for (Recent& entry : recent) {
					if (entry.type == Type::Char) {
						continue;
					}
					if (entry.type == Type::KeyDown && entry.key == event.key) {
						++entry.repeats;
						return;
					}
					break;
				}
			}
			std::optional<std::string> text = describe(event);
			if (!text) {
				return;
			}
			recent.push_front({.text = std::move(*text), .type = event.type, .key = event.key, .repeats = event.repeat ? 1 : 0});
			if (recent.size() > kRecentEvents) {
				recent.pop_back();
			}
		}

		// The list's line for an event. None for a move, which the pointer line shows, or a pad's axis,
		// which changes nearly every frame while a stick drifts: either would push everything else out.
		std::optional<std::string> describe(const InputEvent& event) const {
			using Type = InputEvent::Type;
			const std::string pad = "pad" + std::to_string(event.device.index);
			switch (event.type) {
				case Type::KeyDown:
					return "keydown " + owner->input().keyName(event.key);
				case Type::KeyUp:
					return "keyup " + owner->input().keyName(event.key);
				case Type::Char:
					return "char U+" + hex(event.codepoint);
				case Type::PointerDown:
					return std::string("down ") + buttonName(event.mouseButton) + " " + point(event.position);
				case Type::PointerUp:
					return std::string("up ") + buttonName(event.mouseButton) + " " + point(event.position);
				case Type::Scroll: {
					std::ostringstream line;
					line << "scroll (" << event.scroll.x << ", " << event.scroll.y << ")";
					return line.str();
				}
				case Type::PointerEnter:
					return "pointer enter";
				case Type::PointerLeave:
					return "pointer leave";
				case Type::ButtonDown:
					return pad + " button " + std::to_string(static_cast<int>(event.button)) + " down";
				case Type::ButtonUp:
					return pad + " button " + std::to_string(static_cast<int>(event.button)) + " up";
				case Type::DeviceConnected:
					return "connect " + pad;
				case Type::DeviceDisconnected:
					return "disconnect " + pad;
				case Type::FocusLost:
					return "focus lost";
				case Type::FocusGained:
					return "focus gained";
				case Type::PointerMove:
				case Type::Axis:
					return std::nullopt;
			}
			return std::nullopt;
		}

		app::App*				 owner = nullptr;
		std::optional<glm::vec2> dragStart;
		std::deque<Recent>		 recent;
		bool					 repeatCharacterNext = false; // the last event was a key repeat
	};

} // namespace

namespace sandbox {
	const SceneInfo kInputTestScene{.name = kSceneName, .make = makeScene<InputTestScene>};
} // namespace sandbox
