#pragma once

// A PlatformEvents that keeps what a platform reported, in order, for the platform tests: each input
// event, connection and disconnection as it came, and as one line of text for exact comparisons
// ("ButtonDown 2 A", "Axis 2 LeftTrigger 0.5", "connected 2 Pad 03000000", "disconnected 2").

#include "platform/Platform.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace app::testing {

	class PlatformLog final : public PlatformEvents {
	  public:
		struct Entry {
			enum class Kind : std::uint8_t { Input, Connected, Disconnected };
			Kind				   kind = Kind::Input;
			foundation::InputEvent event{};
			DeviceInfo			   device{}; // a connection's; a disconnection's id
			double				   time = 0.0;
		};

		void inputEvent(const foundation::InputEvent& event) override {
			entries.push_back({.kind = Entry::Kind::Input, .event = event, .time = event.time});
		}
		void deviceConnected(const DeviceInfo& device, double time) override {
			entries.push_back({.kind = Entry::Kind::Connected, .device = device, .time = time});
		}
		void deviceDisconnected(foundation::DeviceId device, double time) override {
			entries.push_back({.kind = Entry::Kind::Disconnected, .device = {.id = device}, .time = time});
		}
		void framebufferResized(const Renderer::Viewport& viewport) override { resizes.push_back(viewport); }

		std::vector<foundation::InputEvent> inputs() const {
			std::vector<foundation::InputEvent> events;
			for (const Entry& entry : entries) {
				if (entry.kind == Entry::Kind::Input) {
					events.push_back(entry.event);
				}
			}
			return events;
		}

		// Pad and connection entries as text; other input events by type alone.
		std::vector<std::string> text() const {
			std::vector<std::string> lines;
			for (const Entry& entry : entries) {
				lines.push_back(describe(entry));
			}
			return lines;
		}

		std::vector<Entry>				entries;
		std::vector<Renderer::Viewport> resizes;

	  private:
		static std::string describe(const Entry& entry) {
			using Type = foundation::InputEvent::Type;
			constexpr std::array<std::string_view, 15> kButtons{
				"A",
				"B",
				"X",
				"Y",
				"LeftBumper",
				"RightBumper",
				"Back",
				"Start",
				"Guide",
				"LeftThumb",
				"RightThumb",
				"DpadUp",
				"DpadRight",
				"DpadDown",
				"DpadLeft"
			};
			constexpr std::array<std::string_view, 6> kAxes{"LeftX", "LeftY", "RightX", "RightY", "LeftTrigger", "RightTrigger"};

			const std::string index = std::to_string(entry.kind == Entry::Kind::Input ? entry.event.device.index : entry.device.id.index);
			switch (entry.kind) {
				case Entry::Kind::Connected:
					return "connected " + index + " " + entry.device.name + " " + entry.device.modelGuid;
				case Entry::Kind::Disconnected:
					return "disconnected " + index;
				case Entry::Kind::Input:
					break;
			}
			const foundation::InputEvent& event = entry.event;
			switch (event.type) {
				case Type::ButtonDown:
					return "ButtonDown " + index + " " + std::string(kButtons.at(static_cast<std::size_t>(event.button)));
				case Type::ButtonUp:
					return "ButtonUp " + index + " " + std::string(kButtons.at(static_cast<std::size_t>(event.button)));
				case Type::Axis: {
					char value[32];
					std::snprintf(value, sizeof(value), "%g", static_cast<double>(event.value));
					return "Axis " + index + " " + std::string(kAxes.at(static_cast<std::size_t>(event.axis))) + " " + value;
				}
				default:
					return "type " + std::to_string(static_cast<int>(event.type));
			}
		}
	};

} // namespace app::testing
