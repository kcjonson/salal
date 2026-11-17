// mask: CSS masks on drawRect (docs/masks.md), drawn as worldsim's player visibility cover with soft
// edges (its sight-softening design, 7.5 and 7.7). A busy background, then two fills over it:
//
// - The tint, black at half alpha, masked by one content layer: the view at 1, then a group a
//   colonist, isolated and combined with what is under it by darken, holding his bright shape at 0
//   and over it his feather and drawn shadows by lighten, each clipped to his bright shape.
// - The unseen fill, opaque, masked by the view less the record's meshes and the pending shapes, and
//   over them a content layer of the seen area's feather: the record's pieces in two cells, two
//   meshes sharing an edge, cut against the pending shapes, and the pending shapes' own pieces.
//
// The cases of 7.7, each where its label says: two record cells sharing an edge (the dashed line), a
// pending shape abutting the record and a pending leg overlapping it, two bright shapes overlapping
// and two abutting, a beam narrower than twice the feather, a reflex corner, a soft body's triangle
// and fins, an opaque rock's bands and fins seen from outside and a room's doorway from inside, a
// piece reaching past its colonist's bright shape, and 50 colonists drifting with their shadows.
// Their values are checked by CoverageMask.test.cpp, which draws the same rules and reads them back.

#include "Scenes.h"

#include <graphics/Color.h>
#include <primitives/MaskImage.h>
#include <primitives/Primitives.h>
#include <random/Rng.h>
#include <vector/Types.h>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <span>
#include <vector>

namespace {

	namespace Primitives = Renderer::Primitives;
	using Foundation::Vec2;
	using Primitives::MaskBlend;
	using Primitives::MaskComposite;
	using Primitives::MaskImage;
	using Primitives::MaskIsolation;
	using Primitives::MaskLayer;
	using Primitives::MaskMode;
	using Primitives::MaskPaint;
	using Primitives::MaskShape;

	constexpr const char* kSceneName = "mask";
	constexpr float		  kPi = std::numbers::pi_v<float>;
	constexpr float		  kDesignWidth = 1600.0F; // the layout's own units, scaled to the window
	constexpr float		  kDesignHeight = 900.0F;
	constexpr float		  kFeather = 24.0F; // sightFeatherMm, in layout units
	constexpr float		  kTile = 48.0F;
	constexpr float		  kSeenDimAlpha = 0.5F;
	constexpr int32_t	  kCoverKey = 1000;
	constexpr int		  kCrowd = 50;

	const Foundation::Color kUnseenColor(0.13F, 0.15F, 0.19F, 1.0F);

	float cross(Vec2 first, Vec2 second) {
		return (first.x * second.y) - (first.y * second.x);
	}

	// A triangle with a value at each corner, linear between: a piece of darkness or of the fade.
	struct Piece {
		std::array<Vec2, 3>	 corners;
		std::array<float, 3> values;
	};

	// The two-stop gradient that is a piece's linear field, from its least corner to its greatest.
	MaskPaint pieceGradient(const Piece& piece) {
		const Vec2	first = piece.corners[1] - piece.corners[0];
		const Vec2	second = piece.corners[2] - piece.corners[0];
		const float rise1 = piece.values[1] - piece.values[0];
		const float rise2 = piece.values[2] - piece.values[0];
		const Vec2	slope = Vec2((rise1 * second.y) - (rise2 * first.y), (rise2 * first.x) - (rise1 * second.x)) / cross(first, second);
		const float slopeSquared = glm::dot(slope, slope);
		if (slopeSquared < 1e-12F) {
			return MaskPaint::flat(piece.values[0]);
		}
		const auto	least = static_cast<size_t>(std::min_element(piece.values.begin(), piece.values.end()) - piece.values.begin());
		const float lowest = piece.values[least];
		const float highest = *std::max_element(piece.values.begin(), piece.values.end());
		const Vec2	from = piece.corners[least];
		return MaskPaint::linear(from, from + (slope * ((highest - lowest) / slopeSquared)), lowest, highest);
	}

