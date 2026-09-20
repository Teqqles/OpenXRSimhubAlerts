#pragma once
#include "hooks.h"
#include "render/render_backend.h"
#include "shm_reader.h"
#include "shm_contract.h"
#include <unordered_map>

// Per-session overlay state. Owned by g_sessions (defined in session.cpp) and
// consumed by the xrEndFrame hook (endframe.cpp). Kept in a shared header so
// both translation units agree on the layout (Highlander: one definition).
struct SessionState {
  IRenderBackend* backend   = nullptr;
  XrSwapchain     swapchain = XR_NULL_HANDLE;
  XrSpace         viewSpace = XR_NULL_HANDLE;
  ShmReader       shm;
  DataBlock       last{};
};

// Defined (non-static) in session.cpp; referenced via extern in endframe.cpp.
extern std::unordered_map<XrSession, SessionState> g_sessions;
