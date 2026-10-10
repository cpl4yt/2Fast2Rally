#include "core/log.h"
#include <raylib.h>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <deque>
#include <mutex>
#include <filesystem>

namespace rally::log {
	namespace {
		FILE* file = nullptr;
		std::string filePath;
		std::deque<std::string> recent;
		std::mutex mtx;

		void write(const char* level, const char* fmt, va_list args) {
			char msg[2048];
			vsnprintf(msg, sizeof msg, fmt, args);
			char stamp[32];
			std::time_t t = std::time(nullptr);
			std::strftime(stamp, sizeof stamp, "%H:%M:%S", std::localtime(&t));
			char line[2200];
			snprintf(line, sizeof line, "[%s] %-5s %s", stamp, level, msg);

			std::lock_guard<std::mutex> lock(mtx);
			std::fprintf(stderr, "%s\n", line);
			if (file) {
				std::fprintf(file, "%s\n", line);
				std::fflush(file);
			}
			recent.emplace_back(line);
			while (recent.size() > 40) recent.pop_front();
		}

		void raylibCallback(int logLevel, const char* text, va_list args) {
			const char* level = "INFO";
			if (logLevel >= LOG_ERROR) level = "ERROR";
			else if (logLevel == LOG_WARNING) level = "WARN";
			else if (logLevel < LOG_INFO) return; // drop debug/trace spam
			write(level, text, args);
		}
	}

	void init(const std::string& path) {
		filePath = path;
		std::error_code ec;
		std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
		file = std::fopen(path.c_str(), "w");
		SetTraceLogCallback(raylibCallback);
	}

	void shutdown() {
		std::lock_guard<std::mutex> lock(mtx);
		if (file) std::fclose(file);
		file = nullptr;
	}

	void info(const char* fmt, ...) { va_list a; va_start(a, fmt); write("INFO", fmt, a); va_end(a); }
	void warn(const char* fmt, ...) { va_list a; va_start(a, fmt); write("WARN", fmt, a); va_end(a); }
	void error(const char* fmt, ...) { va_list a; va_start(a, fmt); write("ERROR", fmt, a); va_end(a); }

	const std::string& path() { return filePath; }

	std::string recentLines() {
		std::lock_guard<std::mutex> lock(mtx);
		std::string out;
		for (auto& l : recent) { out += l; out += '\n'; }
		return out;
	}
}
