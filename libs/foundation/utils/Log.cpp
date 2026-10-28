#include "Log.h"

#include <atomic>
#include <cstdio>
#include <ctime>
#include <thread>

namespace foundation {

	// Static member initialization
	LogLevel Logger::s_levels[static_cast<int>(LogCategory::Count)];
	bool	 Logger::s_initialized = false;

#ifdef DEVELOPMENT_BUILD
	std::atomic<Logger::LogSinkFn> Logger::s_sinkFn{nullptr};
	std::atomic<void*>			   Logger::s_sinkContext{nullptr};
	std::atomic<int>			   Logger::s_sinkCalls{0};

	namespace {
		// Counts one log() call as inside the sink from before it loads the sink until it returns,
		// however the sink returns.
		class SinkCall {
		  public:
			explicit SinkCall(std::atomic<int>& calls)
				: count(calls) {
				count.fetch_add(1);
			}
			~SinkCall() { count.fetch_sub(1); }
			SinkCall(const SinkCall&) = delete;
			SinkCall& operator=(const SinkCall&) = delete;
			SinkCall(SinkCall&&) = delete;
			SinkCall& operator=(SinkCall&&) = delete;

		  private:
			std::atomic<int>& count;
		};
	} // namespace
#endif

	void Logger::initialize() {
		if (s_initialized) {
			return;
		}

		// Set default log levels for each category
		// Development: Debug for most categories, Info for less verbose ones
		// These can be overridden by config or runtime calls to setLevel()
#ifdef DEVELOPMENT_BUILD
		s_levels[static_cast<int>(LogCategory::Renderer)] = LogLevel::Info;
		s_levels[static_cast<int>(LogCategory::Physics)] = LogLevel::Info;
		s_levels[static_cast<int>(LogCategory::Audio)] = LogLevel::Info;
		s_levels[static_cast<int>(LogCategory::Network)] = LogLevel::Info;
		s_levels[static_cast<int>(LogCategory::Game)] = LogLevel::Debug;
		s_levels[static_cast<int>(LogCategory::World)] = LogLevel::Info;
		s_levels[static_cast<int>(LogCategory::UI)] = LogLevel::Info;
		s_levels[static_cast<int>(LogCategory::Engine)] = LogLevel::Info;
		s_levels[static_cast<int>(LogCategory::Foundation)] = LogLevel::Info;
#else
		// Release builds: only errors
		for (int i = 0; i < static_cast<int>(LogCategory::Count); i++) {
			s_levels[i] = LogLevel::Error;
		}
#endif

		s_initialized = true;
	}

	void Logger::shutdown() {
		setSink(nullptr, nullptr);
		s_initialized = false;
	}

	// The old sink goes first, then the calls already in it finish: log() counts itself in before it
	// loads the sink, so with every access sequentially consistent, a call this wait misses saw the
	// null. Only then does the new context go in, before the sink that reads it.
	void Logger::setSink(LogSinkFn fn, void* context) {
#ifdef DEVELOPMENT_BUILD
		s_sinkFn.store(nullptr);
		while (s_sinkCalls.load() != 0) {
			std::this_thread::yield();
		}
		if (fn != nullptr) {
			s_sinkContext.store(context);
			s_sinkFn.store(fn);
		}
#else
		(void)fn;
		(void)context;
#endif
	}

	void Logger::setLevel(LogCategory category, LogLevel level) {
		s_levels[static_cast<int>(category)] = level; // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index)
	}

	LogLevel Logger::getLevel(LogCategory category) {
		return s_levels[static_cast<int>(category)]; // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index)
	}

	void Logger::log(LogCategory category, LogLevel level, const char* file, int line, const char* format, ...) {
		// Format message first (needed for both console and sink)
		char	message[256];
		va_list args;
		va_start(args, format);
		vsnprintf(message, sizeof(message), format, args);
		va_end(args);

#ifdef DEVELOPMENT_BUILD
		// Sink sees every message regardless of the console filter (dev client filters itself).
		// Call the loaded value, never re-read the atomic: a concurrent clear must not split the check from the call.
		{
			const SinkCall counted(s_sinkCalls);
			if (LogSinkFn fn = s_sinkFn.load(); fn != nullptr) {
				fn(s_sinkContext.load(), LogRecord{category, level, message, file, line});
			}
		}
#endif

		// Filter by level for console output
		if (level < getLevel(category)) {
			return; // Too verbose for console, skip console output
		}

		// Get timestamp
		time_t now = time(nullptr);
		tm*	   timeinfo = localtime(&now);
		char   timestamp[32];
		strftime(timestamp, sizeof(timestamp), "%H:%M:%S", timeinfo);

		// ANSI color codes for different levels
		const char* colorCode = "";
		const char* resetCode = "\033[0m";

#ifdef DEVELOPMENT_BUILD
		switch (level) {
			case LogLevel::Debug:
				colorCode = "\033[90m";
				break; // Gray
			case LogLevel::Info:
				colorCode = "\033[37m";
				break; // White
			case LogLevel::Warning:
				colorCode = "\033[33m";
				break; // Yellow
			case LogLevel::Error:
				colorCode = "\033[31m";
				break; // Red
		}
#else
		resetCode = ""; // No colors in release builds
#endif

		// Print to console: [TIME][CATEGORY][LEVEL] message
		printf("%s[%s][%s][%s]%s %s", colorCode, timestamp, categoryToString(category), levelToString(level), resetCode, message);

		// Add file:line for errors and warnings
		if (level >= LogLevel::Warning) {
			printf(" (%s:%d)", file, line);
		}

		printf("\n");
		fflush(stdout);
	}

	const char* categoryToString(LogCategory cat) {
		switch (cat) {
			case LogCategory::Renderer:
				return "Renderer";
			case LogCategory::Physics:
				return "Physics";
			case LogCategory::Audio:
				return "Audio";
			case LogCategory::Network:
				return "Network";
			case LogCategory::Game:
				return "Game";
			case LogCategory::World:
				return "World";
			case LogCategory::UI:
				return "UI";
			case LogCategory::Engine:
				return "Engine";
			case LogCategory::Foundation:
				return "Foundation";
			default:
				return "Unknown";
		}
	}

	const char* levelToString(LogLevel level) {
		switch (level) {
			case LogLevel::Debug:
				return "DEBUG";
			case LogLevel::Info:
				return "INFO";
			case LogLevel::Warning:
				return "WARN";
			case LogLevel::Error:
				return "ERROR";
			default:
				return "?????";
		}
	}

} // namespace foundation
