// mask: the stencil mask (docs/masks.md) over a busy background. Two overlapping shapes move across it:
// a disk, an uploaded mesh drawn with drawInstanced writing 1, and a star, a triangle fan drawn with
// drawTriangles writing 2 over it. Then a flat opaque fill lands where the mask is 0, outside both,
// and a translucent dark fill where it is 1, inside the disk but outside the star; inside the star the
// background shows untouched. The labels draw after the mask is off.

#include "Scenes.h"

#include <graphics/Color.h>
#include <primitives/Primitives.h>
#include <vector/Types.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <vector>

namespace {

	constexpr const char* kSceneName = "mask";
	constexpr float		  kPi = std::numbers::pi_v<float>;
	constexpr int		  kDiskSegments = 64;
	constexpr int		  kStarPoints = 7;
	constexpr float		  kTile = 48.0F;

	namespace P = Renderer::Primitives;

	// A disk of `radius` round the origin as a fan, for uploadInstancedMesh.
	renderer::TessellatedMesh diskMesh(float radius) {
		renderer::TessellatedMesh mesh;
		mesh.vertices.emplace_back(0.0F, 0.0F);
		for (int i = 0; i < kDiskSegments; ++i) {
			const float angle = 2.0F * kPi * static_cast<float>(i) / kDiskSegments;
			mesh.vertices.emplace_back(radius * std::cos(angle), radius * std::sin(angle));
		}
		for (int i = 0; i < kDiskSegments; ++i) {
			mesh.indices.push_back(0);
			mesh.indices.push_back(static_cast<uint16_t>(1 + i));
			mesh.indices.push_back(static_cast<uint16_t>(1 + ((i + 1) % kDiskSegments)));
		}
		mesh.colors.assign(mesh.vertices.size(), Foundation::Color::white());
		return mesh;
	}

	class MaskScene final : public app::Scene {
	  public:
		const char* name() const override { return kSceneName; }

		void onEnter(app::App& /*app*/) override { disk = P::uploadInstancedMesh(diskMesh(1.0F), 1); }

		void onExit() override { disk = {}; }

		void update(const app::FrameContext& frame) override { seconds += frame.dt; }

		void render(float /*alpha*/) override {
			int windowWidth = 0;
			int windowHeight = 0;
			P::getLogicalViewport(windowWidth, windowHeight);
			const float width = static_cast<float>(windowWidth);
			const float height = static_cast<float>(windowHeight);
			const auto	t = static_cast<float>(seconds);

			drawBackground(width, height);

			// The shapes move on their own Lissajous paths, so their overlap keeps changing.
			const float			   radius = std::min(width, height) * 0.3F;
			const Foundation::Vec2 diskCentre{
				(width * 0.42F) + (width * 0.12F * std::sin(t * 0.5F)), (height * 0.5F) + (height * 0.1F * std::sin(t * 0.7F))
			};
			const Foundation::Vec2 starCentre{
				(width * 0.6F) + (width * 0.14F * std::cos(t * 0.4F)), (height * 0.5F) + (height * 0.18F * std::sin(t * 0.9F))
			};

			P::clearMask(0);
			P::setMask({.mode = P::MaskMode::Write, .value = 1});
			// World equals screen: the camera at the viewport's centre, one pixel a metre.
			const Renderer::InstanceData instance(diskCentre, 0.0F, radius, Foundation::Color::white());
			P::drawInstanced(disk, &instance, 1, {.position = {width * 0.5F, height * 0.5F}});
			P::setMask({.mode = P::MaskMode::Write, .value = 2});
			drawStar(starCentre, radius * 0.85F, t * 0.3F);
			// The fills run past the window, so their anti-aliased edges fall outside it.
			const Foundation::Rect view{-4.0F, -4.0F, width + 8.0F, height + 8.0F};
			P::setMask({.mode = P::MaskMode::Test, .value = 0});
			P::drawRect({.bounds = view, .style = {.fill = {0.16F, 0.18F, 0.22F, 1.0F}}});
			P::setMask({.mode = P::MaskMode::Test, .value = 1});
			P::drawRect({.bounds = view, .style = {.fill = {0.0F, 0.0F, 0.0F, 0.6F}}});
			P::setMask({});

			const Foundation::Color ink(0.85F, 0.88F, 0.92F, 1.0F);
			P::drawText(
				{.text = "mask: opaque outside both, dark inside the disk, clear inside the star",
				 .position = {24.0F, 20.0F},
				 .scale = 1.5F,
				 .color = ink}
			);
			P::drawText(
				{.text = "disk: uploaded mesh, drawInstanced, writes 1    star: drawTriangles fan, writes 2",
				 .position = {24.0F, height - 44.0F},
				 .scale = 1.25F,
				 .color = ink}
			);
		}

