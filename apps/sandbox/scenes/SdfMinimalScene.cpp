// sdf_minimal: the alphabet and the digits laid out by FontRenderer::generateGlyphQuads and batched
// through Primitives::drawGlyphs, the path text takes without drawText.

#include "Scenes.h"

#include <font/FontRenderer.h>
#include <graphics/Color.h>
#include <primitives/Primitives.h>

#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <vector>

namespace {

	constexpr const char* kSceneName = "sdf_minimal";
	constexpr float		  kScale = 2.0F; // 32 px; the atlas's base size is 16

	const Foundation::Color kBackground(0.1F, 0.1F, 0.2F, 1.0F);

	class SdfMinimalScene final : public app::Scene {
	  public:
		const char* name() const override { return kSceneName; }

		void render(float /*alpha*/) override {
			Renderer::Primitives::clear(kBackground);
			drawLine("ABCDEFGHIJKLMNOPQRSTUVWXYZ", {50.0F, 150.0F});
			drawLine("abcdefghijklmnopqrstuvwxyz", {50.0F, 250.0F});
			drawLine("0123456789", {50.0F, 350.0F});
		}

	  private:
		static void drawLine(const std::string& text, const glm::vec2& position) {
			const Renderer::FontRenderer* fonts = Renderer::Primitives::getFontRenderer();
			if (fonts == nullptr) {
				return;
			}
			std::vector<Renderer::FontRenderer::GlyphQuad> glyphs;
			fonts->generateGlyphQuads(text, position, kScale, glm::vec4(1.0F, 1.0F, 1.0F, 1.0F), glyphs);
			Renderer::Primitives::drawGlyphs(glyphs.data(), glyphs.size());
		}
	};

} // namespace

namespace sandbox {
	const SceneInfo kSdfMinimalScene{.name = kSceneName, .make = makeScene<SdfMinimalScene>};
} // namespace sandbox