	// The feather's rectangle inward of an edge, 1 on it and 0 a feather in.
	void edgePieces(Vec2 start, Vec2 end, Vec2 inward, std::vector<Piece>& pieces) {
		const Vec2 farStart = start + (inward * kFeather);
		const Vec2 farEnd = end + (inward * kFeather);
		pieces.push_back({{start, end, farEnd}, {1.0F, 1.0F, 0.0F}});
		pieces.push_back({{start, farEnd, farStart}, {1.0F, 0.0F, 0.0F}});
	}

	// A reflex corner's fan, 1 at the corner and 0 on the arc, in triangles of at most 15 degrees.
	void fanPieces(Vec2 corner, float fromAngle, float toAngle, std::vector<Piece>& pieces) {
		constexpr float kStep = 15.0F * kPi / 180.0F;
		const int		steps = std::max(1, static_cast<int>(std::ceil(std::abs(toAngle - fromAngle) / kStep)));
		for (int step = 0; step < steps; ++step) {
			const float first = fromAngle + ((toAngle - fromAngle) * static_cast<float>(step) / static_cast<float>(steps));
			const float second = fromAngle + ((toAngle - fromAngle) * static_cast<float>(step + 1) / static_cast<float>(steps));
			pieces.push_back(
				{{corner,
				  corner + (Vec2(std::cos(first), std::sin(first)) * kFeather),
				  corner + (Vec2(std::cos(second), std::sin(second)) * kFeather)},
				 {1.0F, 0.0F, 0.0F}}
			);
		}
	}

	Vec2 inwardNormal(std::span<const Vec2> outline, Vec2 start, Vec2 end) {
		float area = 0.0F;
		for (size_t index = 0; index < outline.size(); ++index) {
			area += cross(outline[index], outline[(index + 1) % outline.size()]);
		}
		const Vec2 along = glm::normalize(end - start);
		return area > 0.0F ? Vec2(-along.y, along.x) : Vec2(along.y, -along.x);
	}

	// A soft body's triangle and its two fins (3.2), `strength` at its foot.
	void softShadow(Vec2 foot, Vec2 away, float width, float length, float strength, std::vector<Piece>& pieces) {
		const Vec2 across(-away.y, away.x);
		const Vec2 left = foot + (across * (width * 0.5F));
		const Vec2 right = foot - (across * (width * 0.5F));
		const Vec2 tip = foot + (away * length);
		pieces.push_back({{left, right, tip}, {strength, strength, 0.0F}});
		pieces.push_back({{left, tip, left + (across * width) + (away * length * 0.6F)}, {strength, 0.0F, 0.0F}});
		pieces.push_back({{right, right - (across * width) + (away * length * 0.6F), tip}, {strength, 0.0F, 0.0F}});
	}

	// An opaque body's shadow edge: a band at full darkness on its lit side, then a fin falling to 0 (3.1).
	void hardShadowEdge(Vec2 corner, Vec2 ray, Vec2 litSide, float reach, std::vector<Piece>& pieces) {
		const Vec2 rim = corner + (ray * reach);
		const Vec2 bandEdge = rim + (litSide * reach * 0.07F);
		const Vec2 finEdge = rim + (litSide * reach * 0.21F);
		pieces.push_back({{corner, rim, bandEdge}, {1.0F, 1.0F, 1.0F}});
		pieces.push_back({{corner, bandEdge, finEdge}, {1.0F, 1.0F, 0.0F}});
	}

	// A colonist's bright shape and the pieces of his darkness inside it (7.1).
	struct Colonist {
		std::vector<Vec2>  bright;
		std::vector<Piece> pieces;

		template <typename Rim>
		void featherRim(Rim rim) {
			for (size_t index = 0; index < bright.size(); ++index) {
				const Vec2 start = bright[index];
				const Vec2 end = bright[(index + 1) % bright.size()];
				if (rim(index)) {
					edgePieces(start, end, inwardNormal(bright, start, end), pieces);
				}
			}
		}
	};

