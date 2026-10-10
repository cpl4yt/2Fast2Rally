#pragma once
#include <string>

namespace rally::log {
	// Opens logs/rally.log (truncated each run) and routes raylib's TraceLog
	// output into it as well as stderr.
	void init(const std::string& path);
	void shutdown();

	void info(const char* fmt, ...);
	void warn(const char* fmt, ...);
	void error(const char* fmt, ...);

	// Path of the current log file (for the crash handler to point at).
	const std::string& path();
	// Last ~40 lines, kept in memory so a crash report can include them.
	std::string recentLines();
}
