#pragma once

// Never throws across the OpenXR boundary; best-effort append to a log file.
void Log(const char* msg) noexcept;