	std::vector<Vec2> circle(Vec2 centre, float radius, int sides) {
		std::vector<Vec2> outline;
		for (int side = 0; side < sides; ++side) {
			const float angle = 2.0F * kPi * static_cast<float>(side) / static_cast<float>(sides);
			outline.push_back(centre + (Vec2(std::cos(angle), std::sin(angle)) * radius));
		}
		return outline;
	}

	struct Box {
		float left, top, right, bottom;

		std::array<Vec2, 4> outline() const { return {{{left, top}, {right, top}, {right, bottom}, {left, bottom}}}; }

		// The box grown by the feather, its corners round in the fan's steps.
		std::vector<Vec2> grown() const {
			std::vector<Vec2>							outline;
			const std::array<std::pair<Vec2, float>, 4> corners = {{
				{{right, top}, -0.5F},
				{{right, bottom}, 0.0F},
				{{left, bottom}, 0.5F},
				{{left, top}, 1.0F},
			}};
			for (const auto& [corner, start] : corners) {
				for (int step = 0; step <= 6; ++step) {
					const float angle = kPi * (start + (0.5F * static_cast<float>(step) / 6.0F));
					outline.push_back(corner + (Vec2(std::cos(angle), std::sin(angle)) * kFeather));
				}
			}
			return outline;
		}
	};

	// The record: two cells sharing the edge x = 800, the second shorter, so a reflex corner stands
	// where they meet; a pending shape abutting the second's bottom edge, and a pending leg
	// overlapping the first's.
	constexpr Box kFirstCell{40.0F, 60.0F, 800.0F, 820.0F};
	constexpr Box kSecondCell{800.0F, 60.0F, 1560.0F, 600.0F};
	constexpr Box kAbutting{1100.0F, 600.0F, 1400.0F, 760.0F};
	constexpr Box kOverlapping{300.0F, 780.0F, 480.0F, 940.0F};

	// The record's feather by cell, for the record's own edge; and inside the pending shapes'
	// feather, the record's pieces cut against them and their own.
	void seenFeather(std::vector<Piece>& firstCell, std::vector<Piece>& secondCell, std::vector<Piece>& pending) {
		edgePieces({40.0F, 60.0F}, {800.0F, 60.0F}, {0.0F, 1.0F}, firstCell);
		edgePieces({40.0F, 820.0F}, {40.0F, 60.0F}, {1.0F, 0.0F}, firstCell);
		edgePieces({800.0F, 820.0F}, {40.0F, 820.0F}, {0.0F, -1.0F}, firstCell);
		edgePieces({800.0F, 600.0F}, {800.0F, 820.0F}, {-1.0F, 0.0F}, firstCell);
		fanPieces({800.0F, 600.0F}, kPi, 1.5F * kPi, firstCell);
		edgePieces({800.0F, 60.0F}, {1560.0F, 60.0F}, {0.0F, 1.0F}, secondCell);
		edgePieces({1560.0F, 60.0F}, {1560.0F, 600.0F}, {-1.0F, 0.0F}, secondCell);
		edgePieces({1560.0F, 600.0F}, {800.0F, 600.0F}, {0.0F, -1.0F}, secondCell);

		edgePieces({1100.0F, 600.0F}, {1076.0F, 600.0F}, {0.0F, -1.0F}, pending);
		edgePieces({1424.0F, 600.0F}, {1400.0F, 600.0F}, {0.0F, -1.0F}, pending);
		edgePieces({1100.0F, 760.0F}, {1100.0F, 600.0F}, {1.0F, 0.0F}, pending);
		edgePieces({1400.0F, 600.0F}, {1400.0F, 760.0F}, {-1.0F, 0.0F}, pending);
		edgePieces({1400.0F, 760.0F}, {1100.0F, 760.0F}, {0.0F, -1.0F}, pending);
		fanPieces({1100.0F, 600.0F}, 1.5F * kPi, 2.0F * kPi, pending);
		fanPieces({1400.0F, 600.0F}, kPi, 1.5F * kPi, pending);
		edgePieces({300.0F, 820.0F}, {276.0F, 820.0F}, {0.0F, -1.0F}, pending);
		edgePieces({504.0F, 820.0F}, {480.0F, 820.0F}, {0.0F, -1.0F}, pending);
		edgePieces({300.0F, 940.0F}, {300.0F, 820.0F}, {1.0F, 0.0F}, pending);
		edgePieces({480.0F, 820.0F}, {480.0F, 940.0F}, {-1.0F, 0.0F}, pending);
		fanPieces({300.0F, 820.0F}, 1.5F * kPi, 2.0F * kPi, pending);
		fanPieces({480.0F, 820.0F}, kPi, 1.5F * kPi, pending);
	}

