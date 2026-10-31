// The tree snapshot's schema: the same fields on every node, null for what is unknown or doesn't
// apply, local and screen bounds, and a bad id that doesn't throw. After worldsim's
// libs/ui/debug/UiTreeSerializer.test.cpp (a4226e17).

#include "ui/debug/TreeSnapshot.h"

#include "ui/Context.h"
#include "ui/shapes/Rectangle.h"
#include "ui/shapes/Text.h"

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

namespace ui {

	namespace {
		constexpr Foundation::Vec2 kViewport{800.0F, 600.0F};
	} // namespace

	TEST(UiTreeSnapshotTest, ASceneWithoutUiHasNoRoots) {
		const nlohmann::json tree = nlohmann::json::parse(treeJson(nullptr, "menu", kViewport));
		EXPECT_EQ(tree["scene"], "menu");
		EXPECT_EQ(tree["viewport"]["width"], 800);
		EXPECT_EQ(tree["viewport"]["height"], 600);
		EXPECT_TRUE(tree["roots"].empty());
	}

	TEST(UiTreeSnapshotTest, EveryNodeHasTheSameFieldsWithNullForUnknowns) {
		Context		   context(kViewport);
		Ref<Component> panel = context.root().addChild<Component>({.id = "panel", .position = {100.0F, 50.0F}, .size = {200.0F, 100.0F}});
		panel.addChild<Rectangle>({
			.position = {10.0F, 10.0F},
			.size = {20.0F, 20.0F},
			.style = {.backgroundColor = Foundation::Color::red(), .boxShadow = Foundation::BoxShadow{.blur = 4.0F}, .opacity = 0.5F},
			.zIndex = 2,
		});
		panel.addChild<Text>({.size = {50.0F, 20.0F}, .text = "hello"});
		context.layout();

		const nlohmann::json tree = nlohmann::json::parse(treeJson(&context, "game", kViewport));
		ASSERT_EQ(tree["roots"].size(), 1U);
		const nlohmann::json& root = tree["roots"][0];
		EXPECT_EQ(root["id"], "root");
		EXPECT_EQ(root["isolation"], true);
		const nlohmann::json& panelNode = root["children"][0];
		const nlohmann::json& rect = panelNode["children"][0];
		const nlohmann::json& text = panelNode["children"][1];

		for (const nlohmann::json* node : {&root, &panelNode, &rect, &text}) {
			for (const char* field :
				 {"id",		   "type",			"bounds",		  "screenBounds", "margin", "zIndex",		 "isolation",
				  "key",	   "visible",		"enabled",		  "opacity",	  "clip",	"contentOffset", "transform",
				  "focusable", "pointerEvents", "handlesPointer", "state",		  "text",	"value",		 "style",
				  "inkBounds", "children"}) {
				EXPECT_TRUE(node->contains(field)) << field;
			}
		}
		EXPECT_TRUE(panelNode["zIndex"].is_null());
		EXPECT_TRUE(rect["id"].is_null());
		EXPECT_TRUE(rect["text"].is_null());
		EXPECT_TRUE(rect["clip"].is_null());
		EXPECT_EQ(rect["type"], "Rectangle");
		EXPECT_EQ(rect["zIndex"], 2);
		EXPECT_EQ(rect["bounds"]["x"], 10.0F);
		EXPECT_EQ(rect["screenBounds"]["x"], 110.0F);
		EXPECT_EQ(rect["screenBounds"]["y"], 60.0F);
		EXPECT_EQ(rect["inkBounds"]["x"], 106.0F);
		EXPECT_EQ(rect["opacity"], 0.5F);
		EXPECT_EQ(rect["style"]["fill"], nlohmann::json::array({1.0F, 0.0F, 0.0F, 1.0F}));
		EXPECT_TRUE(rect["style"]["border"].is_null());
		EXPECT_EQ(text["type"], "Text");
		EXPECT_EQ(text["text"]["content"], "hello");
		EXPECT_GT(rect["key"].get<int>(), text["key"].get<int>()); // the zIndex lifts it over the text after it
	}

	TEST(UiTreeSnapshotTest, ABadIdIsReplacedNotThrown) {
		Context context(kViewport);
		context.root().addChild<Component>({.id = "bad\xff", .size = {1.0F, 1.0F}});
		context.layout();
		std::string json;
		EXPECT_NO_THROW(json = treeJson(&context, "game", kViewport));
		EXPECT_NO_THROW(static_cast<void>(nlohmann::json::parse(json)));
	}

} // namespace ui
