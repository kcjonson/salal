#include "input/Input.h"

#include "platform/Platform.h"

#include <utils/Log.h>
#include <utils/Utf8.h>

#include <algorithm>
#include <cstdint>
#include <string_view>

namespace app {

	namespace {

		using foundation::DeviceId;
		using foundation::DeviceKind;
		using foundation::InputEvent;
		using foundation::Key;

		constexpr DeviceId	  kKeyboard{.kind = DeviceKind::Keyboard};
		constexpr DeviceId	  kMouse{.kind = DeviceKind::Mouse};
		constexpr std::size_t kPads = foundation::kMaxRealGamepads + foundation::kMaxVirtualGamepads;

		// The English name of each Key, in Key's order.
		constexpr std::array<std::string_view, static_cast<std::size_t>(Key::Count)> kKeyNames = {
			"Unknown",
			"Space",
			"'",
			",",
			"-",
			".",
			"/",
			"0",
			"1",
			"2",
			"3",
			"4",
			"5",
			"6",
			"7",
			"8",
			"9",
			";",
			"=",
			"A",
			"B",
			"C",
			"D",
			"E",
			"F",
			"G",
			"H",
			"I",
			"J",
			"K",
			"L",
			"M",
			"N",
			"O",
			"P",
			"Q",
			"R",
			"S",
			"T",
			"U",
			"V",
			"W",
			"X",
			"Y",
			"Z",
			"[",
			"\\",
			"]",
			"`",
			"World 1",
			"World 2",
			"Escape",
			"Enter",
			"Tab",
			"Backspace",
			"Insert",
			"Delete",
			"Right",
			"Left",
			"Down",
			"Up",
			"Page Up",
			"Page Down",
			"Home",
			"End",
			"Caps Lock",
			"Scroll Lock",
			"Num Lock",
			"Print Screen",
			"Pause",
			"F1",
			"F2",
			"F3",
			"F4",
			"F5",
			"F6",
			"F7",
			"F8",
			"F9",
			"F10",
			"F11",
			"F12",
			"F13",
			"F14",
			"F15",
			"F16",
			"F17",
			"F18",
			"F19",
			"F20",
			"F21",
			"F22",
			"F23",
			"F24",
			"F25",
			"Keypad 0",
			"Keypad 1",
			"Keypad 2",
			"Keypad 3",
			"Keypad 4",
			"Keypad 5",
			"Keypad 6",
			"Keypad 7",
			"Keypad 8",
			"Keypad 9",
			"Keypad .",
			"Keypad /",
			"Keypad *",
			"Keypad -",
			"Keypad +",
			"Keypad Enter",
			"Keypad =",
			"Left Shift",
			"Left Control",
			"Left Alt",
			"Left Super",
			"Right Shift",
			"Right Control",
			"Right Alt",
			"Right Super",
			"Menu",
		};

		constexpr bool everyKeyNamed() {
			return std::ranges::none_of(kKeyNames, [](std::string_view name) { return name.empty(); });
		}
		static_assert(everyKeyNamed(), "kKeyNames needs a name for every Key, in Key's order");

		// The keys a layout prints its own label on: the main block from the apostrophe to the second
		// non-US key. Space and the keypad keep their names, since a layout's label for a keypad digit
		// would read as the main block's.
		bool layoutLabels(Key key) {
			return key >= Key::Apostrophe && key <= Key::World2;
		}

		// The capital of a small letter in ASCII, the Latin-1 Supplement, Greek or Cyrillic, the scripts
		// most layouts print on their main block; anything else as it is.
		char32_t capital(char32_t c) {
			if ((c >= U'a' && c <= U'z') || (c >= 0xE0 && c <= 0xFE && c != 0xF7)) {
				return c - 0x20;
			}
			if (c == 0x3C2) {
				return 0x3A3; // final sigma
			}
			if ((c >= 0x3B1 && c <= 0x3CB) || (c >= 0x430 && c <= 0x44F)) {
				return c - 0x20;
			}
			if (c >= 0x450 && c <= 0x45F) {
				return c - 0x50;
			}
			return c;
		}

		std::string capitalised(const std::string& label) {
			std::string capitals;
			for (const char32_t c : foundation::UTF8::decode(label)) {
				capitals += foundation::UTF8::encode(capital(c));
			}
			return capitals;
		}

