// perf_world: a world's batches, with the flushes, barriers and clips between them. Under a camera that
// pans and zooms: 120 stroked roads, worldsim's VectorPerf 10,000 stars (pre-tessellated, drawn with
// drawTriangles inside a clip), a flush, 3,000 instanced trees, 400 rotating polygons from six cached
// silhouettes sorted by z, a barrier, 200 labels; then, in screen space, four clipped panels of
// rects and text. Fixed content from a fixed seed; the camera and the rotation are functions of the tick.

#include "Scenes.h"

#include <graphics/Color.h>
#include <primitives/Primitives.h>
#include <random/Rng.h>
#include <vector/Tessellator.h>
#include <vector/Types.h>

#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

	constexpr const char* kSceneName = "perf_world";
	constexpr uint32_t	  kSeed = 0x5A1A1003;
	constexpr float		  kPi = std::numbers::pi_v<float>;
	constexpr float		  kWorldWidth = 2400.0F;
	constexpr float		  kWorldHeight = 1350.0F;
	constexpr int		  kStars = 10000;
	constexpr int		  kRoads = 120;
	constexpr int		  kRoadPoints = 60;
	constexpr int		  kEntities = 400;
	constexpr int		  kTrees = 3000;
	constexpr int		  kLabels = 200;
	constexpr int		  kPanelDots = 60;
	constexpr float		  kStarClipMargin = 100.0F; // VectorPerf's
	constexpr float		  kPanelSize = 220.0F;

	const Foundation::Color kBackground(0.05F, 0.05F, 0.1F, 1.0F);
	const Foundation::Color kPanel(0.1F, 0.1F, 0.14F, 0.92F);
	const Foundation::Color kInk(0.85F, 0.85F, 0.9F, 1.0F);

	struct Star {
		renderer::TessellatedMesh mesh;
		Foundation::Color		  color;
	};

	struct Road {
		std::vector<Foundation::Vec2> points;
		float						  width = 1.0F;
		Foundation::Color			  color;
	};

	struct Entity {
		Foundation::Vec2  position;
		std::size_t		  silhouette = 0;
		float			  scale = 1.0F;
		float			  spin = 0.0F; // radians a tick
		Foundation::Color color;
		bool			  bordered = false;
	};

	struct Label {
		std::string		 text;
		Foundation::Vec2 position;
	};

	// A closed outline of `points` points, alternating `outer` and `inner` from the centre.
	std::vector<Foundation::Vec2> starOutline(Foundation::Vec2 centre, int points, float outer, float inner) {
		std::vector<Foundation::Vec2> outline;
		for (int i = 0; i < points * 2; ++i) {
			const float angle = (static_cast<float>(i) * kPi / static_cast<float>(points)) - (kPi / 2.0F);
			const float radius = (i % 2 == 0) ? outer : inner;
			outline.emplace_back(centre.x + (radius * std::cos(angle)), centre.y + (radius * std::sin(angle)));
		}
		return outline;
	}

	class PerfWorldScene final : public app::Scene {
	  public:
		PerfWorldScene() {
			foundation::Rng		  rng(kSeed);
			renderer::Tessellator tessellator;

			stars.reserve(kStars);
			for (int i = 0; i < kStars; ++i) {
				const float			 x = kWorldWidth * rng.nextFloat();
				const float			 y = kWorldHeight * rng.nextFloat();
				const float			 outer = 8.0F + (17.0F * rng.nextFloat());
				const float			 hue = rng.nextFloat();
				Star				 star{.color = {hue, 1.0F - (hue * 0.5F), 0.3F + (hue * 0.4F), 1.0F}};
				renderer::VectorPath path;
				path.vertices = starOutline({x, y}, 5, outer, outer * 0.4F);
				if (tessellator.Tessellate(path, star.mesh)) {
					stars.push_back(std::move(star));
				}
			}

			for (int i = 0; i < kRoads; ++i) {
				Road			 road;
				Foundation::Vec2 at{kWorldWidth * rng.nextFloat(), kWorldHeight * rng.nextFloat()};
				float			 heading = 2.0F * kPi * rng.nextFloat();
				for (int p = 0; p < kRoadPoints; ++p) {
					road.points.push_back(at);
					heading += (rng.nextFloat() - 0.5F) * 0.6F;
					const float step = 20.0F + (20.0F * rng.nextFloat());
					at += Foundation::Vec2(step * std::cos(heading), step * std::sin(heading));
				}
				road.width = 2.0F + (6.0F * rng.nextFloat());
				const float shade = rng.nextFloat();
				road.color = {0.35F + (0.2F * shade), 0.3F + (0.15F * shade), 0.25F, 1.0F};
				roads.push_back(std::move(road));
			}

			silhouettes = {
				starOutline({0.0F, 0.0F}, 3, 12.0F, 5.0F),
				starOutline({0.0F, 0.0F}, 6, 12.0F, 8.0F),
				{{-10.0F, 8.0F}, {-10.0F, -4.0F}, {0.0F, -12.0F}, {10.0F, -4.0F}, {10.0F, 8.0F}},
				{{-12.0F, -6.0F}, {6.0F, -6.0F}, {6.0F, -12.0F}, {14.0F, 0.0F}, {6.0F, 12.0F}, {6.0F, 6.0F}, {-12.0F, 6.0F}},
				{{-10.0F, -10.0F}, {-2.0F, -10.0F}, {-2.0F, 2.0F}, {10.0F, 2.0F}, {10.0F, 10.0F}, {-10.0F, 10.0F}},
				{{0.0F, -12.0F}, {11.0F, 6.0F}, {-11.0F, 6.0F}},
			};
			for (int i = 0; i < kEntities; ++i) {
				Entity entity;
				entity.position = {kWorldWidth * rng.nextFloat(), kWorldHeight * rng.nextFloat()};
				entity.silhouette = static_cast<std::size_t>(rng.nextInt(0, static_cast<int>(silhouettes.size()) - 1));
				entity.scale = 0.8F + (0.8F * rng.nextFloat());
				entity.spin = (rng.nextFloat() - 0.5F) * 0.1F;
				entity.color = {0.5F + (0.5F * rng.nextFloat()), 0.4F + (0.4F * rng.nextFloat()), 0.3F, 1.0F};
				entity.bordered = rng.nextFloat() < 0.33F;
				entities.push_back(entity);
			}

			trees.reserve(kTrees);
			for (int i = 0; i < kTrees; ++i) {
				const Foundation::Vec2 at{kWorldWidth * rng.nextFloat(), kWorldHeight * rng.nextFloat()};
				const float			   scale = 0.8F + (0.6F * rng.nextFloat());
				const float			   green = 0.4F + (0.4F * rng.nextFloat());
				trees.emplace_back(at, 0.0F, scale, Foundation::Color(0.2F, green, 0.25F, 1.0F));
			}

			for (int i = 0; i < kLabels; ++i) {
				const Foundation::Vec2 at{kWorldWidth * rng.nextFloat(), kWorldHeight * rng.nextFloat()};
				labels.push_back({.text = "site " + std::to_string(i), .position = at});
			}

			for (int i = 0; i < kPanelDots; ++i) {
				const float x = rng.nextFloat();
				const float y = rng.nextFloat();
				panelDots.emplace_back(x, y);
			}
		}

		const char* name() const override { return kSceneName; }

		void onEnter(app::App& /*app*/) override {
			renderer::TessellatedMesh tree;
			renderer::VectorPath	  outline;
			outline.vertices = starOutline({0.0F, 0.0F}, 7, 9.0F, 5.0F);
			renderer::Tessellator().Tessellate(outline, tree);
			treeMesh = Renderer::Primitives::uploadInstancedMesh(tree, kTrees);
		}

		void onExit() override { Renderer::Primitives::releaseInstancedMesh(treeMesh); }

		void fixedUpdate(const app::TickContext& context) override { tick = context.tick; }

		void render(float /*alpha*/) override {
			namespace P = Renderer::Primitives;
			int windowWidth = 0;
			int windowHeight = 0;
			P::getLogicalViewport(windowWidth, windowHeight);
			const float w = static_cast<float>(windowWidth);
			const float h = static_cast<float>(windowHeight);
			const auto	t = static_cast<float>(tick);

			const P::WorldCamera camera{
				.position =
					{(kWorldWidth * 0.5F) + (300.0F * std::sin(t * 0.005F)), (kWorldHeight * 0.5F) + (200.0F * std::cos(t * 0.004F))},
				.zoom = 1.0F + (0.15F * std::sin(t * 0.01F)),
			};
			Foundation::Mat4 view = glm::translate(Foundation::Mat4(1.0F), {w * 0.5F, h * 0.5F, 0.0F});
			view = glm::scale(view, {camera.zoom, camera.zoom, 1.0F});
			view = glm::translate(view, {-camera.position.x, -camera.position.y, 0.0F});

			P::clear(kBackground);
			P::PushTransform(view);

			for (const Road& road : roads) {
				P::drawPolyline({.points = road.points, .width = road.width, .color = road.color, .cap = P::LineCap::Round});
			}
			P::pushClip({kStarClipMargin, kStarClipMargin, w - (2.0F * kStarClipMargin), h - (2.0F * kStarClipMargin)});
			for (const Star& star : stars) {
				P::drawTriangles(
					{.vertices = star.mesh.vertices.data(),
					 .indices = star.mesh.indices.data(),
					 .vertexCount = star.mesh.vertices.size(),
					 .indexCount = star.mesh.indices.size(),
					 .color = star.color}
				);
			}
			P::popClip();

			// The ground pass is over: what follows draws on top of it outside the batch.
			P::flush();
			P::drawInstanced(treeMesh, trees.data(), static_cast<uint32_t>(trees.size()), camera);

			for (const Entity& entity : entities) {
				Foundation::Mat4 place = glm::translate(Foundation::Mat4(1.0F), {entity.position.x, entity.position.y, 0.0F});
				place = glm::rotate(place, entity.spin * t, {0.0F, 0.0F, 1.0F});
				place = glm::scale(place, {entity.scale, entity.scale, 1.0F});
				P::PushTransform(place);
				P::drawPolygon(
					{.vertices = silhouettes[entity.silhouette],
					 .style =
						 {.fill = entity.color,
						  .border = entity.bordered ? std::optional<Foundation::LineStyle>({.color = kInk, .width = 1.5F}) : std::nullopt},
					 .zIndex = static_cast<int>(entity.position.y)}
				);
				P::PopTransform();
			}

			P::sortBarrier();
			for (const Label& label : labels) {
				P::drawText({.text = label.text, .position = label.position, .scale = 0.875F, .color = kInk});
			}
			P::PopTransform();

			// Screen-space panels, each its own clip.
			P::sortBarrier();
			const std::array<Foundation::Vec2, 4> corners{
				Foundation::Vec2{10.0F, 10.0F},
				Foundation::Vec2{w - kPanelSize - 10.0F, 10.0F},
				Foundation::Vec2{10.0F, h - kPanelSize - 10.0F},
				Foundation::Vec2{w - kPanelSize - 10.0F, h - kPanelSize - 10.0F},
			};
			for (std::size_t p = 0; p < corners.size(); ++p) {
				const Foundation::Vec2 at = corners[p];
				P::pushClip({at.x, at.y, kPanelSize, kPanelSize});
				P::drawRect(
					{.bounds = {at.x, at.y, kPanelSize, kPanelSize},
					 .style = {.fill = kPanel, .border = Foundation::BorderStyle{.color = kInk, .width = 1.0F, .cornerRadius = 6.0F}}}
				);
				for (const Foundation::Vec2& dot : panelDots) {
					P::drawRect(
						{.bounds =
							 {at.x + 10.0F + (dot.x * (kPanelSize - 30.0F)), at.y + 40.0F + (dot.y * (kPanelSize - 60.0F)), 6.0F, 6.0F},
						 .style = {.fill = {0.4F + (0.15F * static_cast<float>(p)), 0.8F, 0.5F, 1.0F}}}
					);
				}
				P::drawText({.text = "panel " + std::to_string(p), .position = {at.x + 10.0F, at.y + 10.0F}, .color = kInk});
				P::popClip();
			}
		}

		std::string exportState() const override {
			return R"({"stars":)" + std::to_string(stars.size()) + R"(,"roads":)" + std::to_string(roads.size()) + R"(,"entities":)" +
				   std::to_string(entities.size()) + R"(,"trees":)" + std::to_string(trees.size()) + R"(,"labels":)" +
				   std::to_string(labels.size()) + "}";
		}

	  private:
		std::vector<Star>						   stars;
		std::vector<Road>						   roads;
		std::vector<std::vector<Foundation::Vec2>> silhouettes;
		std::vector<Entity>						   entities;
		std::vector<Renderer::InstanceData>		   trees;
		std::vector<Label>						   labels;
		std::vector<Foundation::Vec2>			   panelDots;
		Renderer::InstancedMeshHandle			   treeMesh;
		uint64_t								   tick = 0;
	};

} // namespace

namespace sandbox {
	const SceneInfo kPerfWorldScene{.name = kSceneName, .make = makeScene<PerfWorldScene>};
} // namespace sandbox
