#include "diagnostics/Diagnostics.h"

#include "detail/SafeFile.h"
#include "diagnostics/CrashHandler.h"
#include "storage/UserDirectories.h"

#include <utils/Log.h>
#include <utils/LogFile.h>

#include <algorithm>
#include <array>
#include <ctime>
#include <functional>
#include <map>
#include <system_error>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

#ifndef SALAL_BUILD_CONFIG
#define SALAL_BUILD_CONFIG "unknown"
#endif

namespace app {

	namespace {

		constexpr std::array<std::string_view, 4> kSuffixes{".1.log", ".log", ".txt", ".dmp"};

		struct Run {
			std::vector<std::filesystem::path> files;
			std::filesystem::path			   log; // its <run>.log; empty when it has none
			std::filesystem::file_time_type	   latest = std::filesystem::file_time_type::min();
		};

		// Newest first.
		using Runs = std::map<std::string, Run, std::greater<>>;

		bool digits(std::string_view text) {
			return !text.empty() && std::ranges::all_of(text, [](char c) { return c >= '0' && c <= '9'; });
		}

		Runs runsIn(const std::filesystem::path& directory) {
			Runs			runs;
			std::error_code error;
			for (std::filesystem::directory_iterator it(directory, error); !error && it != std::filesystem::directory_iterator();
				 it.increment(error)) {
				const std::string					  name = detail::pathText(it->path().filename());
				const std::optional<std::string_view> run = runOf(name);
				std::error_code						  ignored;
				if (!run || !it->is_regular_file(ignored)) {
					continue;
				}
				Run& entry = runs[std::string(*run)];
				entry.files.push_back(it->path());
				if (name.size() == run->size() + 4 && name.ends_with(".log")) {
					entry.log = it->path();
				}
				const std::filesystem::file_time_type written = std::filesystem::last_write_time(it->path(), ignored);
				if (!ignored && written > entry.latest) {
					entry.latest = written;
				}
			}
			return runs;
		}

		// Keeps the newest `kept` runs no older than `age`, and any whose log a process still has open.
		void removeBeyond(const Runs& runs, std::size_t kept, std::chrono::hours age) {
			const std::filesystem::file_time_type cutoff = std::filesystem::file_time_type::clock::now() - age;
			std::size_t							  keeping = 0;
			for (const auto& [name, run] : runs) {
				if (keeping < kept && run.latest >= cutoff) {
					++keeping;
					continue;
				}
				if (!run.log.empty() && foundation::logFileInUse(run.log)) {
					continue;
				}
				for (const std::filesystem::path& file : run.files) {
					std::error_code ignored;
					std::filesystem::remove(file, ignored);
				}
			}
		}

		std::uint64_t processId() {
#ifdef _WIN32
			return GetCurrentProcessId();
#else
			return static_cast<std::uint64_t>(getpid());
#endif
		}

		std::tm localTime(std::chrono::system_clock::time_point when) {
			const std::time_t seconds = std::chrono::system_clock::to_time_t(when);
			std::tm			  local{};
#ifdef _WIN32
			localtime_s(&local, &seconds);
#else
			localtime_r(&seconds, &local);
#endif
			return local;
		}

	} // namespace

	std::filesystem::path diagnosticsDirectory(std::string_view application) {
		if (!validFileName(application)) {
			return {};
		}
		std::error_code				error;
		const std::filesystem::path temp = std::filesystem::temp_directory_path(error);
		return error ? std::filesystem::path() : temp / std::filesystem::path(std::u8string(application.begin(), application.end()));
	}

	std::string runName(std::chrono::system_clock::time_point started, std::uint64_t pid) {
		const std::tm local = localTime(started);
		char		  stamp[32];
		std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &local);
		return std::string(stamp) + "-" + std::to_string(pid);
	}

	std::optional<std::string_view> runOf(std::string_view fileName) {
		for (std::string_view suffix : kSuffixes) {
			if (!fileName.ends_with(suffix)) {
				continue;
			}
			const std::string_view run = fileName.substr(0, fileName.size() - suffix.size());
			// yyyyMMdd-HHmmss-<pid>
			if (run.size() > 16 && digits(run.substr(0, 8)) && run[8] == '-' && digits(run.substr(9, 6)) && run[15] == '-' &&
				digits(run.substr(16))) {
				return run;
			}
			return std::nullopt;
		}
		return std::nullopt;
	}

	void pruneDiagnostics(const std::filesystem::path& root, const DiagnosticsLimits& limits) {
		const std::filesystem::path logs = root / "logs";
		const std::filesystem::path crashes = root / "crashes";

		// A crashed run's log joins its report, so it's kept as long as the crash is.
		const Runs crashed = runsIn(crashes);
		for (const auto& [name, run] : runsIn(logs)) {
			if (!crashed.contains(name) || (!run.log.empty() && foundation::logFileInUse(run.log))) {
				continue;
			}
			for (const std::filesystem::path& file : run.files) {
				std::error_code ignored;
				std::filesystem::rename(file, crashes / file.filename(), ignored);
			}
		}

		removeBeyond(runsIn(logs), limits.runs, limits.runAge);
		removeBeyond(runsIn(crashes), limits.crashes, limits.crashAge);
	}

	std::unique_ptr<Diagnostics>
	Diagnostics::start(const std::filesystem::path& root, std::string_view application, const DiagnosticsLimits& limits) {
		std::error_code error;
		std::filesystem::create_directories(root / "logs", error);
		if (!error) {
			std::filesystem::create_directories(root / "crashes", error);
		}
		if (error) {
			LOG_ERROR(
				Engine, "No log file and no crash reports: %s can't be made (%s)", detail::pathText(root).c_str(), error.message().c_str()
			);
			return nullptr;
		}
		pruneDiagnostics(root, limits);

		const std::chrono::system_clock::time_point started = std::chrono::system_clock::now();
		const std::uint64_t							pid = processId();
		const std::string							run = runName(started, pid);
		const std::tm								local = localTime(started);
		char										when[32];
		std::strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%S", &local);
		const std::string header =
			std::string(application) + ", " + SALAL_BUILD_CONFIG + " build, pid " + std::to_string(pid) + ", started " + when;

		std::unique_ptr<Diagnostics> diagnostics(new Diagnostics);
		diagnostics->report = root / "crashes" / (run + ".txt");
		const std::filesystem::path log = root / "logs" / (run + ".log");
		if (foundation::openLogFile(log, limits.rollBytes, header)) {
			diagnostics->log = log;
		} else {
			LOG_ERROR(Engine, "No log file: %s can't be opened", detail::pathText(log).c_str());
		}
		diagnostics->crashHandler =
			CrashHandler::install({.report = diagnostics->report, .dump = root / "crashes" / (run + ".dmp"), .header = header});
		LOG_INFO(
			Engine, "Logging to %s; crash reports go to %s", detail::pathText(log).c_str(), detail::pathText(root / "crashes").c_str()
		);
		return diagnostics;
	}

	Diagnostics::~Diagnostics() {
		crashHandler.reset();
		if (!log.empty()) {
			foundation::closeLogFile();
		}
	}

} // namespace app
