// Each control's file name: foundation's enumerator names, read back in any case.

#include "input/ControlNames.h"

#include <gtest/gtest.h>

#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace app {

	namespace {

		using foundation::GamepadAxis;
		using foundation::GamepadButton;
		using foundation::Key;
		using foundation::MouseButton;

	} // namespace

	TEST(ControlNamesTest, EveryControlHasItsEnumeratorsNameAndReadsBackInAnyCase) {
		const std::vector<std::pair<Key, std::string_view>> keys{
			{Key::Unknown, "Unknown"},
			{Key::Space, "Space"},
			{Key::Num0, "Num0"},
			{Key::Equal, "Equal"},
			{Key::A, "A"},
			{Key::Z, "Z"},
			{Key::World2, "World2"},
			{Key::Escape, "Escape"},
			{Key::Pause, "Pause"},
			{Key::F1, "F1"},
			{Key::F25, "F25"},
			{Key::Kp0, "Kp0"},
			{Key::KpEqual, "KpEqual"},
			{Key::LeftShift, "LeftShift"},
			{Key::Menu, "Menu"},
		};
		for (const auto& [key, name] : keys) {
			EXPECT_EQ(controlName(key), name);
		}
		for (int i = 0; i < static_cast<int>(Key::Count); ++i) {
			const auto key = static_cast<Key>(i);
			EXPECT_EQ(controlNamed<Key>(controlName(key)), key) << i;
		}
		EXPECT_EQ(controlNamed<Key>("leftshift"), Key::LeftShift);
		EXPECT_EQ(controlNamed<Key>("Shift"), std::nullopt);
		EXPECT_EQ(controlName(Key::Count), "");

		EXPECT_EQ(controlName(MouseButton::Button8), "Button8");
		EXPECT_EQ(controlNamed<MouseButton>("MIDDLE"), MouseButton::Middle);
		EXPECT_EQ(controlName(GamepadButton::DpadLeft), "DpadLeft");
		EXPECT_EQ(controlNamed<GamepadButton>("guide"), GamepadButton::Guide);
		EXPECT_EQ(controlName(GamepadAxis::RightTrigger), "RightTrigger");
		EXPECT_EQ(controlNamed<GamepadAxis>("lefty"), GamepadAxis::LeftY);
		EXPECT_EQ(controlNamed<GamepadAxis>(""), std::nullopt);
	}

} // namespace app
