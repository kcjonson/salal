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

namespace {

	using foundation::InputEvent;
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

	const foundation::ButtonState& button(const foundation::InputSnapshot& input, MouseButton which) {
		return input.mouse[static_cast<std::size_t>(which)];
	}

	const foundation::ButtonState& key(const foundation::InputSnapshot& input, Key which) {
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

	void text(const std::string& line, float x, float y, float size, const Foundation::Color& color) {
		Renderer::Primitives::drawText({.text = line, .position = {x, y}, .scale = size / 16.0F, .color = color});
	}

	class InputTestScene final : public app::Scene {
	  public:
		const char* name() const override { return kSceneName; }

		void onEnter(app::App& app) override { owner = &app; }

		void update(const app::FrameContext& frame) override {
			for (const InputEvent& event : owner->input().events()) {
				if (event.type == InputEvent::Type::PointerDown && event.mouseButton == MouseButton::Left) {
					dragStart = event.position;
				}
				// The pointer line shows where it is; a list of every move would hide everything else.
				if (event.type != InputEvent::Type::PointerMove) {
					recent.push_front(describe(event));
					if (recent.size() > kRecentEvents) {
						recent.pop_back();
					}
				}
			}
			input = frame.input;
			if (!button(input, MouseButton::Left).down) {
				dragStart.reset();
			}
		}

		void render(float /*alpha*/) override {
			Renderer::Primitives::clear(kBackground);
			float y = kTop;
			text("Input Test Scene", kLeft, y, kTitleSize, kText);
			y += kLineHeight * 1.5F;

			text("Pointer: " + point(input.pointer), kLeft, y, kTextSize, kText);
			y += kLineHeight;
			text("Pointer Delta: " + point(input.pointerDelta), kLeft, y, kTextSize, kText);
			y += kLineHeight;

			const bool left = button(input, MouseButton::Left).down;
			const bool right = button(input, MouseButton::Right).down;
			const bool middle = button(input, MouseButton::Middle).down;
			text(
				std::string("Mouse Buttons: L:") + (left ? "DOWN" : "UP") + "  R:" + (right ? "DOWN" : "UP") +
					"  M:" + (middle ? "DOWN" : "UP"),
				kLeft,
				y,
				kTextSize,
				left || right || middle ? kHeld : kText
			);
			y += kLineHeight;

			if (dragStart) {
				text("Dragging: Start" + point(*dragStart) + " Delta" + point(input.pointer - *dragStart), kLeft, y, kTextSize, kEdge);
			} else {
				text("Dragging: No", kLeft, y, kTextSize, kText);
			}
			y += kLineHeight;

			std::ostringstream scroll;
			scroll << "Scroll: (" << input.scroll.x << ", " << input.scroll.y << ")";
			text(scroll.str(), kLeft, y, kTextSize, input.scroll != glm::vec2(0.0F) ? kEdge : kText);
			y += kLineHeight;

			text(
				std::string("Pointer In Window: ") + (input.pointerInWindow ? "YES" : "NO"),
				kLeft,
				y,
				kTextSize,
				input.pointerInWindow ? kHeld : kEdge
			);
			y += kLineHeight * 1.5F;

			text("Keyboard (Try WASD, Arrow Keys, Space, Enter):", kLeft, y, kTextSize, kHeading);
			y += kLineHeight;
			for (const KeyRow& row : kKeys) {
				const foundation::ButtonState& state = key(input, row.key);
				const char*					   label = "UP";
				Foundation::Color			   color = kText;
				if (state.pressed()) {
					label = "PRESSED";
					color = kHeld;
				} else if (state.released()) {
					label = "RELEASED";
					color = kEdge;
				} else if (state.down) {
					label = "DOWN";
					color = kHeading;
				}
				text(std::string(row.label) + ": " + label, kLeft, y, kTextSize, color);
				y += kLineHeight;
			}

			y += kLineHeight;
			text("Try moving the mouse, clicking, dragging, scrolling, and pressing keys!", kLeft, y, kHintSize, kHint);

			float eventsY = kTop + (kLineHeight * 1.5F);
			text("Recent events, newest first:", kEventsLeft, eventsY, kTextSize, kHeading);
			eventsY += kLineHeight;
			for (const std::string& line : recent) {
				text(line, kEventsLeft, eventsY, kTextSize, kText);
				eventsY += kLineHeight;
			}
		}

		std::string exportState() const override {
			const auto pair = [](const glm::vec2& value) {
				return nlohmann::ordered_json::array({value.x, value.y});
			};
			nlohmann::ordered_json keysDown = nlohmann::ordered_json::array();
			for (std::size_t index = 1; index < input.keys.size(); ++index) {
				if (input.keys[index].down) {
					keysDown.push_back(owner->input().keyName(static_cast<Key>(index)));
				}
			}
			nlohmann::ordered_json drag = nullptr;
			if (dragStart) {
				drag = {{"start", pair(*dragStart)}, {"delta", pair(input.pointer - *dragStart)}};
			}
			return nlohmann::ordered_json{
				{"pointer", pair(input.pointer)},
				{"pointerDelta", pair(input.pointerDelta)},
				{"buttons",
				 {{"left", button(input, MouseButton::Left).down},
				  {"right", button(input, MouseButton::Right).down},
				  {"middle", button(input, MouseButton::Middle).down}}},
				{"drag", drag},
				{"scroll", pair(input.scroll)},
				{"pointerInWindow", input.pointerInWindow},
				{"keysDown", keysDown},
				{"recent", recent},
			}
				.dump();
		}

	  private:
		std::string describe(const InputEvent& event) const {
			using Type = InputEvent::Type;
			const std::string pad = "pad" + std::to_string(event.device.index);
			switch (event.type) {
				case Type::KeyDown:
					return "keydown " + owner->input().keyName(event.key) + (event.repeat ? " (repeat)" : "");
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
				case Type::Axis: {
					std::ostringstream line;
					line << pad << " axis " << static_cast<int>(event.axis) << " " << event.value;
					return line.str();
				}
				case Type::DeviceConnected:
					return "connect " + pad;
				case Type::DeviceDisconnected:
					return "disconnect " + pad;
				case Type::FocusLost:
					return "focus lost";
				case Type::FocusGained:
					return "focus gained";
				case Type::PointerMove:
					return "move " + point(event.position);
			}
			return "unknown event";
		}

		static std::string hex(char32_t codepoint) {
			std::ostringstream digits;
			digits << std::uppercase << std::hex << static_cast<std::uint32_t>(codepoint);
			std::string out = digits.str();
			return std::string(out.size() < 4 ? 4 - out.size() : 0, '0') + out;
		}

		app::App*				  owner = nullptr;
		foundation::InputSnapshot input{};
		std::optional<glm::vec2>  dragStart;
		std::deque<std::string>	  recent;
	};

} // namespace

namespace sandbox {
	const SceneInfo kInputTestScene{.name = kSceneName, .make = makeScene<InputTestScene>};
} // namespace sandbox
