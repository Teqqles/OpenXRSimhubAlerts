#include "log.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <string>

// Best-effort logger. Any failure is swallowed: this must never throw or
// propagate an error across the OpenXR boundary.
void Log(const char* msg) noexcept {
  try {
    if (!msg) return;

    // Expand %LOCALAPPDATA% into a real path before touching the filesystem.
    // ExpandEnvironmentStringsA returns the required size (including the NUL).
    char expanded[MAX_PATH * 2] = {};
    DWORD n = ExpandEnvironmentStringsA(
        "%LOCALAPPDATA%\\OpenXRSimHubAlerts", expanded, sizeof(expanded));
    std::string dir;
    if (n != 0 && n <= sizeof(expanded)) {
      dir.assign(expanded);
    } else {
      // Fall back to the raw environment variable if expansion failed.
      const char* la = std::getenv("LOCALAPPDATA");
      if (!la) return;
      dir = std::string(la) + "\\OpenXRSimHubAlerts";
    }

    // CreateDirectoryA on the expanded path (ignore "already exists").
    CreateDirectoryA(dir.c_str(), nullptr);

    std::string path = dir + "\\layer.log";
    FILE* f = std::fopen(path.c_str(), "a");
    if (!f) return;
    std::fputs(msg, f);
    std::fputc('\n', f);
    std::fclose(f);
  } catch (...) {
    // Swallow everything.
  }
}
