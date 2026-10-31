// perf_text: list rows in six columns, one text size each, 11 to 24 px. A row is a background, an
// image icon, a label in one font atlas and a value and unit in a second (the sandbox's font loaded
// again, kSecondFont), so each row samples three textures, which today splits the batch three times
// a row. The icons cycle through twelve textures, so a frame samples fourteen, more than eight
// resident slots hold. Fixed content from a fixed seed, bobbing by a translate that is a function of
// the tick.

#include "Scenes.h"

#include <Texture.h>
#include <graphics/Color.h>
#include <primitives/Primitives.h>
#include <random/Rng.h>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace {

	constexpr const char*		   kSceneName = "perf_text";
	constexpr uint32_t			   kSeed = 0x5A1A1002;
	constexpr int				   kIconSize = 64;
	constexpr std::size_t		   kIcons = 12;
	constexpr float				   kTop = 56.0F;
	constexpr float				   kBottom = 900.0F;
	constexpr std::array<float, 6> kScales{0.6875F, 0.75F, 0.875F, 1.0F, 1.25F, 1.5F};
	constexpr std::array<const char*, 12>
		kWords{"iron", "copper", "salvage", "fuel", "hull", "shield", "cargo", "engine", "sensor", "crew", "ammo", "reactor"};
	constexpr std::array<const char*, 4> kUnits{"t", "kg", "%", "u/s"};

	const Foundation::Color kBackground(0.08F, 0.08F, 0.1F, 1.0F);
	const Foundation::Color kRowEven(0.12F, 0.12F, 0.15F, 1.0F);
	const Foundation::Color kRowOdd(0.15F, 0.15F, 0.19F, 1.0F);
	const Foundation::Color kLabel(0.85F, 0.85F, 0.9F, 1.0F);
	const Foundation::Color kValue(0.55F, 0.9F, 0.65F, 1.0F);
	const Foundation::Color kUnit(0.6F, 0.6F, 0.7F, 1.0F);

	struct Row {
		Foundation::Rect  background;
		Foundation::Rect  icon;
		Foundation::Color tint;
		std::string		  label, value, unit;
		Foundation::Vec2  labelAt, valueAt, unitAt;
		float			  scale = 1.0F;
	};

	class PerfTextScene final : public app::Scene {
	  public:
		PerfTextScene() {
			foundation::Rng rng(kSeed);
			float			x = 10.0F;
			for (const float scale : kScales) {
				const float width = 300.0F * scale;
				const float height = 24.0F * scale;
				const float em = 16.0F * scale;
				headers.push_back({.text = std::string("size ") + std::to_string(static_cast<int>(em)) + " px", .position = {x, 12.0F}});
				for (float y = kTop; y + height <= kBottom; y += height) {
					// One draw a statement: the order of a call's arguments, or an operator's operands, is the compiler's.
					std::string label = rng.pick(kWords);
					label += std::string(" ") + rng.pick(kWords);
					const int whole = rng.nextInt(0, 99999);
					const int hundredths = rng.nextInt(0, 99);
					char	  value[16];
					std::snprintf(value, sizeof(value), "%d.%02d", whole, hundredths);
					const float iconSide = em;
					rows.push_back({
						.background = {x, y, width - 4.0F, height - 2.0F},
						.icon = {x + 4.0F, y + ((height - iconSide) * 0.5F) - 1.0F, iconSide, iconSide},
						.tint = {0.5F + (0.5F * rng.nextFloat()), 0.5F + (0.5F * rng.nextFloat()), 0.5F + (0.5F * rng.nextFloat()), 1.0F},
						.label = std::move(label),
						.value = value,
						.unit = rng.pick(kUnits),
						.labelAt = {x + iconSide + 10.0F, y + (height * 0.2F)},
						.valueAt = {x + (width * 0.6F), y + (height * 0.2F)},
						.unitAt = {x + (width * 0.88F), y + (height * 0.35F)},
						.scale = scale,
					});
				}
				x += width + 6.0F;
			}
		}

		const char* name() const override { return kSceneName; }

		void onEnter(app::App& /*app*/) override {
			// A ring on a diamond, each icon's a little different, premultiplied.
			std::vector<std::uint8_t> pixels(static_cast<std::size_t>(kIconSize) * kIconSize * 4);
			for (std::size_t k = 0; k < kIcons; ++k) {
				const float ringRadius = 0.45F + (0.04F * static_cast<float>(k));
				const float diamondSize = 0.2F + (0.03F * static_cast<float>(k));
				for (int y = 0; y < kIconSize; ++y) {
					for (int x = 0; x < kIconSize; ++x) {
						const float		  dx = ((static_cast<float>(x) + 0.5F) / kIconSize * 2.0F) - 1.0F;
						const float		  dy = ((static_cast<float>(y) + 0.5F) / kIconSize * 2.0F) - 1.0F;
						const float		  ring = std::abs(std::sqrt((dx * dx) + (dy * dy)) - ringRadius) < 0.12F ? 1.0F : 0.0F;
						const float		  diamond = std::abs(dx) + std::abs(dy) < diamondSize ? 0.8F : 0.0F;
						const auto		  alpha = static_cast<std::uint8_t>(std::lround(255.0F * std::max(ring, diamond)));
						const std::size_t i = ((static_cast<std::size_t>(y) * kIconSize) + static_cast<std::size_t>(x)) * 4;
						pixels[i + 0] = alpha;
						pixels[i + 1] = alpha;
						pixels[i + 2] = alpha;
						pixels[i + 3] = alpha;
					}
				}
				icons[k] = Renderer::Primitives::createTexture({.width = kIconSize, .height = kIconSize, .pixels = pixels.data()});
			}
		}

		void onExit() override { icons = {}; }

		void fixedUpdate(const app::TickContext& context) override { tick = context.tick; }

		void render(float /*alpha*/) override {
			namespace P = Renderer::Primitives;
			P::clear(kBackground);
			P::PushTransform(
				glm::translate(Foundation::Mat4(1.0F), {0.0F, std::round(6.0F * std::sin(static_cast<float>(tick) * 0.03F)), 0.0F})
			);
			for (const Header& header : headers) {
				P::drawText(
					{.text = header.text, .position = header.position, .scale = 1.75F, .color = kLabel, .font = sandbox::kMonoFont}
				);
			}
			for (std::size_t i = 0; i < rows.size(); ++i) {
				const Row& row = rows[i];
				P::drawRect({.bounds = row.background, .style = {.fill = (i % 2 == 0) ? kRowEven : kRowOdd}});
				P::drawImage({.texture = icons[i % kIcons].handle(), .bounds = row.icon, .tint = row.tint});
				P::drawText({.text = row.label, .position = row.labelAt, .scale = row.scale, .color = kLabel, .font = sandbox::kMonoFont});
				P::drawText(
					{.text = row.value, .position = row.valueAt, .scale = row.scale, .color = kValue, .font = sandbox::kSecondFont}
				);
				P::drawText(
					{.text = row.unit, .position = row.unitAt, .scale = row.scale * 0.75F, .color = kUnit, .font = sandbox::kSecondFont}
				);
			}
			P::PopTransform();
		}

		std::string exportState() const override { return R"({"rows":)" + std::to_string(rows.size()) + "}"; }

	  private:
		struct Header {
			std::string		 text;
			Foundation::Vec2 position;
		};

		std::vector<Header>					  headers;
		std::vector<Row>					  rows;
		std::array<Renderer::Texture, kIcons> icons;
		uint64_t							  tick = 0;
	};

} // namespace

namespace sandbox {
	const SceneInfo kPerfTextScene{.name = kSceneName, .make = makeScene<PerfTextScene>};
} // namespace sandbox
