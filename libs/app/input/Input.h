#pragma once

// The frame's input and the devices behind it: what dispatch, update and the ui read, which devices
// are connected, which back which player, and what a rebinding screen captures. A fixed tick reads
// its own snapshot, from TickContext. The App feeds both from one InputAccumulator, which the
// platform's events go into as they arrive, and connections through the same calls (PlatformEvents),
// the dev server's virtual pads included.

#include "input/DeviceInfo.h"

#include <input/ActionMap.h>
#include <input/InputAccumulator.h>
#include <input/InputTypes.h>

#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace app {

	class App;
	class Platform;

	// Player slots run from 0 to kMaxPlayerSlots - 1.
	inline constexpr int kMaxPlayerSlots = 8;

	class Input {
	  public:
		// Key labels come from `platform`, which outlives the Input.
		explicit Input(const Platform& platform);

		// This frame's state.
		const foundation::InputSnapshot& frame() const { return frameState; }
		// This frame's events, in order.
		std::span<const foundation::InputEvent> events() const { return accumulator.frameEvents(); }

		// Connected now: the keyboard and the mouse, always, then the gamepads in id order.
		std::span<const DeviceInfo> devices() const { return connected; }

		// The device behind this frame's first press, in event order, for "press a button to join": a
		// key going down (not a repeat), a mouse button or a pad button, from a device that doesn't back
		// its share of slots already, so a joined player's press doesn't hide a new player's. A stick or
		// trigger moving isn't a press, so a stick bumped in passing joins nobody.
		std::optional<foundation::DeviceId> pressedDevice() const;

		// Player slots. A connected device backs a slot: the keyboard up to two (split keyboard), the
		// mouse and each pad one. claim refuses, logged, a slot out of range, a device not connected,
		// and one backing its share of other slots already; a slot already claimed takes the new device
		// in place of its old one. A claimed pad that disconnects keeps its slot, which waits for that
		// pad's model: the next pad to connect with the same model GUID takes it (the lowest slot first,
		// when two wait on one model), and any other pad arrives unclaimed. A pad with no GUID is taken
		// back by nothing.
		void claim(int slot, foundation::DeviceId device);
		// Frees the slot, a waiting one included: its pad arrives unclaimed when it comes back.
		void release(int slot);
		// The slot's device while it's connected, and once its pad has left until another device connects
		// at that id: the accumulator has released and zeroed it, so a sim reading it sees the release,
		// then nothing. nullopt for a free slot and for a waiting one whose id another device has taken.
		// A pad that leaves and comes back at another id in one poll moves the slot before a tick reads
		// the release, so the sim sees a second press with no release between; one that leaves as another
		// device takes its id in one poll leaves the slot nothing to read the release from.
		std::optional<foundation::DeviceId> deviceForSlot(int slot) const;
		// Claimed, with its device connected: false for a free slot and for one waiting for its pad.
		bool slotConnected(int slot) const;

		// What to show for a key: the layout's label for the keys it prints (the main block), its small
		// letters capitalised as the table's are (ASCII, Latin-1, Greek and Cyrillic, by range), else a
		// fixed English name ("Left Shift", "Keypad 0", "Page Up"). "Unknown" for Key::Unknown and
		// anything out of range.
		std::string keyName(foundation::Key key) const;

		// The next press this frame that an earlier call this frame hasn't returned, as a binding for a
		// rebinding screen: a key going down (not a repeat), a mouse button, a pad button, or a stick or
		// trigger crossing half travel away from rest, which binds that half (scale -1 for a stick's
		// negative half). nullopt once this frame has none left.
		std::optional<foundation::Binding> captureNext();

	  private:
		friend class App;

		struct Slot {
			bool				 claimed = false;
			bool				 connected = false; // its device is; a claimed pad that left waits with this false
			bool				 idTaken = false;	// another device connected at the departed pad's id
			foundation::DeviceId device{};
			std::string			 modelGuid; // the model a waiting slot takes back
		};

		void			  push(const foundation::InputEvent& event);
		void			  connect(const DeviceInfo& device, double time);
		void			  disconnect(foundation::DeviceId device, double time);
		void			  readFrame();
		const DeviceInfo* find(foundation::DeviceId device) const;
		// The slots the device backs now, connected.
		int backing(foundation::DeviceId device) const;

		const Platform&					  platform;
		foundation::InputAccumulator	  accumulator;
		foundation::InputSnapshot		  frameState{};
		std::vector<DeviceInfo>			  connected;
		std::array<Slot, kMaxPlayerSlots> slots{};
		std::size_t						  captured = 0; // this frame's events captureNext has moved past
	};

} // namespace app