	renderer::TessellatedMesh pieceMesh(std::span<const Piece> pieces) {
		renderer::TessellatedMesh mesh;
		for (const Piece& piece : pieces) {
			for (size_t corner = 0; corner < 3; ++corner) {
				mesh.indices.push_back(static_cast<uint16_t>(mesh.vertices.size()));
				mesh.vertices.push_back(piece.corners[corner]);
				mesh.colors.emplace_back(1.0F, 1.0F, 1.0F, piece.values[corner]);
			}
		}
		return mesh;
	}

	renderer::TessellatedMesh boxMesh(const Box& box) {
		renderer::TessellatedMesh mesh;
		const std::array<Vec2, 4> corners = box.outline();
		mesh.vertices.assign(corners.begin(), corners.end());
		mesh.indices = {0, 1, 2, 0, 2, 3};
		mesh.colors.assign(4, Foundation::Color::white());
		return mesh;
	}

	// The colonists that stand still: each case of the tint but the crowd.
	std::vector<Colonist> fixedColonists() {
		std::vector<Colonist> colonists;
		// In the open with an opaque rock north of him seen from outside, and a trunk east of him
		// whose shadow reaches into his neighbour's sight, and a piece reaching past his shape.
		{
			const Vec2		  eye(250.0F, 250.0F);
			const float		  radius = 150.0F;
			const Vec2		  nearLeft(235.0F, 170.0F);
			const Vec2		  nearRight(265.0F, 170.0F);
			const Vec2		  leftRay = glm::normalize(nearLeft - eye);
			const Vec2		  rightRay = glm::normalize(nearRight - eye);
			const float		  leftAngle = std::atan2(leftRay.y, leftRay.x);
			const float		  rightAngle = std::atan2(rightRay.y, rightRay.x);
			Colonist		  colonist;
			std::vector<bool> rim;
			for (int side = 0; side < 48; ++side) {
				const float angle = rightAngle + ((2.0F * kPi - (rightAngle - leftAngle)) * static_cast<float>(side) / 47.0F);
				colonist.bright.push_back(eye + (Vec2(std::cos(angle), std::sin(angle)) * radius));
				rim.push_back(side < 47);
			}
			colonist.bright.push_back(nearLeft);
			rim.push_back(false);
			colonist.bright.push_back(nearRight);
			rim.push_back(false);
			colonist.featherRim([&](size_t index) { return rim[index]; });
			const float reach = radius - glm::length(nearLeft - eye) + 4.0F;
			hardShadowEdge(nearLeft, leftRay, Vec2(leftRay.y, -leftRay.x), reach, colonist.pieces);
			hardShadowEdge(nearRight, rightRay, Vec2(-rightRay.y, rightRay.x), reach, colonist.pieces);
			softShadow(eye + Vec2(70.0F, 20.0F), glm::normalize(Vec2(70.0F, 20.0F)), 16.0F, 110.0F, 0.5F, colonist.pieces);
			colonist.pieces.push_back(
				{{eye + Vec2(-90.0F, 70.0F), eye + Vec2(-220.0F, 140.0F), eye + Vec2(-90.0F, 140.0F)}, {0.25F, 0.25F, 0.25F}}
			);
			colonists.push_back(std::move(colonist));
		}
		{
			Colonist colonist{.bright = circle({430.0F, 250.0F}, 130.0F, 48)};
			colonist.featherRim([](size_t) { return true; });
			colonists.push_back(std::move(colonist));
		}
		// A beam 20 wide against a feather of 24.
		{
			Colonist colonist{.bright = {{600.0F, 110.0F}, {1100.0F, 150.0F}, {1098.4F, 170.0F}, {598.4F, 130.0F}}};
			colonist.featherRim([](size_t) { return true; });
			colonists.push_back(std::move(colonist));
		}
		// Inside a room, its doorway's jambs casting shadow edges down the beam out of it.
		{
			const Vec2 eye(980.0F, 370.0F);
			const Vec2 upperJamb(1080.0F, 360.0F);
			const Vec2 lowerJamb(1080.0F, 380.0F);
			const Vec2 upperRay = glm::normalize(upperJamb - eye);
			const Vec2 lowerRay = glm::normalize(lowerJamb - eye);
			const Vec2 upperEnd = upperJamb + (upperRay * (220.0F / upperRay.x));
			const Vec2 lowerEnd = lowerJamb + (lowerRay * (220.0F / lowerRay.x));
			Colonist   colonist{
				  .bright = {
					  {880.0F, 280.0F}, {1080.0F, 280.0F}, upperJamb, upperEnd, lowerEnd, lowerJamb, {1080.0F, 460.0F}, {880.0F, 460.0F}
				  }
			  };
			colonist.featherRim([](size_t index) { return index == 3; });
			hardShadowEdge(upperJamb, upperRay, Vec2(-upperRay.y, upperRay.x), 224.0F, colonist.pieces);
			hardShadowEdge(lowerJamb, lowerRay, Vec2(lowerRay.y, -lowerRay.x), 224.0F, colonist.pieces);
			colonists.push_back(std::move(colonist));
		}
		// Two bright shapes abutting.
		for (const float left : {1150.0F, 1300.0F}) {
			Colonist colonist{.bright = {{left, 110.0F}, {left + 150.0F, 110.0F}, {left + 150.0F, 240.0F}, {left, 240.0F}}};
			colonist.featherRim([](size_t) { return true; });
			colonists.push_back(std::move(colonist));
		}
		// An L, its reflex corner fanned.
		{
			Colonist colonist{
				.bright = {{600.0F, 230.0F}, {820.0F, 230.0F}, {820.0F, 300.0F}, {680.0F, 300.0F}, {680.0F, 420.0F}, {600.0F, 420.0F}}
			};
			colonist.featherRim([](size_t) { return true; });
			fanPieces({680.0F, 300.0F}, 1.5F * kPi, kPi, colonist.pieces);
			colonists.push_back(std::move(colonist));
		}
		return colonists;
	}

