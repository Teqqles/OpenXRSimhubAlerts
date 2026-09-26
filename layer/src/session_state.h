#pragma once
#include "hooks.h"
#include "render/render_backend.h"
#include "shm_reader.h"
#include "shm_contract.h"
#include "frame_pacer.h"
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
  // Overlay quad half-extents (metres) at kQuadDistance ahead. Sized from the
  // runtime's real per-eye FOV on first successful xrLocateViews so the quad's
  // u,v edges map to the true peripheral edge of view on any headset. Seeded
  // with the pre-FOV fallback (1.6 m quad => 0.8 half-extent, ~77deg coverage).
  float           quadHalfW = 0.8f;
  float           quadHalfH = 0.8f;
  bool            fovResolved = false;
  FramePacer      pacer;  // Config::refreshMode
  // The last due frame drew non-empty geometry, so skipped frames may resubmit
  // its image.
  bool            overlayReady = false;
};

// Defined (non-static) in session.cpp; referenced via extern in endframe.cpp.
extern std::unordered_map<XrSession, SessionState> g_sessions;
