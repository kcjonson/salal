#pragma once

// A directory of the test's own under the system's temp directory, removed with everything in it when
// the test ends, so file tests never touch the real per-user directories or each other. Also what the
// file tests share: reading and writing a whole file, and bytes from text.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace app::testing {

	class TempDir {
	  public:
		TempDir() {
			const ::testing::TestInfo* test = ::testing::UnitTest::GetInstance()->current_test_info();
			std::string				   name = test != nullptr ? std::string(test->test_suite_name()) + "." + test->name() : "test";
			name += "-" + std::to_string(std::random_device{}());
			root = std::filesystem::temp_directory_path() / "salal-tests" / name;
			std::filesystem::create_directories(root);
		}
		~TempDir() {
			std::error_code error;
			std::filesystem::remove_all(root, error);
			std::filesystem::remove(root.parent_path(), error); // only once it's empty
		}

		TempDir(const TempDir&) = delete;
		TempDir& operator=(const TempDir&) = delete;
		TempDir(TempDir&&) = delete;
		TempDir& operator=(TempDir&&) = delete;

		const std::filesystem::path& path() const { return root; }
		std::filesystem::path		 operator/(std::string_view name) const { return root / std::string(name); }

	  private:
		std::filesystem::path root;
	};

	inline std::vector<std::byte> bytesOf(std::string_view text) {
		const auto bytes = std::as_bytes(std::span(text.data(), text.size()));
		return {bytes.begin(), bytes.end()};
	}

	inline std::string textOf(std::span<const std::byte> bytes) {
		return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
	}

	inline std::string readText(const std::filesystem::path& file) {
		std::ifstream in(file, std::ios::binary);
		return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
	}

	inline void writeText(const std::filesystem::path& file, std::string_view text) {
		std::ofstream out(file, std::ios::binary | std::ios::trunc);
		out.write(text.data(), static_cast<std::streamsize>(text.size()));
	}

	inline std::vector<std::byte> readBytes(const std::filesystem::path& file) {
		return bytesOf(readText(file));
	}

	inline void writeBytes(const std::filesystem::path& file, std::span<const std::byte> bytes) {
		writeText(file, textOf(bytes));
	}

	// The names in a directory, sorted.
	inline std::vector<std::string> filesIn(const std::filesystem::path& directory) {
		std::vector<std::string> names;
		for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(directory)) {
			names.push_back(entry.path().filename().string());
		}
		std::sort(names.begin(), names.end());
		return names;
	}

} // namespace app::testing
