#pragma once

// A tree of rows, after worldsim's TreeView (libs/ui/components/treeview, a4226e17): each visible
// node a row indented by its depth, with a caret when it has children and its count after its label.
// The rows are kept by node id across a rebuild (reconcileChildren), so focus and hover survive a
// branch opening. The tree is one Tab stop whose up and down move among the rows; right opens a
// row's branch and left closes it. A press on a branch row opens or closes it, and any press
// selects the row and calls onSelect with its id.

#include "ui/Component.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace ui {

	struct TreeNode {
		std::string			  id; // unique in the tree
		std::string			  label;
		std::optional<int>	  count;
		std::vector<TreeNode> children;
		bool				  expanded = false;
	};

	class TreeView : public Component {
	  public:
		struct Args {
			UI_COMMON_ARGS
			std::vector<TreeNode>					nodes;
			float									indent = 16.0F; // px a level
			std::function<void(const std::string&)> onSelect;
		};

		explicit TreeView(const Args& args);

		void							 setNodes(std::vector<TreeNode> value);
		[[nodiscard]] const std::string& getSelectedId() const { return selectedId; }
		// Opens or closes a branch by id, as its row does.
		void setExpanded(const std::string& id, bool expanded);
		// A row, by the tree.
		void press(const std::string& id);

		[[nodiscard]] const char* typeName() const override { return "TreeView"; }

	  protected:
		void onMount() override;

	  private:
		struct Row {
			const TreeNode* node;
			int				depth;
		};

		void			 rebuild();
		TreeNode*		 find(const std::string& id);
		std::vector<Row> visibleRows() const;

		std::vector<TreeNode>					nodes;
		float									indent;
		std::string								selectedId;
		std::function<void(const std::string&)> onSelect;
	};

} // namespace ui
