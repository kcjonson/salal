#include "Handle.h"
#include <gtest/gtest.h>
#include <type_traits>

namespace Renderer {

	TEST(HandleTest, DefaultIsInvalid) {
		const TextureHandle handle;
		EXPECT_FALSE(handle.isValid());
		EXPECT_EQ(handle.value, TextureHandle::kInvalid);
	}

	TEST(HandleTest, PacksIndexAndGeneration) {
		const TextureHandle handle = TextureHandle::make(0x1234, 0xABCD);
		EXPECT_TRUE(handle.isValid());
		EXPECT_EQ(handle.index(), 0x1234);
		EXPECT_EQ(handle.generation(), 0xABCD);
		EXPECT_EQ(handle.value, 0xABCD1234U);
	}

	TEST(HandleTest, EqualityIsByValue) {
		EXPECT_EQ(TextureHandle::make(1, 2), TextureHandle::make(1, 2));
		EXPECT_NE(TextureHandle::make(1, 2), TextureHandle::make(1, 3));
		EXPECT_NE(TextureHandle::make(1, 2), TextureHandle{});
	}

	TEST(HandleTest, TagsAreDistinctTypes) {
		using OtherHandle = Handle<struct OtherTag>;
		static_assert(!std::is_same_v<TextureHandle, OtherHandle>);
		static_assert(sizeof(TextureHandle) == sizeof(uint32_t));
	}

} // namespace Renderer
