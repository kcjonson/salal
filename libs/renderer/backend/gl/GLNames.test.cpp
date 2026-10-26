// The registry is a table of names, so it needs no GL context.

#include "backend/gl/GLNames.h"
#include <gtest/gtest.h>

namespace Renderer::backend::gl {

	TEST(GLNamesTest, InvalidHandleNamesNothing) {
		EXPECT_EQ(name(TextureHandle{}), 0U);
	}

	TEST(GLNamesTest, RegisteredNameReadsBack) {
		const TextureHandle handle = registerTexture(42);
		EXPECT_TRUE(handle.isValid());
		EXPECT_EQ(name(handle), 42U);
		unregisterTexture(handle);
		EXPECT_EQ(name(handle), 0U);
	}

	TEST(GLNamesTest, DistinctHandlesPerRegistration) {
		const TextureHandle a = registerTexture(7);
		const TextureHandle b = registerTexture(7);
		EXPECT_NE(a, b);
		EXPECT_EQ(name(a), 7U);
		EXPECT_EQ(name(b), 7U);
		unregisterTexture(a);
		EXPECT_EQ(name(a), 0U);
		EXPECT_EQ(name(b), 7U);
		unregisterTexture(b);
	}

	TEST(GLNamesTest, ReusedSlotGetsNewGeneration) {
		const TextureHandle stale = registerTexture(1);
		unregisterTexture(stale);
		const TextureHandle fresh = registerTexture(2);
		EXPECT_EQ(fresh.index(), stale.index());
		EXPECT_NE(fresh.generation(), stale.generation());
		EXPECT_EQ(name(stale), 0U);
		EXPECT_EQ(name(fresh), 2U);
		unregisterTexture(fresh);
	}

	TEST(GLNamesTest, UnregisterTwiceIsHarmless) {
		const TextureHandle handle = registerTexture(3);
		unregisterTexture(handle);
		unregisterTexture(handle);
		const TextureHandle next = registerTexture(4);
		EXPECT_EQ(name(next), 4U);
		unregisterTexture(next);
	}

} // namespace Renderer::backend::gl
