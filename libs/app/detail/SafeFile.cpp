#include "detail/SafeFile.h"

#include <utils/Log.h>

#include <array>
#include <cstdio>
#include <fstream>
#include <string>
#include <system_error>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

namespace app::detail {

	namespace {

		// Written, flushed out of the C library and then out of the OS's cache to the disk, and closed.
		bool writeAndSync(const std::filesystem::path& path, std::span<const std::byte> bytes) {
#ifdef _WIN32
			FILE* file = nullptr;
			if (_wfopen_s(&file, path.c_str(), L"wb") != 0 || file == nullptr) {
				return false;
			}
#else
			FILE* file = std::fopen(path.c_str(), "wb");
			if (file == nullptr) {
				return false;
			}
#endif
			bool written = bytes.empty() || std::fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size();
			written = std::fflush(file) == 0 && written;
#ifdef _WIN32
			written = written && _commit(_fileno(file)) == 0;
#else
			written = written && fsync(fileno(file)) == 0;
#endif
			return std::fclose(file) == 0 && written;
		}

	} // namespace

	std::filesystem::path withSuffix(const std::filesystem::path& file, std::string_view suffix) {
		std::filesystem::path suffixed = file;
		suffixed += std::filesystem::path(std::string(suffix));
		return suffixed;
	}

	std::string pathText(const std::filesystem::path& path) {
		const std::u8string text = path.u8string();
		return {text.begin(), text.end()};
	}

	std::filesystem::path tempFor(const std::filesystem::path& target) {
		return withSuffix(target, ".tmp");
	}

	bool writeFileSafely(const std::filesystem::path& target, std::span<const std::byte> bytes) {
		std::error_code				error;
		const std::filesystem::path directory = target.parent_path();
		if (!directory.empty()) {
			std::filesystem::create_directories(directory, error);
			if (error) {
				LOG_ERROR(Engine, "Can't create %s: %s", pathText(directory).c_str(), error.message().c_str());
				return false;
			}
		}
		const std::filesystem::path temp = tempFor(target);
		if (!writeAndSync(temp, bytes)) {
			LOG_ERROR(Engine, "Can't write %s", pathText(temp).c_str());
			std::filesystem::remove(temp, error);
			return false;
		}
		std::filesystem::rename(temp, target, error);
		if (error) {
			LOG_ERROR(Engine, "Can't replace %s: %s", pathText(target).c_str(), error.message().c_str());
			std::filesystem::remove(temp, error);
			return false;
		}
		return true;
	}

	FileRead readFile(const std::filesystem::path& file) {
		std::error_code					   error;
		const std::filesystem::file_status status = std::filesystem::status(file, error);
		if (status.type() == std::filesystem::file_type::not_found) {
			return {.status = FileRead::Status::Missing};
		}
		if (error || status.type() != std::filesystem::file_type::regular) {
			return {.status = FileRead::Status::Unreadable};
		}
		std::ifstream in(file, std::ios::binary);
		if (!in) {
			return {.status = FileRead::Status::Unreadable};
		}
		FileRead				read{.status = FileRead::Status::Read};
		std::array<char, 65536> chunk{};
		while (in.read(chunk.data(), chunk.size()) || in.gcount() > 0) {
			const auto* begin = reinterpret_cast<const std::byte*>(chunk.data());
			read.bytes.insert(read.bytes.end(), begin, begin + in.gcount());
		}
		if (in.bad()) {
			return {.status = FileRead::Status::Unreadable};
		}
		return read;
	}

	bool moveAside(const std::filesystem::path& file, std::string_view suffix) {
		std::error_code				error;
		const std::filesystem::path aside = withSuffix(file, suffix);
		std::filesystem::rename(file, aside, error);
		if (error) {
			LOG_ERROR(Engine, "Can't move %s aside: %s", pathText(file).c_str(), error.message().c_str());
			return false;
		}
		LOG_WARNING(Engine, "Moved %s aside to %s", pathText(file).c_str(), pathText(aside).c_str());
		return true;
	}

} // namespace app::detail
