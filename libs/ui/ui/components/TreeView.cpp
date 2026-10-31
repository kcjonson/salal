#include "ui/components/TreeView.h"

#include "ui/Context.h"
#include "ui/components/Control.h"
#include "ui/shapes/Chevron.h"
#include "ui/shapes/Text.h"
#include "ui/theme/Tokens.h"
#include "ui/theme/Variants.h"

namespace ui {

	namespace {

		constexpr float kPadX = 8.0F;
		constexpr float kCaret = 10.0F;
		constexpr float kGap = 6.0F;

		class TreeRow : public Control {
		  public:
			struct Args {
				UI_COMMON_ARGS
				std::string nodeId;
			};

			explicit TreeRow(const Args& args)
				: nodeId(args.nodeId) {}

			[[nodiscard]] const char* typeName() const override { return "TreeRow"; }
			[[nodiscard]] StyleId	  themeStyle() const override { return styles::listRow.theme; }

			void show(const TreeNode& node, int depth, float indent, bool selected) {
				setActive(selected);
				setLayout({
					.display = Display::Flex,
					.gap = kGap,
					.padding = {0.0F, kPadX, 0.0F, kPadX + static_cast<float>(depth) * indent},
					.alignItems = AlignItems::Center,
				});
				branch = !node.children.empty();
				expanded = node.expanded;
				if (Text* shown = text.get()) {
					shown->setText(node.label);
				}
				if (Text* shown = count.get()) {
					shown->setText(node.count ? std::to_string(*node.count) : std::string());
				}
				// The caret points right when closed and down when open; a leaf keeps its room.
				if (Chevron* shown = caret.get()) {
					shown->setVisible(branch);
					shown->setPointing(expanded ? Chevron::Pointing::Down : Chevron::Pointing::Right);
				}
			}

		  protected:
			void onMount() override {
				Control::onMount();
				setSize({kAuto, sizeMetrics(getContext()->getTheme(), ControlSize::Sm).controlHeight});
				caret = addChild<Component>({.size = {kCaret, kCaret}}).addChild<Chevron>({});
				text = addChild<Text>({.layout = {.flexGrow = 1.0F}, .style = styles::text::body});
				count = addChild<Text>({.style = styles::label});
			}

			void activate() override { pressed(); }

			void handleKey(KeyEvent& event) override {
				if (event.type == KeyEvent::Type::Navigate && branch &&
					((event.direction == Direction::Right && !expanded) || (event.direction == Direction::Left && expanded))) {
					if (auto* tree = dynamic_cast<TreeView*>(parent().get())) {
						tree->setExpanded(nodeId, !expanded);
					}
					event.consume();
					return;
				}
				Control::handleKey(event);
			}

		  private:
			void pressed() {
				if (auto* tree = dynamic_cast<TreeView*>(parent().get())) {
					tree->press(nodeId);
				}
			}

			std::string	 nodeId;
			bool		 branch = false;
			bool		 expanded = false;
			Ref<Text>	 text;
			Ref<Text>	 count;
			Ref<Chevron> caret;
		};

		TreeNode* findIn(std::vector<TreeNode>& nodes, const std::string& id) {
			for (TreeNode& node : nodes) {
				if (node.id == id) {
					return &node;
				}
				if (TreeNode* found = findIn(node.children, id)) {
					return found;
				}
			}
			return nullptr;
		}

	} // namespace

	TreeView::TreeView(const Args& args)
		: nodes(args.nodes),
		  indent(args.indent),
		  onSelect(args.onSelect) {}

	void TreeView::onMount() {
		Layout column = getLayout();
		column.display = Display::Flex;
		column.flexDirection = FlexDirection::Column;
		column.alignItems = AlignItems::Stretch;
		setLayout(column);
		setFocusGroup(FocusGroup{.orientation = FocusGroup::Orientation::Vertical});
		rebuild();
	}

	void TreeView::setNodes(std::vector<TreeNode> value) {
		nodes = std::move(value);
		rebuild();
	}

	TreeNode* TreeView::find(const std::string& id) {
		return findIn(nodes, id);
	}

	void TreeView::setExpanded(const std::string& id, bool expanded) {
		if (TreeNode* node = find(id); node != nullptr && node->expanded != expanded) {
			node->expanded = expanded;
			rebuild();
		}
	}

	void TreeView::press(const std::string& id) {
		TreeNode* node = find(id);
		if (node == nullptr) {
			return;
		}
		selectedId = id;
		if (!node->children.empty()) {
			node->expanded = !node->expanded;
		}
		rebuild();
		if (onSelect) {
			onSelect(id);
		}
	}

	std::vector<TreeView::Row> TreeView::visibleRows() const {
		std::vector<Row> rows;
		const auto		 walk = [&](const auto& self, const std::vector<TreeNode>& level, int depth) -> void {
			  for (const TreeNode& node : level) {
				  rows.push_back({.node = &node, .depth = depth});
				  if (node.expanded) {
					  self(self, node.children, depth + 1);
				  }
			  }
		};
		walk(walk, nodes, 0);
		return rows;
	}

	void TreeView::rebuild() {
		if (getContext() == nullptr) {
			return;
		}
		reconcileChildren(
			visibleRows(),
			[](const Row& row) { return row.node->id; },
			[&](const Row& row) -> Ref<Component> {
				Ref<TreeRow> made = addChild<TreeRow>({.id = "tree_" + row.node->id, .nodeId = row.node->id});
				made.get()->show(*row.node, row.depth, indent, row.node->id == selectedId);
				return made;
			},
			[&](Component& existing, const Row& row) {
				static_cast<TreeRow&>(existing).show(*row.node, row.depth, indent, row.node->id == selectedId);
			}
		);
	}

} // namespace ui