	class MaskScene final : public app::Scene {
	  public:
		const char* name() const override { return kSceneName; }

		void onEnter(app::App& /*app*/) override {
			std::vector<Piece> firstCell;
			std::vector<Piece> secondCell;
			pendingPieces.clear();
			seenFeather(firstCell, secondCell, pendingPieces);
			firstFeather = Primitives::uploadInstancedMesh(pieceMesh(firstCell), 1);
			secondFeather = Primitives::uploadInstancedMesh(pieceMesh(secondCell), 1);
			firstSeen = Primitives::uploadInstancedMesh(boxMesh(kFirstCell), 1);
			secondSeen = Primitives::uploadInstancedMesh(boxMesh(kSecondCell), 1);
			abuttingGrown = kAbutting.grown();
			overlappingGrown = kOverlapping.grown();
			still = fixedColonists();
			foundation::Rng random(2026);
			crowd.clear();
			for (int index = 0; index < kCrowd; ++index) {
				crowd.push_back(
					{.home = {80.0F + (random.nextFloat() * 660.0F), 470.0F + (random.nextFloat() * 300.0F)},
					 .radius = 40.0F + (random.nextFloat() * 30.0F),
					 .phase = random.nextFloat() * 2.0F * kPi,
					 .shadowAngle = random.nextFloat() * 2.0F * kPi,
					 .strength = 0.3F + (random.nextFloat() * 0.4F)}
				);
			}
		}

