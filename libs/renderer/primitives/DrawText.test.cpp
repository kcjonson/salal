// drawText with no font renderer draws nothing and warns once, not on every call. On the null
// backend, so no GL; the warning is counted through the log sink, which development builds have.

#include "BaseShader.h"
#include "backend/NullBackend.h"
#include "primitives/Primitives.h"

#include <utils/Log.h>

#include <gtest/gtest.h>

#include <cstring>
#include <memory>

namespace Renderer {

#ifdef DEVELOPMENT_BUILD
	namespace {
		void countDrawTextWarnings(void* context, const foundation::LogRecord& record) {
			if (record.level == foundation::LogLevel::Warning && std::strstr(record.message, "drawText") != nullptr) {
				++*static_cast<int*>(context);
			}
		}
	} // namespace

	TEST(DrawTextTest, WithNoFontRendererItWarnsOnceUntilOneIsSet) {
		Primitives::init(std::make_unique<backend::NullBackend>(), base_shader::source());
		Primitives::setFontRenderer(nullptr);
		int warnings = 0;
		foundation::Logger::setSink(countDrawTextWarnings, &warnings);
		for (int i = 0; i < 3; ++i) {
			Primitives::drawText({.text = "no font", .position = {0.0F, 0.0F}});
		}
		EXPECT_EQ(warnings, 1);

		Primitives::setFontRenderer(nullptr); // setting one, even none, starts the count again
		Primitives::drawText({.text = "no font", .position = {0.0F, 0.0F}});
		EXPECT_EQ(warnings, 2);

		foundation::Logger::setSink(nullptr, nullptr);
		Primitives::shutdown();
	}
#endif

} // namespace Renderer
