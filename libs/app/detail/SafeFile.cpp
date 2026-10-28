#include "detail/SafeFile.h"

#include <utils/Log.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <system_error>
#include <thread>
#include <utility>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace app::detail {

	namespace {

		constexpr auto kStaleTemp = std::chrono::minutes(10);
		constexpr auto kHeldRetry = std::chrono::milliseconds(50);
		constexpr int  kHeldRetries = 20; // about a second

		std::function<bool(const std::filesystem::path&)>& fault() {
			static std::function<bool(const std::filesystem::path&)> injected;
			return injected;
		}

		bool injectedFault(const std::filesystem::path& target) {
			if (fault() && fault()(target)) {
				LOG_ERROR(Engine, "A fault set by a test fails the write of %s", pathText(target).c_str());
				return true;
			}
			return false;
		}

		bool allDigits(std::string_view text) {
			return !text.empty() && std::ranges::all_of(text, [](char c) { return c >= '0' && c <= '9'; });
		}

		std::uint64_t processId() {
#ifdef _WIN32
			return GetCurrentProcessId();
#else
			return static_cast<std::uint64_t>(getpid());
#endif
		}

		// <target>.<pid>.<n>.tmp, with n counting this process's temp files.
		std::filesystem::path tempName(const std::filesystem::path& target) {
			static std::atomic<std::uint64_t> made{0};
			return withSuffix(target, "." + std::to_string(processId()) + "." + std::to_string(made++) + ".tmp");
		}

#ifdef _WIN32
		bool held(DWORD error) {
			return error == ERROR_ACCESS_DENIED || error == ERROR_SHARING_VIOLATION || error == ERROR_LOCK_VIOLATION;
		}

		// Created exclusively, written, flushed to the disk and closed. ERROR_FILE_EXISTS in `error` when
		// the name was taken.
		bool writeNew(const std::filesystem::path& path, std::span<const std::byte> bytes, DWORD& error) {
			const HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (file == INVALID_HANDLE_VALUE) {
				error = GetLastError();
				return false;
			}
			bool written = true;
			for (std::size_t offset = 0; written && offset < bytes.size();) {
				const auto chunk = static_cast<DWORD>(std::min<std::size_t>(bytes.size() - offset, 1U << 30U));
				DWORD	   wrote = 0;
				written = WriteFile(file, bytes.data() + offset, chunk, &wrote, nullptr) != FALSE && wrote == chunk;
				offset += wrote;
			}
			written = written && FlushFileBuffers(file) != FALSE;
			error = written ? 0 : GetLastError();
			return CloseHandle(file) != FALSE && written;
		}

		// A POSIX-semantics rename through a handle on `from`, which replaces a target others have open;
		// MoveFileExW where the filesystem doesn't take one (FAT, some network shares, Windows before 10).
		bool renameOnce(const std::filesystem::path& from, const std::filesystem::path& to, DWORD& error) {
			const HANDLE source = CreateFileW(
				from.c_str(),
				DELETE | SYNCHRONIZE,
				FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
				nullptr,
				OPEN_EXISTING,
				FILE_FLAG_BACKUP_SEMANTICS, // so a directory moved aside opens too
				nullptr
			);
			if (source == INVALID_HANDLE_VALUE) {
				error = GetLastError();
				return false;
			}
			const std::wstring		target = to.wstring();
			const std::size_t		nameBytes = target.size() * sizeof(wchar_t);
			std::vector<std::byte>	buffer(sizeof(FILE_RENAME_INFO) + nameBytes);
			FILE_RENAME_INFO* const info = reinterpret_cast<FILE_RENAME_INFO*>(buffer.data());
			info->Flags = FILE_RENAME_FLAG_REPLACE_IF_EXISTS | FILE_RENAME_FLAG_POSIX_SEMANTICS;
			info->RootDirectory = nullptr;
			info->FileNameLength = static_cast<DWORD>(nameBytes);
			std::memcpy(info->FileName, target.c_str(), nameBytes);
			bool renamed = SetFileInformationByHandle(source, FileRenameInfoEx, info, static_cast<DWORD>(buffer.size())) != FALSE;
			error = renamed ? 0 : GetLastError();
			CloseHandle(source);
			if (!renamed && (error == ERROR_INVALID_PARAMETER || error == ERROR_NOT_SUPPORTED || error == ERROR_INVALID_FUNCTION)) {
				renamed = MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
				error = renamed ? 0 : GetLastError();
			}
			return renamed;
		}

		std::string describe(DWORD error) {
			return std::system_category().message(static_cast<int>(error));
		}
#else
		// Through to the disk: on Apple fsync leaves the data in the drive's cache, where F_FULLFSYNC doesn't;
		// a volume that can't take F_FULLFSYNC (network, FAT) gets fsync.
		bool syncFile(int fd) {
#ifdef __APPLE__
			if (fcntl(fd, F_FULLFSYNC) == 0) {
				return true;
			}
#endif
			return fsync(fd) == 0;
		}

		// A directory's entries flushed, so a rename or a new directory in it survives a power cut. A
		// filesystem that can't flush a directory (EINVAL) has nothing to do.
		void syncDirectory(const std::filesystem::path& directory) {
			const int fd = open(directory.empty() ? "." : directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
			if (fd < 0) {
				return;
			}
			syncFile(fd);
			close(fd);
		}

		// Created exclusively, written, flushed to the disk and closed. EEXIST in `error` when the name was
		// taken.
		bool writeNew(const std::filesystem::path& path, std::span<const std::byte> bytes, int& error) {
			const int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0666);
			if (fd < 0) {
				error = errno;
				return false;
			}
			bool written = true;
			for (std::size_t offset = 0; written && offset < bytes.size();) {
				const ssize_t wrote = ::write(fd, bytes.data() + offset, bytes.size() - offset);
				if (wrote < 0 && errno == EINTR) {
					continue;
				}
				written = wrote > 0;
				offset += written ? static_cast<std::size_t>(wrote) : 0;
			}
			written = written && syncFile(fd);
			error = written ? 0 : errno;
			return close(fd) == 0 && written;
		}

		std::string describe(int error) {
			return std::generic_category().message(error);
		}
#endif

		// The directory made, with each directory it made flushed into its parent on POSIX.
		bool makeDirectories(const std::filesystem::path& directory) {
			std::error_code error;
			if (directory.empty() || std::filesystem::is_directory(directory, error)) {
				return true;
			}
			std::filesystem::path existing = directory;
			while (existing.has_parent_path() && existing != existing.parent_path() && !std::filesystem::exists(existing, error)) {
				existing = existing.parent_path();
			}
			std::filesystem::create_directories(directory, error);
			if (error) {
				LOG_ERROR(Engine, "Can't create %s: %s", pathText(directory).c_str(), error.message().c_str());
				return false;
			}
#ifndef _WIN32
			for (std::filesystem::path made = directory; made != existing && made.has_parent_path() && made != made.parent_path();
				 made = made.parent_path()) {
				syncDirectory(made.parent_path());
			}
#endif
			return true;
		}

		// What a crashed write of `target` left, once it's old enough that no live write still owns it.
		void sweepTemps(const std::filesystem::path& target) {
			const std::string					  name = pathText(target.filename());
			const std::filesystem::file_time_type cutoff = std::filesystem::file_time_type::clock::now() - kStaleTemp;
			std::error_code						  error;
			const std::filesystem::path			  directory = target.has_parent_path() ? target.parent_path() : std::filesystem::path(".");
			for (std::filesystem::directory_iterator it(directory, error); !error && it != std::filesystem::directory_iterator();
				 it.increment(error)) {
				std::error_code ignored;
				if (tempTargetOf(pathText(it->path().filename())) == std::string_view(name) &&
					std::filesystem::last_write_time(it->path(), ignored) < cutoff) {
					std::filesystem::remove(it->path(), ignored);
				}
			}
		}

	} // namespace

	std::optional<std::string_view> tempTargetOf(std::string_view name) {
		constexpr std::string_view kTmp = ".tmp";
		if (!name.ends_with(kTmp)) {
			return std::nullopt;
		}
		const std::string_view rest = name.substr(0, name.size() - kTmp.size()); // <target>.<pid>.<n>
		const std::size_t	   counterDot = rest.rfind('.');
		if (counterDot == std::string_view::npos || counterDot == 0 || !allDigits(rest.substr(counterDot + 1))) {
			return std::nullopt;
		}
		const std::size_t pidDot = rest.rfind('.', counterDot - 1);
		if (pidDot == std::string_view::npos || pidDot == 0 || !allDigits(rest.substr(pidDot + 1, counterDot - pidDot - 1))) {
			return std::nullopt;
		}
		return rest.substr(0, pidDot);
	}

	std::filesystem::path withSuffix(const std::filesystem::path& file, std::string_view suffix) {
		std::filesystem::path suffixed = file;
		suffixed += std::filesystem::path(std::string(suffix));
		return suffixed;
	}

	std::string pathText(const std::filesystem::path& path) {
		const std::u8string text = path.u8string();
		return {text.begin(), text.end()};
	}

	void setFaultForTests(std::function<bool(const std::filesystem::path& target)> injected) {
		fault() = std::move(injected);
	}

	bool renameReplacing(const std::filesystem::path& from, const std::filesystem::path& to) {
		if (injectedFault(to)) {
			return false;
		}
		std::error_code ignored;
		if (std::filesystem::is_directory(to, ignored) && !std::filesystem::is_directory(from, ignored)) {
			LOG_ERROR(Engine, "Can't replace %s: it's a directory", pathText(to).c_str());
			return false;
		}
#ifdef _WIN32
		DWORD error = 0;
		for (int attempt = 0; attempt <= kHeldRetries; ++attempt) {
			if (renameOnce(from, to, error)) {
				return true;
			}
			if (!held(error)) {
				break;
			}
			std::this_thread::sleep_for(kHeldRetry);
		}
		LOG_ERROR(Engine, "Can't replace %s: %s", pathText(to).c_str(), describe(error).c_str());
		return false;
#else
		if (std::rename(from.c_str(), to.c_str()) != 0) {
			LOG_ERROR(Engine, "Can't replace %s: %s", pathText(to).c_str(), describe(errno).c_str());
			return false;
		}
		syncDirectory(to.parent_path());
		if (from.parent_path() != to.parent_path()) {
			syncDirectory(from.parent_path());
		}
		return true;
#endif
	}

	bool writeFileSafely(const std::filesystem::path& target, std::span<const std::byte> bytes) {
		if (injectedFault(target) || !makeDirectories(target.parent_path())) {
			return false;
		}
		std::filesystem::path temp;
		bool				  written = false;
#ifdef _WIN32
		DWORD		error = 0;
		const DWORD taken = ERROR_FILE_EXISTS;
#else
		int		  error = 0;
		const int taken = EEXIST;
#endif
		for (int attempt = 0; attempt < 100 && !written; ++attempt) {
			temp = tempName(target);
			written = writeNew(temp, bytes, error);
			if (!written && error != taken) {
				break;
			}
		}
		std::error_code ignored;
		if (!written) {
			LOG_ERROR(Engine, "Can't write %s: %s", pathText(temp).c_str(), describe(error).c_str());
			std::filesystem::remove(temp, ignored);
			return false;
		}
		if (!renameReplacing(temp, target)) {
			std::filesystem::remove(temp, ignored);
			return false;
		}
		sweepTemps(target);
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

	std::filesystem::path freeName(const std::filesystem::path& file, std::string_view suffix) {
		std::filesystem::path candidate = withSuffix(file, suffix);
		std::error_code		  ignored;
		for (int n = 1; std::filesystem::exists(std::filesystem::symlink_status(candidate, ignored)); ++n) {
			candidate = withSuffix(file, std::string(suffix) + "." + std::to_string(n));
		}
		return candidate;
	}

	std::optional<std::filesystem::path> moveAside(const std::filesystem::path& file, std::string_view suffix) {
		const std::filesystem::path aside = freeName(file, suffix);
		if (!renameReplacing(file, aside)) {
			LOG_ERROR(Engine, "Can't move %s aside", pathText(file).c_str());
			return std::nullopt;
		}
		LOG_WARNING(Engine, "Moved %s aside to %s", pathText(file).c_str(), pathText(aside).c_str());
		return aside;
	}

} // namespace app::detail