		[[maybe_unused]] const char* kindName(DeviceKind kind) {
			switch (kind) {
				case DeviceKind::Keyboard:
					return "keyboard";
				case DeviceKind::Mouse:
					return "mouse";
				case DeviceKind::Gamepad:
					return "gamepad";
			}
			return "device";
		}

		// The slots a device may back: two halves of the keyboard, one of anything else.
		int shareOf(DeviceId device) {
			return device.kind == DeviceKind::Keyboard ? 2 : 1;
		}

		// The device a press came from: key and pointer events belong to the one keyboard and mouse
		// whatever their device says, as in the accumulator.
		std::optional<DeviceId> pressFrom(const InputEvent& event) {
			switch (event.type) {
				case InputEvent::Type::KeyDown:
					return event.repeat ? std::nullopt : std::optional<DeviceId>{kKeyboard};
				case InputEvent::Type::PointerDown:
					return kMouse;
				case InputEvent::Type::ButtonDown:
					return event.device;
				default:
					return std::nullopt;
			}
		}

	} // namespace

	Input::Input(const Platform& inputPlatform)
		: platform(inputPlatform),
		  connected{{.id = kKeyboard, .name = "Keyboard"}, {.id = kMouse, .name = "Mouse"}} {}

	std::optional<DeviceId> Input::pressedDevice() const {
		for (const InputEvent& event : events()) {
			if (const std::optional<DeviceId> device = pressFrom(event); device && backing(*device) < shareOf(*device)) {
				return device;
			}
		}
		return std::nullopt;
	}

	void Input::claim(int slot, DeviceId device) {
		if (slot < 0 || slot >= kMaxPlayerSlots) {
			LOG_WARNING(Engine, "Input::claim: slot %d is out of range (0 to %d); refused", slot, kMaxPlayerSlots - 1);
			return;
		}
		const DeviceInfo* info = find(device);
		if (info == nullptr) {
			LOG_WARNING(Engine, "Input::claim: %s %d isn't connected; slot %d refused", kindName(device.kind), int{device.index}, slot);
			return;
		}
		Slot& target = slots[static_cast<std::size_t>(slot)];
		if (target.claimed && target.connected && target.device == device) {
			return;
		}
		const int share = shareOf(device);
		if (backing(device) >= share) {
			LOG_WARNING(
				Engine,
				"Input::claim: %s %d backs %d slot(s) already; slot %d refused",
				kindName(device.kind),
				int{device.index},
				share,
				slot
			);
			return;
		}
		target = {.claimed = true, .connected = true, .device = device, .modelGuid = info->modelGuid};
	}

	void Input::release(int slot) {
		if (slot >= 0 && slot < kMaxPlayerSlots) {
			slots[static_cast<std::size_t>(slot)] = {};
		}
	}

	std::optional<DeviceId> Input::deviceForSlot(int slot) const {
		if (slot < 0 || slot >= kMaxPlayerSlots) {
			return std::nullopt;
		}
		const Slot& held = slots[static_cast<std::size_t>(slot)];
		if (!held.claimed || held.idTaken) {
			return std::nullopt;
		}
		return held.device;
	}

	bool Input::slotConnected(int slot) const {
		if (slot < 0 || slot >= kMaxPlayerSlots) {
			return false;
		}
		const Slot& held = slots[static_cast<std::size_t>(slot)];
		return held.claimed && held.connected;
	}

	std::string Input::keyName(Key key) const {
		const auto index = static_cast<std::size_t>(key);
		if (index >= kKeyNames.size()) {
			return std::string(kKeyNames[0]);
		}
		if (layoutLabels(key)) {
			if (std::optional<std::string> label = platform.keyLabel(key); label && !label->empty()) {
				return capitalised(*label);
			}
		}
		return std::string(kKeyNames[index]);
	}

