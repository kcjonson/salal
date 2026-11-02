// sdf_minimal: the alphabet and the digits laid out by FontRenderer::generateGlyphQuads and batched
// through Primitives::drawGlyphs, the path text takes without drawText; then the same glyphs'
// shadow through drawGlyphShadow, a halo and a drop shadow over stripes, at 48 px, where the atlas's
// field reaches 3 px past the outline.

#include "Scenes.h"

#include <font/FontRenderer.h>
#include <graphics/Color.h>
#include <graphics/PrimitiveStyles.h>
#include <primitives/Primitives.h>

#include <glm/glm.hpp>

#include <optional>
#include <string>
#include <vector>

namespace {

	constexpr const char* kSceneName = "sdf_minimal";
	constexpr float		  kScale = 2.0F;	   // 32 px; the atlas's base size is 16
	constexpr float		  kShadowScale = 3.0F; // 48 px

	const Foundation::Color kBackground(0.1F, 0.1F, 0.2F, 1.0F);
	const Foundation::Color kStripe(0.35F, 0.55F, 0.4F, 1.0F);
	const Foundation::Color kInk(0.1F, 0.08F, 0.06F, 1.0F);
	const Foundation::Color kPaper(0.93F, 0.89F, 0.78F, 1.0F);

	class SdfMinimalScene final : public app::Scene {
	  public:
		const char* name() const override { return kSceneName; }

		void render(float /*alpha*/) override {
			Renderer::Primitives::clear(kBackground);
			drawLine("ABCDEFGHIJKLMNOPQRSTUVWXYZ", {50.0F, 150.0F}, kScale, Foundation::Color::white());
			drawLine("abcdefghijklmnopqrstuvwxyz", {50.0F, 250.0F}, kScale, Foundation::Color::white());
			drawLine("0123456789", {50.0F, 350.0F}, kScale, Foundation::Color::white());

			for (int i = 0; i < 24; ++i) {
				Renderer::Primitives::drawRect(
					{.bounds = {50.0F + (static_cast<float>(i) * 32.0F), 430.0F, 16.0F, 170.0F}, .style = {.fill = kStripe}}
				);
			}
			drawLine(
				"Halo over stripes",
				{70.0F, 450.0F},
				kShadowScale,
				kInk,
				Foundation::BoxShadow{.color = kPaper, .blur = 1.0F, .spread = 2.0F}
			);
			drawLine(
				"Drop shadow",
				{70.0F, 530.0F},
				kShadowScale,
				kPaper,
				Foundation::BoxShadow{.color = kInk, .blur = 2.0F, .offset = {3.0F, 3.0F}}
			);
		}

	  private:
		static void drawLine(
			const std::string&							text,
			const glm::vec2&							position,
			float										scale,
			const Foundation::Color&					color,
			const std::optional<Foundation::BoxShadow>& shadow = std::nullopt
		) {
			const Renderer::FontRenderer* fonts = Renderer::Primitives::getFontRenderer();
			if (fonts == nullptr) {
				return;
			}
			std::vector<Renderer::FontRenderer::GlyphQuad> glyphs;
			fonts->generateGlyphQuads(text, position, scale, glm::vec4(color.r, color.g, color.b, color.a), glyphs);
			if (shadow) {
				Renderer::Primitives::drawGlyphShadow(glyphs.data(), glyphs.size(), *shadow);
			}
			Renderer::Primitives::drawGlyphs(glyphs.data(), glyphs.size());
		}
	};

} // namespace

namespace sandbox {
	const SceneInfo kSdfMinimalScene{.name = kSceneName, .make = makeScene<SdfMinimalScene>};
} // namespace sandbox
