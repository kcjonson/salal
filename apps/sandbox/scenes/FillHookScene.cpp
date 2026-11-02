// fill_hook: a fill hook (docs/shader-hooks.md, "Fill hooks"), the renderer tests' stripes
// (libs/renderer/tests/fill_hooks/stripes.frag.glsl), on rects with each border position, rounded
// corners and a capsule, inside a clip, turned by PushTransform, translucent, beside a plain rect,
// and shifted by its uniform block between two rects.

#include "BaseShader.h"
#include "DesignCanvas.h"
#include "Scenes.h"

#include <graphics/Color.h>
#include <primitives/Primitives.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/packing.hpp>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <optional>
#include <span>
#include <string>

namespace {

	constexpr const char* kSceneName = "fill_hook";
	constexpr float		  kCanvasWidth = 2000.0F;
	constexpr float		  kCanvasHeight = 1240.0F;
	constexpr float		  kWidth = 360.0F;
	constexpr float		  kHeight = 220.0F;
	constexpr float		  kStripe = 24.0F;

	const Foundation::Color kBackground(0.08F, 0.08F, 0.11F, 1.0F);
	const Foundation::Color kLabel(0.75F, 0.75F, 0.8F, 1.0F);
	const Foundation::Color kFill(0.3F, 0.3F, 0.35F, 1.0F); // what the hook draws in place of
	const Foundation::Color kAmber(0.95F, 0.7F, 0.3F, 1.0F);
	const Foundation::Color kTeal(0.2F, 0.55F, 0.6F, 1.0F);
	const Foundation::Color kInk(0.95F, 0.95F, 0.9F, 1.0F);

	// stripes.frag.glsl's block, padded to its std140 size.
	struct StripesUniforms {
		float shift = 0.0F;
		float padding[3] = {}; // NOLINT(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
	};
	static_assert(sizeof(StripesUniforms) == base_shader::kStripes.size);
	static_assert(offsetof(StripesUniforms, shift) == base_shader::kStripesShift.offset);

	// A colour as the hook reads it, rgba8 and premultiplied.
	uint32_t packed(const Foundation::Color& color) {
		return glm::packUnorm4x8(Foundation::Vec4(color.r * color.a, color.g * color.a, color.b * color.a, color.a));
	}

	Renderer::Primitives::Fill stripes(const Foundation::Color& first, const Foundation::Color& second) {
		return {.hook = &base_shader::kStripesFill, .params = {packed(first), packed(second), std::bit_cast<uint32_t>(kStripe)}};
	}

	void shift(float by) {
		const StripesUniforms values{.shift = by};
		Renderer::Primitives::setHookUniforms(base_shader::kStripes, std::as_bytes(std::span(&values, 1)));
	}

	// A label under the rect whose top-left is (x, y).
	void label(const std::string& text, float x, float y) {
		Renderer::Primitives::drawText(
			{.text = text,
			 .position = {x, y + kHeight + 20.0F},
			 .scale = 1.5F,
			 .color = kLabel,
			 .hAlign = Foundation::HorizontalAlign::Center,
			 .boxWidth = kWidth}
		);
	}

	void stripedRect(float x, float y, const std::optional<Foundation::BorderStyle>& border = std::nullopt) {
		Renderer::Primitives::drawRect(
			{.bounds = {x, y, kWidth, kHeight}, .style = {.fill = kFill, .border = border}, .fill = stripes(kAmber, kTeal)}
		);
	}

	class FillHookScene final : public app::Scene {
	  public:
		const char* name() const override { return kSceneName; }

		void render(float /*alpha*/) override {
			namespace P = Renderer::Primitives;

			P::clear(kBackground);
			shift(0.0F); // the block keeps its values from frame to frame
			P::PushTransform(sandbox::designCanvas(kCanvasWidth, kCanvasHeight));
			P::drawText(
				{.text = "fill hook: stripes from each rect's params, under its corners, border and clip",
				 .position = {60.0F, 30.0F},
				 .scale = 2.0F,
				 .color = Foundation::Color::white()}
			);

			// Row 1: the border positions, rounded corners and a capsule.
			stripedRect(80.0F, 120.0F);
			label("no border", 80.0F, 120.0F);
			stripedRect(560.0F, 120.0F, Foundation::BorderStyle{.color = kInk, .width = 8.0F});
			label("centred border", 560.0F, 120.0F);
			stripedRect(
				1040.0F,
				120.0F,
				Foundation::BorderStyle{
					.color = kInk, .width = 12.0F, .cornerRadius = 40.0F, .position = Foundation::BorderPosition::Inside
				}
			);
			label("inside border, radius 40", 1040.0F, 120.0F);
			stripedRect(
				1520.0F,
				120.0F,
				Foundation::BorderStyle{
					.color = kInk, .width = 8.0F, .cornerRadius = 110.0F, .position = Foundation::BorderPosition::Outside
				}
			);
			label("outside border, a capsule", 1520.0F, 120.0F);

			// Row 2: a clip, a transform, translucency, and no hook.
			P::pushClip({620.0F, 520.0F, 240.0F, 160.0F});
			stripedRect(560.0F, 490.0F, Foundation::BorderStyle{.color = kInk, .width = 4.0F, .cornerRadius = 24.0F});
			P::popClip();
			label("inside a 240 x 160 clip", 560.0F, 490.0F);
			P::PushTransform(
				glm::translate(
					glm::rotate(
						glm::translate(Foundation::Mat4(1.0F), Foundation::Vec3(1220.0F, 600.0F, 0.0F)),
						std::numbers::pi_v<float> / 12.0F,
						Foundation::Vec3(0.0F, 0.0F, 1.0F)
					),
					Foundation::Vec3(-kWidth * 0.5F, -kHeight * 0.5F, 0.0F)
				)
			);
			stripedRect(0.0F, 0.0F, Foundation::BorderStyle{.color = kInk, .width = 4.0F, .cornerRadius = 24.0F});
			P::PopTransform();
			label("PushTransform, turned 15 degrees", 1040.0F, 530.0F);
			P::drawRect({.bounds = {1520.0F, 560.0F, kWidth, 80.0F}, .style = {.fill = kInk}});
			P::drawRect(
				{.bounds = {1520.0F, 490.0F, kWidth, kHeight},
				 .style = {.fill = kFill, .border = Foundation::BorderStyle{.color = kInk, .width = 4.0F, .cornerRadius = 24.0F}},
				 .fill = stripes({0.95F, 0.7F, 0.3F, 0.5F}, {0.2F, 0.55F, 0.6F, 0.0F})}
			);
			label("translucent and clear stripes over a bar", 1520.0F, 490.0F);
			P::drawRect(
				{.bounds = {80.0F, 490.0F, kWidth, kHeight},
				 .style = {.fill = kFill, .border = Foundation::BorderStyle{.color = kInk, .width = 4.0F, .cornerRadius = 24.0F}}}
			);
			label("no hook: the flat fill", 80.0F, 490.0F);

			// Row 3: the block, between two rects drawn one after the other.
			stripedRect(560.0F, 860.0F);
			label("the block's shift at 0", 560.0F, 860.0F);
			shift(kStripe * 0.5F);
			stripedRect(1040.0F, 860.0F);
			label("then half a stripe", 1040.0F, 860.0F);
			P::PopTransform();
		}
	};

} // namespace

namespace sandbox {
	const SceneInfo kFillHookScene{.name = kSceneName, .make = makeScene<FillHookScene>};
} // namespace sandbox