	std::optional<foundation::Binding> Input::captureNext() {
		using Source = foundation::Binding::Source;
		constexpr float kHalfTravel = foundation::Binding{}.threshold;

		// Each pad's axes as of each event, from where the frame reader last read them.
		std::array<decltype(foundation::GamepadState::axes), kPads> axes{};
		for (std::size_t pad = 0; pad < axes.size(); ++pad) {
			axes[pad] = frameState.pads[pad].startAxes;
		}

		const std::span<const InputEvent> all = events();
		for (std::size_t i = 0; i < all.size(); ++i) {
			const InputEvent&				   event = all[i];
			std::optional<foundation::Binding> binding;
			switch (event.type) {
				case InputEvent::Type::KeyDown:
					if (!event.repeat) {
						binding = foundation::Binding{.source = Source::Key, .control = static_cast<std::uint16_t>(event.key)};
					}
					break;
				case InputEvent::Type::PointerDown:
					binding = foundation::Binding{.source = Source::MouseButton, .control = static_cast<std::uint16_t>(event.mouseButton)};
					break;
				case InputEvent::Type::ButtonDown:
					binding = foundation::Binding{.source = Source::GamepadButton, .control = static_cast<std::uint16_t>(event.button)};
					break;
				case InputEvent::Type::Axis: {
					float&		previous = axes[event.device.index][static_cast<std::size_t>(event.axis)];
					const float value = event.value;
					const auto	control = static_cast<std::uint16_t>(event.axis);
					if (previous <= kHalfTravel && value > kHalfTravel) {
						binding = foundation::Binding{.source = Source::GamepadAxis, .control = control};
					} else if (previous >= -kHalfTravel && value < -kHalfTravel) {
						binding = foundation::Binding{.source = Source::GamepadAxis, .control = control, .scale = -1.0F};
					}
					previous = value;
					break;
				}
				case InputEvent::Type::DeviceConnected:
				case InputEvent::Type::DeviceDisconnected:
					if (foundation::isPadSlot(event.device)) {
						axes[event.device.index] = {};
					}
					break;
				default:
					break;
			}
			if (binding && i >= captured) {
				captured = i + 1;
				return binding;
			}
		}
		captured = all.size();
		return std::nullopt;
	}

	void Input::push(const InputEvent& event) {
		accumulator.push(event);
	}

	// Gamepads only: the keyboard and the mouse are always there.
	void Input::connect(const DeviceInfo& device, double time) {
		if (!foundation::isPadSlot(device.id)) {
			LOG_WARNING(
				Engine, "A %s %d connected, which isn't a gamepad in pads[]; dropped", kindName(device.id.kind), int{device.id.index}
			);
			return;
		}
		if (find(device.id) != nullptr) {
			LOG_WARNING(Engine, "Gamepad %d connected while connected already; dropped", int{device.id.index});
			return;
		}
		const auto after = std::ranges::find_if(connected, [&](const DeviceInfo& other) {
			return other.id.kind == DeviceKind::Gamepad && other.id.index > device.id.index;
		});
		connected.insert(after, device);
		LOG_INFO(Engine, "Gamepad %d connected: %s", int{device.id.index}, device.name.c_str());

		for (std::size_t slot = 0; slot < slots.size(); ++slot) {
			Slot& waiting = slots[slot];
			if (waiting.claimed && !waiting.connected && !waiting.modelGuid.empty() && waiting.modelGuid == device.modelGuid) {
				waiting.device = device.id;
				waiting.connected = true;
				waiting.idTaken = false;
				LOG_INFO(Engine, "Slot %zu takes back its gamepad, now %d", slot, int{device.id.index});
				break;
			}
		}
		for (Slot& waiting : slots) {
			if (waiting.claimed && !waiting.connected && waiting.device == device.id) {
				waiting.idTaken = true;
			}
		}
		accumulator.push({.type = InputEvent::Type::DeviceConnected, .device = device.id, .time = time});
	}

	void Input::disconnect(DeviceId device, double time) {
		const auto gone = std::ranges::find_if(connected, [&](const DeviceInfo& info) { return info.id == device; });
		if (gone == connected.end() || device.kind != DeviceKind::Gamepad) {
			LOG_WARNING(Engine, "A %s %d that isn't a connected gamepad disconnected; dropped", kindName(device.kind), int{device.index});
			return;
		}
		connected.erase(gone);
		LOG_INFO(Engine, "Gamepad %d disconnected", int{device.index});

		for (Slot& slot : slots) {
			if (slot.claimed && slot.connected && slot.device == device) {
				slot.connected = false;
			}
		}
		accumulator.push({.type = InputEvent::Type::DeviceDisconnected, .device = device, .time = time});
	}

	void Input::readFrame() {
		frameState = accumulator.consumeFrame();
		captured = 0;
	}

	int Input::backing(DeviceId device) const {
		return static_cast<int>(std::ranges::count_if(slots, [&](const Slot& slot) {
			return slot.claimed && slot.connected && slot.device == device;
		}));
	}

	const DeviceInfo* Input::find(DeviceId device) const {
		const auto found = std::ranges::find_if(connected, [&](const DeviceInfo& info) { return info.id == device; });
		return found == connected.end() ? nullptr : &*found;
	}

} // namespace app
