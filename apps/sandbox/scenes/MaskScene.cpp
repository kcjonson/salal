// mask: CSS masks on drawRect (docs/masks.md), drawn as worldsim's player visibility cover is. A busy
// background; a "seen" blob, an uploaded mesh in world space; two overlapping sight circles moving
// inside it. A translucent dark fill masked by the view minus the sights dims what was seen but is
// out of sight, then an opaque fill masked by the view minus the seen mesh covers what was never
// seen. Inside the sights the background shows untouched. The labels draw unmasked over it all.

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

	constexpr const char* kSceneName = "mask";
	constexpr float		  kPi = std::numbers::pi_v<float>;
	constexpr int		  kBlobPoints = 96;
	constexpr int		  kSightPoints = 48;
	constexpr float		  kSightRadius = 0.42F; // world meters
	constexpr float		  kTile = 48.0F;
	constexpr float		  kSeenDimAlpha = 0.6F;
	constexpr int32_t	  kCoverKey = 1000;

	const Foundation::Color kUnseenColor(0.13F, 0.15F, 0.19F, 1.0F);

	// The seen area: a lumpy blob round the world origin, a meter or so across, tessellated once.
	renderer::TessellatedMesh seenBlob() {
		std::vector<Foundation::Vec2> outline;
		for (int index = 0; index < kBlobPoints; ++index) {
			const float angle = 2.0F * kPi * static_cast<float>(index) / kBlobPoints;
			const float reach = 1.0F + (0.18F * std::sin(3.0F * angle)) + (0.1F * std::cos((5.0F * angle) + 1.0F));
			outline.emplace_back(reach * 1.3F * std::cos(angle), reach * std::sin(angle));
		}
		renderer::TessellatedMesh mesh;
		renderer::Tessellator	  tessellator;
		tessellator.Tessellate(renderer::VectorPath(outline), mesh);
		mesh.colors.assign(mesh.vertices.size(), Foundation::Color::white());
		return mesh;
	}

	class MaskScene final : public app::Scene {
	  public:
		const char* name() const override { return kSceneName; }

		void onEnter(app::App& /*app*/) override { seen = Primitives::uploadInstancedMesh(seenBlob(), 1); }

		void onExit() override { seen = {}; }

		void update(const app::FrameContext& frame) override { seconds += frame.dt; }

		void render(float /*alpha*/) override {
			int windowWidth = 0;
			int windowHeight = 0;
			Primitives::getLogicalViewport(windowWidth, windowHeight);
			const float width = static_cast<float>(windowWidth);
			const float height = static_cast<float>(windowHeight);
			const auto	time = static_cast<float>(seconds);

			drawBackground(width, height);

			// The world origin at the view's centre, the blob filling about two thirds of its height.
			const Primitives::WorldCamera camera{.position = {0.0F, 0.0F}, .pixelsPerMeter = std::min(width, height) * 0.32F};
			const auto					  toScreen = [&](Foundation::Vec2 world) {
				   return ((world - camera.position) * camera.zoom * camera.pixelsPerMeter) + Foundation::Vec2(width * 0.5F, height * 0.5F);
			};

			// Two sights on their own paths inside the blob, so their overlap keeps changing.
			const std::array<Foundation::Vec2, 2> sightCentres = {
				Foundation::Vec2(-0.35F + (0.3F * std::sin(time * 0.6F)), 0.25F * std::sin(time * 0.9F)),
				Foundation::Vec2(0.35F + (0.3F * std::cos(time * 0.5F)), 0.3F * std::cos(time * 0.7F)),
			};
			std::array<std::vector<Foundation::Vec2>, 2> sightOutlines;
			for (size_t sight = 0; sight < sightCentres.size(); ++sight) {
				for (int index = 0; index < kSightPoints; ++index) {
					const float angle = 2.0F * kPi * static_cast<float>(index) / kSightPoints;
					sightOutlines[sight].push_back(
						toScreen(sightCentres[sight] + (kSightRadius * Foundation::Vec2(std::cos(angle), std::sin(angle))))
					);
				}
			}

			// Past the window, so the fills' anti-aliased edges fall outside it.
			const Foundation::Rect view{-4.0F, -4.0F, width + 8.0F, height + 8.0F};

			std::vector<Primitives::MaskLayer> dimMask = {
				{.image = Primitives::MaskImage::rect(view), .composite = Primitives::MaskComposite::Subtract}
			};
			for (const std::vector<Foundation::Vec2>& outline : sightOutlines) {
				dimMask.push_back({.image = Primitives::MaskImage::polygon(outline)});
			}
			Primitives::drawRect({.bounds = view, .style = {.fill = {0.0F, 0.0F, 0.0F, kSeenDimAlpha}}, .mask = dimMask, .key = kCoverKey});

			const std::vector<Primitives::MaskLayer> unseenMask = {
				{.image = Primitives::MaskImage::rect(view), .composite = Primitives::MaskComposite::Subtract},
				{.image = Primitives::MaskImage::mesh(seen, camera)},
			};
			Primitives::drawRect({.bounds = view, .style = {.fill = kUnseenColor}, .mask = unseenMask, .key = kCoverKey + 1});

			const Foundation::Color ink(0.85F, 0.88F, 0.92F, 1.0F);
			Primitives::drawText(
				{.text = "mask: unseen covered, seen dimmed, in sight clear",
				 .position = {24.0F, 20.0F},
				 .scale = 1.5F,
				 .color = ink,
				 .key = kCoverKey + 2}
			);
			Primitives::drawText(
				{.text = "dim: the view minus two sight polygons    unseen: the view minus the seen mesh",
				 .position = {24.0F, height - 44.0F},
				 .scale = 1.25F,
				 .color = ink,
				 .key = kCoverKey + 2}
			);
		}

	  private:
		// Coloured tiles, diagonal bars and rings: anything the cover hides or lets through shows.
		static void drawBackground(float width, float height) {
			const int columns = static_cast<int>(std::ceil(width / kTile));
			const int rows = static_cast<int>(std::ceil(height / kTile));
			for (int row = 0; row < rows; ++row) {
				for (int column = 0; column < columns; ++column) {
					const float across = static_cast<float>(column) / static_cast<float>(columns);
					const float down = static_cast<float>(row) / static_cast<float>(rows);
					const float shade = ((row + column) % 2 == 0) ? 1.0F : 0.75F;
					Primitives::drawRect(
						{.bounds = {static_cast<float>(column) * kTile, static_cast<float>(row) * kTile, kTile, kTile},
						 .style = {
							 .fill = {
								 shade * (0.3F + (0.7F * across)), shade * (0.8F - (0.5F * down)), shade * (0.3F + (0.6F * down)), 1.0F
							 }
						 }}
					);
				}
			}
			for (float x = -height; x < width; x += 90.0F) {
				const std::array<Foundation::Vec2, 2> bar = {Foundation::Vec2(x, height), Foundation::Vec2(x + height, 0.0F)};
				Primitives::drawPolyline({.points = bar, .width = 10.0F, .color = {1.0F, 0.95F, 0.6F, 0.8F}});
			}
			for (int ring = 0; ring < 9; ++ring) {
				Primitives::drawCircle(
					{.center = {width * (0.1F + (0.1F * static_cast<float>(ring))), height * (ring % 2 == 0 ? 0.3F : 0.7F)},
					 .radius = 40.0F,
					 .style = {
						 .fill = {0.0F, 0.0F, 0.0F, 0.0F},
						 .border = Foundation::BorderStyle{.color = {1.0F, 1.0F, 1.0F, 0.9F}, .width = 8.0F}
					 }}
				);
			}
		}

		Renderer::InstancedMeshHandle seen;
		double						  seconds = 0.0;
	};

} // namespace

namespace sandbox {
	const SceneInfo kMaskScene{.name = kSceneName, .make = makeScene<MaskScene>};
} // namespace sandbox