		void onExit() override {
			firstFeather = {};
			secondFeather = {};
			firstSeen = {};
			secondSeen = {};
		}

		void update(const app::FrameContext& frame) override { seconds += frame.dt; }

		void render(float /*alpha*/) override {
			int windowWidth = 0;
			int windowHeight = 0;
			Primitives::getLogicalViewport(windowWidth, windowHeight);
			const float width = static_cast<float>(windowWidth);
			const float height = static_cast<float>(windowHeight);
			const auto	time = static_cast<float>(seconds);

			drawBackground(width, height);

			// The layout's units to the window, top-left anchored; the meshes' world is those units.
			const float					  scale = std::min(width / kDesignWidth, height / kDesignHeight);
			const Primitives::WorldCamera camera{.position = {width * 0.5F / scale, height * 0.5F / scale}, .pixelsPerMeter = scale};
			Primitives::PushTransform(glm::scale(Foundation::Mat4(1.0F), {scale, scale, 1.0F}));
			const Foundation::Rect	  view{-4.0F, -4.0F, (width / scale) + 8.0F, (height / scale) + 8.0F};
			const std::array<Vec2, 4> viewPoints = {
				{{view.x, view.y},
				 {view.x + view.width, view.y},
				 {view.x + view.width, view.y + view.height},
				 {view.x, view.y + view.height}}
			};

			// The crowd, drifting, each with a trunk's shadow turning slowly.
			std::vector<Colonist> colonists = still;
			for (const Wanderer& wanderer : crowd) {
				const Vec2 centre =
					wanderer.home + (Vec2(std::sin((time * 0.4F) + wanderer.phase), std::cos((time * 0.3F) + wanderer.phase)) * 20.0F);
				Colonist colonist{.bright = circle(centre, wanderer.radius, 24)};
				colonist.featherRim([](size_t) { return true; });
				const float angle = wanderer.shadowAngle + (time * 0.2F);
				const Vec2	away(std::cos(angle), std::sin(angle));
				softShadow(
					centre + (away * wanderer.radius * 0.3F), away, 10.0F, wanderer.radius * 0.9F, wanderer.strength, colonist.pieces
				);
				colonists.push_back(std::move(colonist));
			}

			// The tint: the view at 1, then a group a colonist.
			size_t pieceCount = 0;
			for (const Colonist& colonist : colonists) {
				pieceCount += colonist.pieces.size();
			}
			std::vector<std::array<MaskLayer, 1>> insideBrightShapes;
			std::vector<std::array<Vec2, 3>>	  outlines;
			insideBrightShapes.reserve(colonists.size());
			outlines.reserve(pieceCount + pendingPieces.size());
			std::vector<MaskShape> tint = {{.points = viewPoints, .paint = MaskPaint::flat(1.0F)}};
			for (const Colonist& colonist : colonists) {
				insideBrightShapes.push_back({{{.image = MaskImage::polygon(colonist.bright)}}});
				MaskShape group{.blend = MaskBlend::Darken, .isolation = MaskIsolation::Isolate};
				group.children.push_back({.points = colonist.bright, .paint = MaskPaint::flat(0.0F)});
				for (const Piece& piece : colonist.pieces) {
					outlines.push_back(piece.corners);
					group.children.push_back(
						{.points = outlines.back(),
						 .paint = pieceGradient(piece),
						 .mask = insideBrightShapes.back(),
						 .blend = MaskBlend::Lighten}
					);
				}
				tint.push_back(std::move(group));
			}
			const std::array<MaskLayer, 1> tintMask = {{{.image = MaskImage::content(tint), .mode = MaskMode::Luminance}}};
			Primitives::drawRect(
				{.bounds = view, .style = {.fill = {0.0F, 0.0F, 0.0F, kSeenDimAlpha}}, .mask = tintMask, .key = kCoverKey}
			);

			// The unseen fill: the view less the seen area, and over it the seen area's feather.
			const std::array<MaskLayer, 3> outsidePendingFeather = {{
				{.image = MaskImage::rect(view), .composite = MaskComposite::Subtract},
				{.image = MaskImage::polygon(abuttingGrown)},
				{.image = MaskImage::polygon(overlappingGrown)},
			}};
			std::vector<MaskShape>		   unseenFeather = {
				{.mesh = {firstFeather, camera}, .mask = outsidePendingFeather, .blend = MaskBlend::Lighten},
				{.mesh = {secondFeather, camera}, .mask = outsidePendingFeather, .blend = MaskBlend::Lighten},
			};
			for (const Piece& piece : pendingPieces) {
				outlines.push_back(piece.corners);
				unseenFeather.push_back({.points = outlines.back(), .paint = pieceGradient(piece), .blend = MaskBlend::Lighten});
			}
			const std::array<Vec2, 4>	   abuttingOutline = kAbutting.outline();
			const std::array<Vec2, 4>	   overlappingOutline = kOverlapping.outline();
			const std::array<MaskLayer, 6> unseenMask = {{
				{.image = MaskImage::content(unseenFeather), .mode = MaskMode::Luminance},
				{.image = MaskImage::rect(view), .composite = MaskComposite::Subtract},
				{.image = MaskImage::mesh(firstSeen, camera)},
				{.image = MaskImage::mesh(secondSeen, camera)},
				{.image = MaskImage::polygon(abuttingOutline)},
				{.image = MaskImage::polygon(overlappingOutline)},
			}};
			Primitives::drawRect({.bounds = view, .style = {.fill = kUnseenColor}, .mask = unseenMask, .key = kCoverKey + 1});

			drawLabels();
			Primitives::PopTransform();
		}

