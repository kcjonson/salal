#pragma once

// A dialog, after worldsim's Dialog (libs/ui/components/dialog, a4226e17): a panel centred on the
// screen with a title (an optional kicker above it) and a close button, its content, and an optional
// footer for actions. Dialog::open opens it from its opener over the whole viewport. Modal, the
// default, it draws the scrim, what's under it takes no pointer for its player, and that player's
// focus stays inside its subtree (a scope) until it closes, then goes back to where it was.
// Non-modal, it's a panel overlay with no scrim, and presses around the panel reach what's under it.
// The close button, cancel and a press on the scrim close it, calling onClose; it fades in.

#include "ui/shapes/Rectangle.h"
#include "ui/shapes/Text.h"

#include <functional>
#include <string>

namespace ui {

	class Dialog : public Rectangle {
	  public:
		struct Args {
			UI_COMMON_ARGS
			std::string			  title;
			std::string			  kicker; // an eyebrow line above the title
			Size				  panelSize{600.0F, kAuto};
			bool				  modal = true;
			bool				  footer = false;
			bool				  closeOnScrim = true;
			std::function<void()> onClose;
		};

		explicit Dialog(const Args& args);

		static Ref<Dialog> open(Component& opener, const Args& args, int player = 0);

		// Where its body goes, a flex column.
		[[nodiscard]] Ref<Component> content() const { return body; }
		// Where its actions go, a flex row aligned to the end; empty without a footer.
		[[nodiscard]] Ref<Component>	 footer() const { return actions; }
		[[nodiscard]] Ref<Rectangle>	 panel() const { return box; }
		[[nodiscard]] const std::string& getTitle() const { return title; }
		void							 setTitle(std::string value);
		[[nodiscard]] bool				 isModal() const { return modal; }
		// As the player closes it: onClose, then closed.
		void close();

		[[nodiscard]] const char* typeName() const override { return "Dialog"; }
		[[nodiscard]] StyleId	  themeStyle() const override;
		[[nodiscard]] bool		  handlesPointer() const override { return modal; }

	  protected:
		void						onMount() override;
		void						update(float dt) override;
		void						handlePointer(PointerEvent& event) override;
		[[nodiscard]] PointerEvents defaultPointerEvents() const override;

	  private:
		std::string	   title;
		std::string	   kicker;
		Size		   panelSize;
		bool		   modal;
		bool		   hasFooter;
		bool		   closeOnScrim;
		int			   player = 0;
		bool		   scoped = false;
		Ref<Rectangle> box;
		Ref<Text>	   titleText;
		Ref<Component> body;
		Ref<Component> actions;
		Tween<float>   appear;
	};

} // namespace ui
