// An ActionMap's bindings through a settings section and back: exactly, through a saved file, over a
// map's defaults, with a cleared action, and past what this build can't read.

#include "settings/Bindings.h"
#include "settings/Settings.h"
#include "tests/TempDir.h"

#include <gtest/gtest.h>

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace app {

	namespace {

		using foundation::ActionMap;
		using foundation::Binding;
		using foundation::GamepadAxis;
		using foundation::GamepadButton;
		using foundation::Key;
		using foundation::MouseButton;
		using foundation::StickDeadzone;
		using Source = Binding::Source;
		using testing::TempDir;

		template <typename Control>
		std::uint16_t control(Control value) {
			return static_cast<std::uint16_t>(value);
		}

		// A map with every source, a key at -1 and an axis with its own threshold and deadzone.
		ActionMap played() {
			ActionMap map;
			map.bind("jump", {.source = Source::Key, .control = control(Key::Space)});
			map.bind("jump", {.source = Source::GamepadButton, .control = control(GamepadButton::A)});
			map.bind("moveX", {.source = Source::Key, .control = control(Key::D)});
			map.bind("moveX", {.source = Source::Key, .control = control(Key::A), .scale = -1.0F});
			map.bind(
				"moveX", {.source = Source::GamepadAxis, .control = control(GamepadAxis::LeftX), .threshold = 0.25F, .deadzone = 0.1F}
			);
			map.bind("fire", {.source = Source::MouseButton, .control = control(MouseButton::Left)});
			map.setStickDeadzone(GamepadAxis::LeftX, GamepadAxis::LeftY, 0.2F);
			return map;
		}

		std::string describe(const Binding& binding) {
			return std::to_string(static_cast<int>(binding.source)) + ":" + std::to_string(binding.control) + " " +
				   std::to_string(binding.scale) + " " + std::to_string(binding.threshold) + " " + std::to_string(binding.deadzone);
		}

		// Every action with its bindings, and every stick, as text for one exact comparison.
		std::vector<std::string> contents(const ActionMap& map) {
			std::vector<std::string> lines;
			for (const std::string_view action : map.actions()) {
				std::string line(action);
				line += ":";
				for (const Binding& binding : map.bindings(action)) {
					line += " [" + describe(binding) + "]";
				}
				lines.push_back(line);
			}
			for (const StickDeadzone& stick : map.stickDeadzones()) {
				lines.push_back(
					"stick " + std::to_string(static_cast<int>(stick.x)) + "," + std::to_string(static_cast<int>(stick.y)) + " " +
					std::to_string(stick.radius)
				);
			}
			return lines;
		}

		bool sameBinding(const Binding& a, const Binding& b) {
			return a.source == b.source && a.control == b.control && a.scale == b.scale && a.threshold == b.threshold &&
				   a.deadzone == b.deadzone;
		}

	} // namespace

	TEST(BindingsTest, TheSectionIsWrittenByControlNameWithEveryField) {
		nlohmann::json section = nlohmann::json::object();
		writeBindings(played(), section);
		EXPECT_EQ(section, nlohmann::json::parse(R"({
			"actions": {
				"fire": [{"mouse": "Left", "scale": 1, "threshold": 0.5, "deadzone": 0}],
				"jump": [{"key": "Space", "scale": 1, "threshold": 0.5, "deadzone": 0},
				         {"button": "A", "scale": 1, "threshold": 0.5, "deadzone": 0}],
				"moveX": [{"key": "D", "scale": 1, "threshold": 0.5, "deadzone": 0},
				          {"key": "A", "scale": -1, "threshold": 0.5, "deadzone": 0},
				          {"axis": "LeftX", "scale": 1, "threshold": 0.25, "deadzone": 0.1}]
			},
			"sticks": [{"x": "LeftX", "y": "LeftY", "radius": 0.2}]
		})"));
		// 0.1F and 0.2F as written text, not 0.10000000149011612.
		EXPECT_NE(section.dump().find("\"deadzone\":0.1,"), std::string::npos);
		EXPECT_NE(section.dump().find("\"radius\":0.2,"), std::string::npos);
	}

	TEST(BindingsTest, AMapGoesThroughASectionAndBackExactly) {
		const ActionMap map = played();
		nlohmann::json	section;
		writeBindings(map, section);
		ActionMap read;
		readBindings(section, read);
		EXPECT_EQ(contents(read), contents(map));
		for (const std::string_view action : map.actions()) {
			ASSERT_EQ(read.bindings(action).size(), map.bindings(action).size());
			for (std::size_t i = 0; i < map.bindings(action).size(); ++i) {
				EXPECT_TRUE(sameBinding(read.bindings(action)[i], map.bindings(action)[i])) << action << " " << i;
			}
		}
	}

	TEST(BindingsTest, AMapGoesThroughTheSettingsFileAndBack) {
		const TempDir temp;
		const auto	  file = temp / "settings.json";
		Settings	  settings = Settings::load(file, 1);
		ActionMap	  map = played();
		map.bind("odd", {.source = Source::GamepadAxis, .control = control(GamepadAxis::RightTrigger), .scale = 0.3F, .threshold = 0.7F});
		map.setStickDeadzone(GamepadAxis::RightY, GamepadAxis::RightX, 0.15F);
		writeBindings(map, settings.section("input"));
		ASSERT_TRUE(settings.save());

		const Settings		  loaded = Settings::load(file, 1);
		const nlohmann::json* input = loaded.find("input");
		ASSERT_NE(input, nullptr);
		ActionMap read;
		readBindings(*input, read);
		EXPECT_EQ(contents(read), contents(map));
		const std::span<const Binding> odd = read.bindings("odd");
		ASSERT_EQ(odd.size(), 1U);
		EXPECT_EQ(odd[0].scale, 0.3F);
		EXPECT_EQ(odd[0].threshold, 0.7F);
	}

	TEST(BindingsTest, TheSectionGoesOverTheMapsDefaultsActionByAction) {
		nlohmann::json section = nlohmann::json::parse(R"({
			"actions": {"jump": [{"key": "W"}], "fire": [], "dash": [{"button": "B"}]},
			"sticks": [{"x": "LeftY", "y": "LeftX", "radius": 0.3}]
		})");
		ActionMap	   map = played();
		readBindings(section, map);

		// jump rebound (fields left out take Binding's defaults), fire cleared but listed, dash added,
		// moveX as it was, and the stick's radius replaced through its pair in either order.
		const std::span<const Binding> jump = map.bindings("jump");
		ASSERT_EQ(jump.size(), 1U);
		EXPECT_TRUE(sameBinding(jump[0], {.source = Source::Key, .control = control(Key::W)}));
		EXPECT_TRUE(map.bindings("fire").empty());
		EXPECT_EQ(map.actions(), (std::vector<std::string_view>{"dash", "fire", "jump", "moveX"}));
		EXPECT_EQ(map.bindings("moveX").size(), 3U);
		ASSERT_EQ(map.stickDeadzones().size(), 1U);
		EXPECT_EQ(map.stickDeadzones()[0].radius, 0.3F);

		// The cleared action goes back out as [], so it stays cleared over the defaults next time.
		nlohmann::json written;
		writeBindings(map, written);
		EXPECT_EQ(written["actions"]["fire"], nlohmann::json::array());
		ActionMap again = played();
		readBindings(written, again);
		EXPECT_TRUE(again.bindings("fire").empty());
	}

	TEST(BindingsTest, WritingKeepsWhatTheMapDoesNotHave) {
		nlohmann::json section = nlohmann::json::parse(R"({
			"actions": {"photoMode": [{"key": "P"}], "jump": [{"key": "W"}]},
			"sticks": [{"x": "RightX", "y": "RightY", "radius": 0.4}, {"x": "LeftY", "y": "LeftX", "radius": 0.9}],
			"version": "a newer build's"
		})");
		writeBindings(played(), section);
		EXPECT_EQ(section["actions"]["photoMode"], nlohmann::json::parse(R"([{"key": "P"}])"));
		EXPECT_EQ(section["actions"]["jump"].size(), 2U);
		EXPECT_EQ(
			section["sticks"],
			nlohmann::json::parse(R"([{"x": "RightX", "y": "RightY", "radius": 0.4}, {"x": "LeftX", "y": "LeftY", "radius": 0.2}])")
		);
		EXPECT_EQ(section["version"], "a newer build's");
	}

	TEST(BindingsTest, WhatThisBuildCannotReadIsSkippedAndTheRestBinds) {
		const nlohmann::json section = nlohmann::json::parse(R"({
			"actions": {
				"jump": [{"key": "Space"}, {"key": "HyperKey"}, {"key": "W", "button": "A"}, {"key": 32}, {"key": "E", "scale": "big"},
				         "Space", {"mouse": "Left", "scale": null}, {"KEY": "Q"}, {"key": "enter"}],
				"fire": "Mouse1"
			},
			"sticks": [{"x": "LeftX", "y": "Sideways", "radius": 0.2}, {"x": "RightX", "y": "RightY"}, 3,
			           {"x": "RightX", "y": "RightY", "radius": 0.25}]
		})");
		ActionMap			 map;
		map.bind("fire", {.source = Source::MouseButton, .control = control(MouseButton::Right)});
		readBindings(section, map);

		// Space, the mouse button with its default scale, and Enter, named in any case.
		const std::span<const Binding> jump = map.bindings("jump");
		ASSERT_EQ(jump.size(), 3U);
		EXPECT_TRUE(sameBinding(jump[0], {.source = Source::Key, .control = control(Key::Space)}));
		EXPECT_TRUE(sameBinding(jump[1], {.source = Source::MouseButton, .control = control(MouseButton::Left)}));
		EXPECT_TRUE(sameBinding(jump[2], {.source = Source::Key, .control = control(Key::Enter)}));
		ASSERT_EQ(map.bindings("fire").size(), 1U); // not a list: the map's binding stays
		EXPECT_EQ(map.bindings("fire")[0].control, control(MouseButton::Right));
		ASSERT_EQ(map.stickDeadzones().size(), 1U);
		EXPECT_EQ(map.stickDeadzones()[0].x, GamepadAxis::RightX);
		EXPECT_EQ(map.stickDeadzones()[0].radius, 0.25F);

		ActionMap untouched = played();
		readBindings(nlohmann::json::array({1}), untouched);
		readBindings(nlohmann::json(), untouched);
		readBindings(nlohmann::json::parse(R"({"actions": [], "sticks": {}})"), untouched);
		EXPECT_EQ(contents(untouched), contents(played()));
	}

	TEST(BindingsTest, AnActionWithNothingThisBuildCanReadKeepsItsBindings) {
		// A newer build's only binding for jump, on a control this one doesn't know.
		const nlohmann::json section = nlohmann::json::parse(R"({"actions": {"jump": [{"button": "Touchpad"}], "fire": [{"key": "F"}]}})");
		ActionMap			 map = played();
		readBindings(section, map);
		ASSERT_EQ(map.bindings("jump").size(), 2U); // the defaults, not cleared
		EXPECT_EQ(map.bindings("jump")[0].control, control(Key::Space));
		ASSERT_EQ(map.bindings("fire").size(), 1U);
		EXPECT_EQ(map.bindings("fire")[0].control, control(Key::F));
	}

	TEST(BindingsTest, ANumberPastAFloatsRangeIsNotRead) {
		const nlohmann::json section = nlohmann::json::parse(R"({
			"actions": {"jump": [{"key": "Space", "scale": 1e300}, {"key": "W", "deadzone": -1e300}, {"key": "Enter", "threshold": 0.75}]},
			"sticks": [{"x": "LeftX", "y": "LeftY", "radius": 1e300}]
		})");
		ActionMap			 map;
		readBindings(section, map);
		ASSERT_EQ(map.bindings("jump").size(), 1U);
		EXPECT_TRUE(sameBinding(map.bindings("jump")[0], {.source = Source::Key, .control = control(Key::Enter), .threshold = 0.75F}));
		EXPECT_TRUE(map.stickDeadzones().empty());
	}

	TEST(BindingsTest, AwkwardFloatsGoThroughTheFileExactly) {
		const TempDir temp;
		ActionMap	  map;
		const float	  third = 1.0F / 3.0F;
		map.bind(
			"a",
			{.source = Source::GamepadAxis,
			 .control = control(GamepadAxis::RightX),
			 .scale = third,
			 .threshold = 1e-7F,
			 .deadzone = 0.123456789F}
		);
		map.bind(
			"a", {.source = Source::Key, .control = control(Key::A), .scale = -0.0F, .threshold = 123456.789F, .deadzone = 16777217.0F}
		);
		map.setStickDeadzone(GamepadAxis::LeftX, GamepadAxis::LeftY, 0.99999994F);
		Settings settings = Settings::load(temp / "settings.json", 1);
		writeBindings(map, settings.section("input"));
		ASSERT_TRUE(settings.save());

		const Settings		  loaded = Settings::load(temp / "settings.json", 1);
		const nlohmann::json* input = loaded.find("input");
		ASSERT_NE(input, nullptr);
		ActionMap read;
		readBindings(*input, read);
		ASSERT_EQ(read.bindings("a").size(), 2U);
		for (std::size_t i = 0; i < 2; ++i) {
			EXPECT_TRUE(sameBinding(read.bindings("a")[i], map.bindings("a")[i])) << i;
		}
		EXPECT_EQ(read.stickDeadzones()[0].radius, 0.99999994F);
	}

	TEST(BindingsTest, ABindingToAControlWithNoNameIsNotWritten) {
		ActionMap map;
		map.bind("bad", {.source = Source::MouseButton, .control = 300}); // past MouseButton, and would wrap to 44 as a uint8_t
		map.bind("bad", {.source = Source::Key, .control = control(Key::Count)});
		map.bind("bad", {.source = Source::Key, .control = control(Key::Menu)});
		nlohmann::json section;
		writeBindings(map, section);
		EXPECT_EQ(section["actions"]["bad"], nlohmann::json::parse(R"([{"key": "Menu", "scale": 1, "threshold": 0.5, "deadzone": 0}])"));
	}

} // namespace app
