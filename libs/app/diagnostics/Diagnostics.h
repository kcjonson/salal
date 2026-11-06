#pragma once

// A game's logs and crash reports on disk, under <temp>/<applicationName>/ unless the game names another
// folder: logs/ with one file per run, and crashes/ with a bundle per crash (its report, its minidump on
// Windows, and the run's log, moved in at the next start). Every build and worktree of a game shares the
// folder, so the limits hold however many there are, and the system's own temp cleanup is a backstop.
//
// Each start prunes the folder before the run's own log opens: logs/ keeps the newest runs and none older
// than a week, crashes/ the newest crashes and none older than a month. Only files named as runs are
// touched (<yyyyMMdd-HHmmss>-<pid> then .log, .1.log, .txt or .dmp), and never a run whose log another
// process still has open.

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace app {

	class CrashHandler;

	struct DiagnosticsLimits {
		std::size_t		   runs = 20;
		std::chrono::hours runAge{24 * 7};
		std::size_t		   crashes = 10;
		std::chrono::hours crashAge{24 * 30};
		std::uint64_t	   rollBytes = std::uint64_t{8} * 1024 * 1024; // so a run keeps its last 8 to 16 MB
	};

	// <temp>/<application>; empty for a name validFileName refuses, an empty one included.
	std::filesystem::path diagnosticsDirectory(std::string_view application);

	// A run's name, <yyyyMMdd-HHmmss>-<pid> in local time, which sorts by when it started.
	std::string runName(std::chrono::system_clock::time_point started, std::uint64_t pid);
	// The run a file in the folder belongs to; nullopt for a file the folder didn't name.
	std::optional<std::string_view> runOf(std::string_view fileName);

	// Moves each crashed run's log from logs/ into crashes/, then removes the runs and crashes the limits
	// don't keep, oldest first.
	void pruneDiagnostics(const std::filesystem::path& root, const DiagnosticsLimits& limits);

	// A run's log file and crash handler, from App::start to ~App.
	class Diagnostics {
	  public:
		// The folder pruned, then the run's log opened with what was logged before it, then the crash
		// handler installed; null, logged, when the folder can't be made.
		static std::unique_ptr<Diagnostics>
		start(const std::filesystem::path& root, std::string_view application, const DiagnosticsLimits& limits = {});
		// The crash handler's predecessors back, then the log closed.
		~Diagnostics();

		Diagnostics(const Diagnostics&) = delete;
		Diagnostics& operator=(const Diagnostics&) = delete;
		Diagnostics(Diagnostics&&) = delete;
		Diagnostics& operator=(Diagnostics&&) = delete;

		// Empty when it couldn't be opened.
		const std::filesystem::path& logFile() const { return log; }
		// Where a crash's report goes, its minidump beside it as <run>.dmp on Windows.
		const std::filesystem::path& crashReport() const { return report; }

	  private:
		Diagnostics() = default;

		std::filesystem::path		  log;
		std::filesystem::path		  report;
		std::unique_ptr<CrashHandler> crashHandler;
	};

} // namespace app
