// perf_mask: masked draws (docs/masks.md) as worldsim's player visibility cover draws them, two a
// frame over the whole window. A dim fill masked by the view minus four sight polygons, then an
// opaque fill masked by the view minus a seen mesh of 3,072 triangles and three pending polygons,
// over a background of tiles. Fixed content, moved by the tick, so every frame of a tick is the same.

#include "Scenes.h"

#include <graphics/Color.h>
#include <primitives/MaskImage.h>
#include <primitives/Primitives.h>
#include <vector/Tessellator.h>
#include <vector/Types.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <vector>

namespace {

	namespace Primitives = Renderer::Primitives;

	constexpr const char* kSceneName = "perf_mask";
	constexpr float		  kPi = std::numbers::pi_v<float>;
	constexpr int		  kSeenPoints = 3074; // a simple outline tessellates to points - 2 triangles
	constexpr int		  kSightPoints = 48;
	constexpr int		  kPendingPoints = 64;
	constexpr float		  kTile = 64.0F;
	constexpr float		  kSeenDimAlpha = 0.6F;
	constexpr int32_t	  kCoverKey = 1000;

	const Foundation::Color kUnseenColor(0.13F, 0.15F, 0.19F, 1.0F);

	// The seen area round the world origin: a ragged outline about two metres across, every point a
	// slightly different reach, tessellated once.
	renderer::TessellatedMesh seenArea() {
		std::vector<Foundation::Vec2> outline;
		for (int index = 0; index < kSeenPoints; ++index) {
			const float angle = 2.0F * kPi * static_cast<float>(index) / kSeenPoints;
			const float reach =
				1.0F + (0.2F * std::sin(7.0F * angle)) + (0.08F * std::cos((23.0F * angle) + 1.0F)) + (0.015F * std::sin(181.0F * angle));
			outline.emplace_back(reach * 1.5F * std::cos(angle), reach * std::sin(angle));
		}
		renderer::TessellatedMesh mesh;
		renderer::Tessellator	  tessellator;
		tessellator.Tessellate(renderer::VectorPath(outline), mesh);
		mesh.colors.assign(mesh.vertices.size(), Foundation::Color::white());
		return mesh;
	}

	// A closed outline of `points` round `centre`, `radius` across, wobbled by `wobble`.
	std::vector<Foundation::Vec2> blob(Foundation::Vec2 centre, float radius, int points, float wobble) {
		std::vector<Foundation::Vec2> outline;
		for (int index = 0; index < points; ++index) {
			const float angle = 2.0F * kPi * static_cast<float>(index) / static_cast<float>(points);
			const float reach = radius * (1.0F + (wobble * std::sin((5.0F * angle) + radius)));
			outline.push_back(centre + (reach * Foundation::Vec2(std::cos(angle), std::sin(angle))));
		}
		return outline;
	}

	class PerfMaskScene final : public app::Scene {
	  public:
		const char* name() const override { return kSceneName; }

		void onEnter(app::App& /*app*/) override { seen = Primitives::uploadInstancedMesh(seenArea(), 1); }

		void onExit() override { seen = {}; }

		void fixedUpdate(const app::TickContext& context) override { tick = context.tick; }

		void render(float /*alpha*/) override {
			int windowWidth = 0;
			int windowHeight = 0;
			Primitives::getLogicalViewport(windowWidth, windowHeight);
			const float width = static_cast<float>(windowWidth);
			const float height = static_cast<float>(windowHeight);
			const auto	time = static_cast<float>(tick) / 60.0F;

			drawBackground(width, height);

			const Primitives::WorldCamera camera{
				.position = {0.2F * std::sin(time * 0.3F), 0.15F * std::cos(time * 0.4F)},
				.pixelsPerMeter = std::min(width, height) * 0.3F,
			};
			const auto toScreen = [&](Foundation::Vec2 world) {
				return ((world - camera.position) * camera.zoom * camera.pixelsPerMeter) + Foundation::Vec2(width * 0.5F, height * 0.5F);
			};

			std::array<std::vector<Foundation::Vec2>, 4> sights;
			for (size_t sight = 0; sight < sights.size(); ++sight) {
				const auto			   phase = static_cast<float>(sight);
				const Foundation::Vec2 centre(
					(-0.9F + (0.6F * phase)) + (0.2F * std::sin((time * 0.6F) + phase)), 0.4F * std::cos((time * 0.5F) + (phase * 1.7F))
				);
				sights[sight] = blob(toScreen(centre), 0.35F * camera.pixelsPerMeter, kSightPoints, 0.0F);
			}
			std::array<std::vector<Foundation::Vec2>, 3> pendings;
			for (size_t pending = 0; pending < pendings.size(); ++pending) {
				const auto			   phase = static_cast<float>(pending);
				const Foundation::Vec2 centre((-1.2F + (1.2F * phase)), 0.9F * std::sin((time * 0.4F) + phase));
				pendings[pending] = blob(toScreen(centre), 0.3F * camera.pixelsPerMeter, kPendingPoints, 0.15F);
			}

			// The window exactly, as worldsim's view is.
			const Foundation::Rect view{0.0F, 0.0F, width, height};

			std::vector<Primitives::MaskLayer> dimMask = {
				{.image = Primitives::MaskImage::rect(view), .composite = Primitives::MaskComposite::Subtract}
			};
			for (const std::vector<Foundation::Vec2>& outline : sights) {
				dimMask.push_back({.image = Primitives::MaskImage::polygon(outline)});
			}
			Primitives::drawRect({.bounds = view, .style = {.fill = {0.0F, 0.0F, 0.0F, kSeenDimAlpha}}, .mask = dimMask, .key = kCoverKey});

			std::vector<Primitives::MaskLayer> unseenMask = {
				{.image = Primitives::MaskImage::rect(view), .composite = Primitives::MaskComposite::Subtract},
				{.image = Primitives::MaskImage::mesh(seen, camera)},
			};
			for (const std::vector<Foundation::Vec2>& outline : pendings) {
				unseenMask.push_back({.image = Primitives::MaskImage::polygon(outline)});
			}
			Primitives::drawRect({.bounds = view, .style = {.fill = kUnseenColor}, .mask = unseenMask, .key = kCoverKey + 1});
		}

	  private:
		static void drawBackground(float width, float height) {
			const int columns = static_cast<int>(std::ceil(width / kTile));
			const int rows = static_cast<int>(std::ceil(height / kTile));
			for (int row = 0; row < rows; ++row) {
				for (int column = 0; column < columns; ++column) {
					const float across = static_cast<float>(column) / static_cast<float>(columns);
					const float down = static_cast<float>(row) / static_cast<float>(rows);
					const float shade = ((row + column) % 2 == 0) ? 1.0F : 0.8F;
					Primitives::drawRect(
						{.bounds = {static_cast<float>(column) * kTile, static_cast<float>(row) * kTile, kTile, kTile},
						 .style = {.fill = {shade * (0.3F + (0.6F * across)), shade * 0.6F, shade * (0.3F + (0.6F * down)), 1.0F}}}
					);
				}
			}
		}

		Renderer::InstancedMeshHandle seen;
		uint64_t					  tick = 0;
	};

} // namespace

namespace sandbox {
	const SceneInfo kPerfMaskScene{.name = kSceneName, .make = makeScene<PerfMaskScene>};
} // namespace sandbox