	  private:
		// Coloured tiles, diagonal bars and rings: anything the cover hides or lets through shows.
		static void drawBackground(float width, float height) {
			const int columns = static_cast<int>(std::ceil(width / kTile));
			const int rows = static_cast<int>(std::ceil(height / kTile));
			for (int row = 0; row < rows; ++row) {
				for (int column = 0; column < columns; ++column) {
					const float u = static_cast<float>(column) / static_cast<float>(columns);
					const float v = static_cast<float>(row) / static_cast<float>(rows);
					const float shade = ((row + column) % 2 == 0) ? 1.0F : 0.75F;
					P::drawRect(
						{.bounds = {static_cast<float>(column) * kTile, static_cast<float>(row) * kTile, kTile, kTile},
						 .style = {.fill = {shade * (0.3F + (0.7F * u)), shade * (0.8F - (0.5F * v)), shade * (0.3F + (0.6F * v)), 1.0F}}}
					);
				}
			}
			for (float x = -height; x < width; x += 90.0F) {
				const std::array<Foundation::Vec2, 2> bar = {Foundation::Vec2(x, height), Foundation::Vec2(x + height, 0.0F)};
				P::drawPolyline({.points = bar, .width = 10.0F, .color = {1.0F, 0.95F, 0.6F, 0.8F}});
			}
			for (int i = 0; i < 9; ++i) {
				P::drawCircle(
					{.center = {width * (0.1F + (0.1F * static_cast<float>(i))), height * (i % 2 == 0 ? 0.3F : 0.7F)},
					 .radius = 40.0F,
					 .style = {
						 .fill = {0.0F, 0.0F, 0.0F, 0.0F},
						 .border = Foundation::BorderStyle{.color = {1.0F, 1.0F, 1.0F, 0.9F}, .width = 8.0F}
					 }}
				);
			}
		}

		// A star of kStarPoints points as a fan round its centre, aliased, as a mask's shape is drawn.
		static void drawStar(Foundation::Vec2 centre, float outer, float turn) {
			std::array<Foundation::Vec2, 1 + (2 * kStarPoints)>					points{};
			std::array<uint16_t, static_cast<std::size_t>(3 * 2 * kStarPoints)> indices{};
			points[0] = centre;
			for (int i = 0; i < 2 * kStarPoints; ++i) {
				const float angle = turn + (kPi * static_cast<float>(i) / kStarPoints);
				const float reach = i % 2 == 0 ? outer : outer * 0.5F;
				points[static_cast<std::size_t>(1 + i)] = centre + Foundation::Vec2(reach * std::cos(angle), reach * std::sin(angle));
				const auto at = static_cast<std::size_t>(3 * i);
				indices[at] = 0;
				indices[at + 1] = static_cast<uint16_t>(1 + i);
				indices[at + 2] = static_cast<uint16_t>(1 + ((i + 1) % (2 * kStarPoints)));
			}
			P::drawTriangles(
				{.vertices = points.data(),
				 .indices = indices.data(),
				 .vertexCount = points.size(),
				 .indexCount = indices.size(),
				 .color = Foundation::Color::white()}
			);
		}

		Renderer::InstancedMeshHandle disk;
		double						  seconds = 0.0;
	};

} // namespace

namespace sandbox {
	const SceneInfo kMaskScene{.name = kSceneName, .make = makeScene<MaskScene>};
} // namespace sandbox
