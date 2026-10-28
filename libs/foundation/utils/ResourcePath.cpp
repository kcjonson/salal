#include "utils/ResourcePath.h"

#include <mutex>
#include <system_error>
#include <utility>
#include <vector>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace Foundation {

	namespace {
		std::mutex						   g_rootsMutex;
		std::vector<std::filesystem::path> g_roots;
	} // namespace

	std::filesystem::path getExecutableDir() {
#ifdef __APPLE__
		uint32_t bufsize = 0;
		_NSGetExecutablePath(nullptr, &bufsize);
		std::vector<char> buf(bufsize);
		if (_NSGetExecutablePath(buf.data(), &bufsize) == 0) {
			return std::filesystem::path(buf.data()).parent_path();
		}
		return {};
#elif defined(_WIN32)
		std::vector<wchar_t> buf(MAX_PATH);
		for (;;) {
			const DWORD len = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
			if (len == 0) {
				return {};
			}
			if (len < buf.size()) {
				return std::filesystem::path(std::wstring(buf.data(), len)).parent_path();
			}
			buf.resize(buf.size() * 2); // path was truncated; grow and retry
		}
#elif defined(__linux__)
		std::error_code				error;
		const std::filesystem::path exe = std::filesystem::read_symlink("/proc/self/exe", error);
		return error ? std::filesystem::path{} : exe.parent_path();
#else
		return {};
#endif
	}

	void setResourceRoots(std::vector<std::filesystem::path> roots) {
		const std::scoped_lock lock(g_rootsMutex);
		g_roots = std::move(roots);
	}

	std::optional<std::filesystem::path> findResource(const std::filesystem::path& relativePath) {
		std::vector<std::filesystem::path> searchPaths;
		{
			const std::scoped_lock lock(g_rootsMutex);
			searchPaths = g_roots;
		}

		if (auto exeDir = getExecutableDir(); !exeDir.empty()) {
			searchPaths.push_back(std::move(exeDir));
		}

		// The working directory may be gone (common in IDE terminals); then it is skipped.
		std::error_code cwdError;
		if (auto cwd = std::filesystem::current_path(cwdError); !cwdError) {
			searchPaths.push_back(std::move(cwd));
		}

		for (const auto& searchPath : searchPaths) {
			auto			fullPath = searchPath / relativePath;
			std::error_code ignored;
			if (std::filesystem::exists(fullPath, ignored)) {
				return fullPath;
			}
		}

		return std::nullopt;
	}

	std::string findResourceString(const std::filesystem::path& relativePath) {
		auto result = findResource(relativePath);
		return result ? result->string() : "";
	}

} // namespace Foundation
