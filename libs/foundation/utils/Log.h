#pragma once

#include <atomic>
#include <cstdarg>
#include <cstdint>

namespace foundation {

	enum class LogLevel : std::uint8_t { // NOLINT(performance-enum-size)
		Debug,
		Info,
		Warning,
		Error
	};

	enum class LogCategory : std::uint8_t { // NOLINT(performance-enum-size)
		Renderer,
		Physics,
		Audio,
		Network,
		Game,
		World,
		UI,
		Engine,
		Foundation,
		Count
	};

	// One formatted log call. Handed to the sink BEFORE the console level filter
	// (the sink sees every message and filters itself). message is null-terminated,
	// at most 255 chars, and valid only for the duration of the call.
	struct LogRecord {
		LogCategory category;
		LogLevel	level;
		const char* message;
		const char* file; // __FILE__ of the call site (static storage)
		int			line;
	};

	class Logger {
	  public:
		static void initialize();
		static void shutdown();

		static void		setLevel(LogCategory category, LogLevel level);
		static LogLevel getLevel(LogCategory category);

		static void log(LogCategory category, LogLevel level, const char* file, int line, const char* format, ...);

		// Called for every formatted message BEFORE the console level filter, from whichever
		// thread logged. Development builds only.
		using LogSinkFn = void (*)(void* context, const LogRecord& record);

		// Install (fn non-null) or clear (fn null) the sink, from any thread but a sink's own call.
		// Either first waits for calls already inside the old sink to return, so its context may be
		// destroyed once this returns, while other threads go on logging.
		static void setSink(LogSinkFn fn, void* context);

	  private:
		static LogLevel s_levels[static_cast<int>(LogCategory::Count)]; // NOLINT(readability-identifier-naming)
		static bool		s_initialized;									// NOLINT(readability-identifier-naming)

#ifdef DEVELOPMENT_BUILD
		static std::atomic<LogSinkFn> s_sinkFn;		 // NOLINT(readability-identifier-naming)
		static std::atomic<void*>	  s_sinkContext; // NOLINT(readability-identifier-naming)
		// log() calls from before they load the sink until they are back from it.
		static std::atomic<int> s_sinkCalls; // NOLINT(readability-identifier-naming)
#endif
	};

	// Helper functions to convert enums to strings
	const char* categoryToString(LogCategory cat);
	const char* levelToString(LogLevel level);

} // namespace foundation

// Convenience macros that automatically capture file and line
// NOLINTBEGIN(cppcoreguidelines-macro-usage)
#define LOG_DEBUG(category, format, ...)                                                                                                   \
	foundation::Logger::log(foundation::LogCategory::category, foundation::LogLevel::Debug, __FILE__, __LINE__, format, ##__VA_ARGS__)

#define LOG_INFO(category, format, ...)                                                                                                    \
	foundation::Logger::log(foundation::LogCategory::category, foundation::LogLevel::Info, __FILE__, __LINE__, format, ##__VA_ARGS__)

#define LOG_WARNING(category, format, ...)                                                                                                 \
	foundation::Logger::log(foundation::LogCategory::category, foundation::LogLevel::Warning, __FILE__, __LINE__, format, ##__VA_ARGS__)

#define LOG_ERROR(category, format, ...)                                                                                                   \
	foundation::Logger::log(foundation::LogCategory::category, foundation::LogLevel::Error, __FILE__, __LINE__, format, ##__VA_ARGS__)

// Compile out Debug, Info, and Warning logs in Release builds
#ifndef DEVELOPMENT_BUILD
#undef LOG_DEBUG
#define LOG_DEBUG(category, format, ...) ((void)0)

#undef LOG_INFO
#define LOG_INFO(category, format, ...) ((void)0)

#undef LOG_WARNING
#define LOG_WARNING(category, format, ...) ((void)0)

// Only LOG_ERROR remains in Release builds
#endif
// NOLINTEND(cppcoreguidelines-macro-usage)
