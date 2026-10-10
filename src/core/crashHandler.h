#pragma once

namespace rally::crash {
	// Installs handlers for unhandled SEH exceptions (Windows), std::terminate
	// and fatal signals. On a crash they write logs/crash-<time>.txt with the
	// recent log lines and (on Windows) a stack trace, then exit.
	void install();
}
