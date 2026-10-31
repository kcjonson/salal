#pragma once

// Toasts, after worldsim's Toast and ToastStack (libs/ui/components/toast, a4226e17). A Toast is a
// notification: a stripe in its severity's tone, an optional icon, its title in that tone and its
// message, and a close button. It closes itself after autoDismissMs (0 keeps it), on its close
// button, or on a press on its body, which calls onPress first; onDismiss hears each. It fades in.
//
// A ToastStack holds them (gap 30): one toast overlay entry over the whole viewport that only its
// toasts take the pointer in, stacking them in a column at one corner, newest nearest the corner.
// Each new toast brings it to the front of the overlay stack, above a dialog opened since, and past
// maxToasts the oldest goes.

#include "ui/shapes/Rectangle.h"
#include "ui/shapes/Text.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace ui {

	enum class ToastSeverity : std::uint8_t { Info, Warning, Critical };

	class Toast : public Rectangle {
	  public:
		struct Args {
			UI_COMMON_ARGS
			std::string			  title;
			std::string			  message;
			ToastSeverity		  severity = ToastSeverity::Info;
			double				  autoDismissMs = 5000.0;
			std::string			  icon; // a name in the scene's icon atlases; empty: none
			std::function<void()> onPress;
			std::function<void()> onDismiss;
		};

		explicit Toast(const Args& args);

		// Closes it, calling onDismiss.
		void dismiss();

		[[nodiscard]] const std::string& getTitle() const { return title; }
		[[nodiscard]] const std::string& getMessage() const { return message; }
		[[nodiscard]] ToastSeverity		 getSeverity() const { return severity; }
		[[nodiscard]] bool				 isPersistent() const { return autoDismissMs <= 0.0; }

		[[nodiscard]] const char* typeName() const override { return "Toast"; }
		[[nodiscard]] StyleId	  themeStyle() const override;
		[[nodiscard]] bool		  handlesPointer() const override { return true; }

	  protected:
		void onMount() override;
		void handlePointer(PointerEvent& event) override;

	  private:
		std::string			  title;
		std::string			  message;
		ToastSeverity		  severity;
		double				  autoDismissMs;
		std::string			  iconName;
		std::function<void()> onPress;
		std::function<void()> onDismiss;
		Tween<float>		  appear;
	};

	enum class ToastAnchor : std::uint8_t { TopRight, TopLeft, BottomRight, BottomLeft };

	class ToastStack : public Component {
	  public:
		struct Args {
			UI_COMMON_ARGS
			ToastAnchor anchor = ToastAnchor::BottomRight;
			float		spacing = 8.0F;
			float		inset = 16.0F; // from the viewport's edges
			std::size_t maxToasts = 5;
			float		toastWidth = 300.0F;
		};

		explicit ToastStack(const Args& args);

		// Opens one under `opener`, which owns it; a scene usually opens one from its root.
		static Ref<ToastStack> open(Component& opener, const Args& args);

		// A new toast, `toastWidth` wide unless its size says otherwise.
		Ref<Toast> add(Toast::Args args);
		void	   dismissAll();
		// Its toasts, oldest first.
		[[nodiscard]] std::vector<Ref<Toast>> toasts() const;
		[[nodiscard]] std::size_t			  count() const { return entries.size(); }

		[[nodiscard]] const char* typeName() const override { return "ToastStack"; }

	  protected:
		void onMount() override;

	  private:
		friend class Toast;

		struct Entry {
			std::string key;
			Toast::Args args;
			Ref<Toast>	toast;
		};

		void removed(const Toast& toast);
		void arrange();

		ToastAnchor		   anchor;
		float			   spacing;
		float			   inset;
		std::size_t		   maxToasts;
		float			   toastWidth;
		std::vector<Entry> entries; // oldest first
		std::uint64_t	   nextKey = 0;
	};

} // namespace ui
