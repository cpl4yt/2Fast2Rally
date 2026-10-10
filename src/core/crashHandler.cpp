// Kept in its own translation unit: <windows.h> collides with raylib names
// (CloseWindow, DrawText, Rectangle...), so raylib is never included here.
#include "core/crashHandler.h"
#include "core/log.h"
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbghelp.h>
#endif

namespace rally::crash {
	namespace {
		void writeReport(const char* reason, const std::string& trace) {
			char name[96];
			std::time_t t = std::time(nullptr);
			std::strftime(name, sizeof name, "logs/crash-%Y%m%d-%H%M%S.txt", std::localtime(&t));
			FILE* f = std::fopen(name, "w");
			if (f) {
				std::fprintf(f, "2Fast2Rally %s crashed: %s\n\n", RALLY_VERSION, reason);
				if (!trace.empty()) std::fprintf(f, "Stack:\n%s\n", trace.c_str());
				std::fprintf(f, "Recent log:\n%s\n", log::recentLines().c_str());
				std::fclose(f);
			}
			log::error("CRASH: %s (report: %s)", reason, name);
			log::shutdown();
		}

#ifdef _WIN32
		std::string stackTrace(CONTEXT* ctx) {
			std::string out;
			HANDLE process = GetCurrentProcess();
			HANDLE thread = GetCurrentThread();
			SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);
			SymInitialize(process, nullptr, TRUE);
			STACKFRAME64 frame{};
			frame.AddrPC.Offset = ctx->Rip;     frame.AddrPC.Mode = AddrModeFlat;
			frame.AddrFrame.Offset = ctx->Rbp;  frame.AddrFrame.Mode = AddrModeFlat;
			frame.AddrStack.Offset = ctx->Rsp;  frame.AddrStack.Mode = AddrModeFlat;
			for (int i = 0; i < 48; ++i) {
				if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, thread, &frame, ctx, nullptr,
					SymFunctionTableAccess64, SymGetModuleBase64, nullptr)) break;
				if (frame.AddrPC.Offset == 0) break;
				char buf[sizeof(SYMBOL_INFO) + 256]{};
				SYMBOL_INFO* sym = reinterpret_cast<SYMBOL_INFO*>(buf);
				sym->SizeOfStruct = sizeof(SYMBOL_INFO);
				sym->MaxNameLen = 255;
				DWORD64 disp = 0;
				char line[512];
				if (SymFromAddr(process, frame.AddrPC.Offset, &disp, sym))
					std::snprintf(line, sizeof line, "  %2d  %s+0x%llx\n", i, sym->Name, (unsigned long long)disp);
				else
					std::snprintf(line, sizeof line, "  %2d  0x%llx\n", i, (unsigned long long)frame.AddrPC.Offset);
				out += line;
			}
			SymCleanup(process);
			return out;
		}

		LONG WINAPI sehFilter(EXCEPTION_POINTERS* info) {
			char reason[128];
			std::snprintf(reason, sizeof reason, "unhandled exception 0x%08lx at %p",
				info->ExceptionRecord->ExceptionCode, info->ExceptionRecord->ExceptionAddress);
			writeReport(reason, stackTrace(info->ContextRecord));
			return EXCEPTION_EXECUTE_HANDLER;
		}
#endif

		void onTerminate() {
			std::string reason = "std::terminate";
			if (auto ep = std::current_exception()) {
				try { std::rethrow_exception(ep); }
				catch (const std::exception& e) { reason += std::string(": ") + e.what(); }
				catch (...) { reason += ": unknown exception"; }
			}
			writeReport(reason.c_str(), "");
			std::_Exit(1);
		}

		void onSignal(int sig) {
			char reason[64];
			std::snprintf(reason, sizeof reason, "signal %d", sig);
			writeReport(reason, "");
			std::_Exit(1);
		}
	}

	void install() {
#ifdef _WIN32
		SetUnhandledExceptionFilter(sehFilter);
#endif
		std::set_terminate(onTerminate);
		std::signal(SIGABRT, onSignal);
		std::signal(SIGFPE, onSignal);
	}
}
