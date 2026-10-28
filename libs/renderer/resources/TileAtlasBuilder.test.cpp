// The caller names each surface's pattern by resource path: found through the resource roots and
// baked, or the surface gets a checker in its colour, which the builder asks for only then. On the
// null backend, so no GL.

#include "resources/TileAtlasBuilder.h"

#include "BaseShader.h"
#include "backend/NullBackend.h"
#include "primitives/Primitives.h"

#include <utils/Log.h>
#include <utils/ResourcePath.h>

#include <gtest/gtest.h>

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <random>
#include <string>
#include <system_error>
#include <vector>

namespace Renderer {

	namespace {

		namespace fs = std::filesystem;

		constexpr const char* kPattern =
			R"(<svg xmlns="http://www.w3.org/2000/svg" width="16" height="16"><rect width="16" height="16" fill="#808080"/></svg>)";

		// A resource root holding patterns/stone.svg, set for the test and cleared after, with the
		// renderer started on a null backend.
		class TileAtlasBuilderTest : public ::testing::Test {
		  protected:
			void SetUp() override {
				fs::create_directories(root / "patterns");
				std::ofstream(root / "patterns" / "stone.svg") << kPattern;
				Foundation::setResourceRoots({root});
				Primitives::init(std::make_unique<backend::NullBackend>(), base_shader::source());
			}
			void TearDown() override {
				Primitives::shutdown();
				Foundation::setResourceRoots({});
				std::error_code ignored;
				fs::remove_all(root, ignored);
			}

			std::vector<glm::vec4> build(TileAtlasBuilder& builder) {
				return builder.buildForSurfaces(
					3,
					[](int surface) -> std::string {
						if (surface == 0) {
							return "patterns/stone.svg";
						}
						return surface == 1 ? "" : "patterns/missing.svg";
					},
					[this](int surface) {
						colourAskedFor.push_back(surface);
						return Foundation::Color(0.5F, 0.4F, 0.3F, 1.0F);
					}
				);
			}

			const fs::path root = fs::temp_directory_path() /
								  ("salal-tile-atlas-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
								   std::to_string(std::random_device{}()));
			std::vector<int> colourAskedFor;
		};

#ifdef DEVELOPMENT_BUILD
		void countMissingPatterns(void* context, const foundation::LogRecord& record) {
			if (record.level == foundation::LogLevel::Warning && std::strstr(record.message, "missing.svg") != nullptr) {
				++*static_cast<int*>(context);
			}
		}
#endif

	} // namespace

	TEST_F(TileAtlasBuilderTest, AFoundPatternIsBakedAndTheRestGetTheirColoursChecker) {
		TileAtlasBuilder			 builder({.patternSize = 16, .atlasSize = 64});
		const std::vector<glm::vec4> rects = build(builder);
		EXPECT_EQ(rects.size(), 3U);
		EXPECT_TRUE(builder.texture().isValid());
		EXPECT_EQ(colourAskedFor, (std::vector<int>{1, 2}));
	}

#ifdef DEVELOPMENT_BUILD
	TEST_F(TileAtlasBuilderTest, APatternPathThatIsNotFoundIsAWarning) {
		int warnings = 0;
		foundation::Logger::setSink(countMissingPatterns, &warnings);
		TileAtlasBuilder builder({.patternSize = 16, .atlasSize = 64});
		(void)build(builder);
		foundation::Logger::setSink(nullptr, nullptr);
		EXPECT_EQ(warnings, 1);
	}
#endif

} // namespace Renderer
