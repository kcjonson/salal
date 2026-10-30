// The paint order (docs/ui-design.md, "Ordering"): its cases as a game meets them, and the keys
// reaching the batch through Primitives on the null backend. Headless; no GL.

#include "ui/Context.h"
#include "ui/shapes/Rectangle.h"

#include "BaseShader.h"
#include "backend/NullBackend.h"
#include "primitives/Primitives.h"

#include <gtest/gtest.h>

#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace ui {

	namespace {

		constexpr Foundation::Vec2 kViewport{800.0F, 600.0F};

		// Ids in paint order, bottom to top, with the unnamed left out.
		std::vector<std::string> painted(Context& context) {
			context.layout();
			std::vector<std::string> ids;
			for (const ComponentId id : context.paintOrder()) {
				if (const Component* component = context.find(id); component != nullptr && !component->getId().empty()) {
					ids.push_back(component->getId());
				}
			}
			return ids;
		}

		Component::Args box(const char* id, std::optional<int> zIndex = std::nullopt, bool isolation = false) {
			return {.id = id, .size = {10.0F, 10.0F}, .zIndex = zIndex, .isolation = isolation};
		}

		class UiOrderTest : public ::testing::Test {
		  protected:
			Context context{kViewport};
		};

	} // namespace

	TEST_F(UiOrderTest, WithNoZIndexItIsTreeOrderParentsFirst) {
		Ref<Component> a = context.root().addChild<Component>(box("a"));
		a.addChild<Component>(box("a1"));
		a.addChild<Component>(box("a2"));
		context.root().addChild<Component>(box("b"));
		EXPECT_EQ(painted(context), (std::vector<std::string>{"root", "a", "a1", "a2", "b"}));
	}

	// The card's zIndex raises it out of its hand over the hand's sibling, and its parts go with it.
	TEST_F(UiOrderTest, AHoveredCardPaintsOverTheResourceBarBesideItsHand) {
		Ref<Component> hand = context.root().addChild<Component>(box("hand"));
		hand.addChild<Component>(box("card1"));
		Ref<Component> hovered = hand.addChild<Component>(box("card2", 1));
		hovered.addChild<Component>(box("art"));
		Ref<Component> bar = context.root().addChild<Component>(box("resources"));
		bar.addChild<Component>(box("gold"));
		EXPECT_EQ(painted(context), (std::vector<std::string>{"root", "hand", "card1", "resources", "gold", "card2", "art"}));
	}

	TEST_F(UiOrderTest, TargetsPaintOverATargetingScrimAndTheRestOfTheBoardUnder) {
		Ref<Component> board = context.root().addChild<Component>(box("board"));
		board.addChild<Component>(box("tile"));
		board.addChild<Component>(box("target", 2));
		context.root().addChild<Component>(box("scrim", 1));
		EXPECT_EQ(painted(context), (std::vector<std::string>{"root", "board", "tile", "scrim", "target"}));
	}

	TEST_F(UiOrderTest, AZIndexInOneRootDoesNotCrossIntoAnother) {
		context.root().addChild<Component>(box("high", 100));
		Ref<Component> hud = context.addRoot<Component>({.id = "hud"});
		hud.addChild<Component>(box("low"));
		EXPECT_EQ(painted(context), (std::vector<std::string>{"root", "high", "hud", "low"}));
	}

	TEST_F(UiOrderTest, RootsPaintByZIndexThenTheOrderAdded) {
		context.addRoot<Component>({.id = "menu", .zIndex = 1});
		context.addRoot<Component>({.id = "game"});
		context.addRoot<Component>({.id = "toast", .zIndex = 1});
		EXPECT_EQ(painted(context), (std::vector<std::string>{"game", "menu", "toast"}));
	}

	TEST_F(UiOrderTest, AnIsolatedComponentPaintsAsOneAtItsZIndex) {
		Ref<Component> panel = context.root().addChild<Component>(box("panel", std::nullopt, true));
		panel.addChild<Component>(box("inner", 50));
		panel.addChild<Component>(box("under", -5));
		context.root().addChild<Component>(box("sibling", 1));
		EXPECT_EQ(painted(context), (std::vector<std::string>{"root", "panel", "under", "inner", "sibling"}));
	}

	// Inside a context, its root always paints first, so a negative zIndex goes under its parent but
	// never under the context.
	TEST_F(UiOrderTest, ANegativeZIndexPaintsUnderItsParentButOverItsContext) {
		Ref<Component> parent = context.root().addChild<Component>(box("parent"));
		parent.addChild<Component>(box("shadow", -1));
		EXPECT_EQ(painted(context), (std::vector<std::string>{"root", "shadow", "parent"}));
	}

	TEST_F(UiOrderTest, AnExplicitZIndexIsTheContextsNotTheParents) {
		Ref<Component> raised = context.root().addChild<Component>(box("raised", 3));
		raised.addChild<Component>(box("dropped", 0));
		context.root().addChild<Component>(box("between", 1));
		EXPECT_EQ(painted(context), (std::vector<std::string>{"root", "dropped", "between", "raised"}));
	}

	TEST_F(UiOrderTest, InvisibleSubtreesHaveNoPlaceAndChangesReorder) {
		Ref<Component> a = context.root().addChild<Component>(box("a"));
		a.addChild<Component>(box("a1"));
		Ref<Component> b = context.root().addChild<Component>(box("b"));
		a.get()->setVisible(false);
		EXPECT_EQ(painted(context), (std::vector<std::string>{"root", "b"}));
		a.get()->setVisible(true);
		a.get()->setZIndex(1);
		EXPECT_EQ(painted(context), (std::vector<std::string>{"root", "b", "a", "a1"}));
		context.root().get()->removeChild(b);
		EXPECT_EQ(painted(context), (std::vector<std::string>{"root", "a", "a1"}));
	}

	TEST_F(UiOrderTest, EachComponentsKeyIsItsRank) {
		context.root().addChild<Component>(box("top", 1));
		context.root().addChild<Component>(box("bottom"));
		context.layout();
		const std::vector<ComponentId>& order = context.paintOrder();
		for (std::size_t rank = 0; rank < order.size(); ++rank) {
			EXPECT_EQ(context.find(order[rank])->drawKey(), static_cast<std::int32_t>(rank));
		}
	}

	// --- Through the batch ---

	class UiDrawOrderTest : public ::testing::Test {
	  protected:
		void SetUp() override {
			auto owned = std::make_unique<Renderer::backend::NullBackend>();
			gpu = owned.get();
			Renderer::Primitives::init(std::move(owned), base_shader::source());
			Renderer::Primitives::setCoordinateSystem(nullptr);
			Renderer::Primitives::setViewport(800, 600);
			gpu->clearRecord();
		}

		void TearDown() override { Renderer::Primitives::shutdown(); }

		// Each rect's screen x, in the order the batch paints them: the index buffer's quads, each
		// naming its first vertex, whose position the transform baked in.
		std::vector<float> paintedX() const {
			EXPECT_EQ(gpu->draws().size(), 1U);
			const Renderer::backend::NullBackend::RecordedDraw& draw = gpu->draws().at(0);
			const Renderer::backend::GeometryDesc*				geometry = gpu->geometry(draw.call.geometry);
			const std::span<const uint8_t>						vertexBytes = gpu->bufferContents(geometry->vertices);
			const std::span<const uint8_t>						indexBytes = gpu->bufferContents(geometry->indices);
			std::vector<Renderer::BatchVertex>					vertices(vertexBytes.size() / sizeof(Renderer::BatchVertex));
			std::vector<uint32_t>								indices(indexBytes.size() / sizeof(uint32_t));
			std::memcpy(vertices.data(), vertexBytes.data(), vertices.size() * sizeof(Renderer::BatchVertex));
			std::memcpy(indices.data(), indexBytes.data(), indices.size() * sizeof(uint32_t));
			std::vector<float> xs;
			for (std::size_t quad = 0; quad * 6 < draw.call.indexCount; ++quad) {
				xs.push_back(vertices.at(indices.at(draw.call.indexOffset + quad * 6)).position.x);
			}
			return xs;
		}

		Renderer::backend::NullBackend* gpu = nullptr;
	};

	// Drawn depth first in tree order, under each parent's position; the keys put the card over the bar.
	TEST_F(UiDrawOrderTest, KeysReachTheBatchInPaintOrder) {
		Context		   context(kViewport);
		Ref<Component> hand = context.root().addChild<Component>({.position = {100.0F, 0.0F}, .size = {300.0F, 100.0F}});
		hand.addChild<Rectangle>({.position = {0.0F, 0.0F}, .size = {10.0F, 10.0F}});				// x 100
		hand.addChild<Rectangle>({.position = {20.0F, 0.0F}, .size = {10.0F, 10.0F}, .zIndex = 1}); // x 120
		context.root().addChild<Rectangle>({.position = {400.0F, 0.0F}, .size = {10.0F, 10.0F}});	// x 400
		context.layout();

		Renderer::Primitives::beginFrame();
		context.render();
		Renderer::Primitives::endFrame();

		EXPECT_EQ(paintedX(), (std::vector<float>{100.0F, 400.0F, 120.0F}));
	}

} // namespace ui