	  private:
		struct Wanderer {
			Vec2  home;
			float radius = 0.0F;
			float phase = 0.0F;
			float shadowAngle = 0.0F;
			float strength = 0.0F;
		};

		static void label(const char* text, Vec2 position) {
			Primitives::drawText(
				{.text = text, .position = position, .scale = 1.0F, .color = {0.95F, 0.96F, 0.98F, 1.0F}, .key = kCoverKey + 2}
			);
		}

		static void drawLabels() {
			label("mask: the visibility cover with soft edges (partial coverage)", {48.0F, 18.0F});
			label("rock seen from outside, trunk, piece past his shape", {110.0F, 420.0F});
			label("overlapping sights: the lens", {360.0F, 400.0F});
			label("beam under twice the feather", {700.0F, 180.0F});
			label("room from inside, doorway beam", {890.0F, 470.0F});
			label("abutting sights", {1250.0F, 250.0F});
			label("reflex corner", {700.0F, 400.0F});
			label("50 colonists", {80.0F, 450.0F});
			label("record cells share x = 800", {810.0F, 620.0F});
			label("pending abutting", {1160.0F, 680.0F});
			label("pending leg overlapping", {310.0F, 860.0F});
			const std::array<Vec2, 2> sharedEdge = {Vec2(800.0F, 60.0F), Vec2(800.0F, 600.0F)};
			Primitives::drawPolyline(
				{.points = sharedEdge,
				 .width = 2.0F,
				 .color = {1.0F, 1.0F, 1.0F, 0.6F},
				 .dash = Foundation::DashPattern{.dash = 10.0F, .gap = 10.0F},
				 .key = kCoverKey + 2}
			);
		}

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
				const std::array<Vec2, 2> bar = {Vec2(x, height), Vec2(x + height, 0.0F)};
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

		Renderer::InstancedMeshHandle firstFeather;
		Renderer::InstancedMeshHandle secondFeather;
		Renderer::InstancedMeshHandle firstSeen;
		Renderer::InstancedMeshHandle secondSeen;
		std::vector<Piece>			  pendingPieces;
		std::vector<Vec2>			  abuttingGrown;
		std::vector<Vec2>			  overlappingGrown;
		std::vector<Colonist>		  still;
		std::vector<Wanderer>		  crowd;
		double						  seconds = 0.0;
	};

} // namespace

namespace sandbox {
	const SceneInfo kMaskScene{.name = kSceneName, .make = makeScene<MaskScene>};
} // namespace sandbox
