// perf_quads: 10,000 overlapping rects, the batcher's per-quad cost. Most have a corner radius, half a
// border (inside, centred or outside), some a gradient or a shadow. Fixed content from a fixed seed,
// scrolled by a translate that is a function of the tick, so every frame of a tick is the same.

#include "Scenes.h"

#include <graphics/Color.h>
#include <primitives/Primitives.h>
#include <random/Rng.h>

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace {

	constexpr const char* kSceneName = "perf_quads";
	constexpr uint32_t	  kSeed = 0x5A1A1001;
	constexpr int		  kRects = 10000;
	constexpr float		  kWidth = 1600.0F;
	constexpr float		  kHeight = 900.0F;

	const Foundation::Color kBackground(0.07F, 0.07F, 0.09F, 1.0F);

	Foundation::Color colour(foundation::Rng& rng, float alpha) {
		return {(0.2F + (0.8F * rng.nextFloat())), (0.2F + (0.8F * rng.nextFloat())), (0.2F + (0.8F * rng.nextFloat())), alpha};
	}

	class PerfQuadsScene final : public app::Scene {
	  public:
		PerfQuadsScene() {
			foundation::Rng rng(kSeed);
			rects.reserve(kRects);
			for (int i = 0; i < kRects; ++i) {
				const float w = 12.0F + (48.0F * rng.nextFloat());
				const float h = 12.0F + (48.0F * rng.nextFloat());
				const float radius = rng.nextFloat() < 0.6F ? 2.0F + (10.0F * rng.nextFloat()) : 0.0F;

				Renderer::Primitives::RectArgs rect{
					.bounds = {(kWidth - w) * rng.nextFloat(), (kHeight - h) * rng.nextFloat(), w, h},
					.style = {.fill = colour(rng, 0.6F + (0.4F * rng.nextFloat()))},
				};
				if (rng.nextFloat() < 0.5F) {
					rect.style.border = Foundation::BorderStyle{
						.color = colour(rng, 1.0F),
						.width = 1.0F + (2.0F * rng.nextFloat()),
						.cornerRadius = radius,
						.position = static_cast<Foundation::BorderPosition>(rng.nextInt(0, 2)),
					};
				} else if (radius > 0.0F) {
					rect.style.border = Foundation::BorderStyle{.color = {0.0F, 0.0F, 0.0F, 0.0F}, .width = 0.0F, .cornerRadius = radius};
				}
				if (rng.nextFloat() < 0.1F) {
					rect.style.gradient = Foundation::LinearGradient{.from = colour(rng, 1.0F), .to = colour(rng, 1.0F)};
				}
				if (rng.nextFloat() < 0.05F) {
					rect.style.boxShadow = Foundation::BoxShadow{.blur = 4.0F + (6.0F * rng.nextFloat()), .offset = {0.0F, 2.0F}};
				}
				rects.push_back(rect);
			}
		}

		const char* name() const override { return kSceneName; }

		void fixedUpdate(const app::TickContext& context) override { tick = context.tick; }

		void render(float /*alpha*/) override {
			namespace P = Renderer::Primitives;
			P::clear(kBackground);
			const auto t = static_cast<float>(tick);
			P::PushTransform(glm::translate(Foundation::Mat4(1.0F), {40.0F * std::sin(t * 0.02F), 30.0F * std::cos(t * 0.015F), 0.0F}));
			for (const P::RectArgs& rect : rects) {
				P::drawRect(rect);
			}
			P::PopTransform();
		}

		std::string exportState() const override { return R"({"rects":)" + std::to_string(rects.size()) + "}"; }

	  private:
		std::vector<Renderer::Primitives::RectArgs> rects;
		uint64_t									tick = 0;
	};

} // namespace

namespace sandbox {
	const SceneInfo kPerfQuadsScene{.name = kSceneName, .make = makeScene<PerfQuadsScene>};
} // namespace sandbox
