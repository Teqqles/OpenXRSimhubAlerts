#pragma once
#include "hooks.h"
#include "render/render_backend.h"
#include "shm_reader.h"
#include "shm_contract.h"
#include "frame_pacer.h"
#include "quad_fit.h"
#include "overlay.h"
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
  // Each eye's overlay quad in view space, fitted to that eye's FOV on the first
  // successful xrLocateViews. Seeded with a centred 1.6 m quad (~77 degrees).
  QuadPlacement   eyeQuad[2] = {
    {{0, 0, 0, 1}, {0, 0, -1.0f}, 1.6f, 1.6f},
    {{0, 0, 0, 1}, {0, 0, -1.0f}, 1.6f, 1.6f},
  };
  bool            fovResolved = false;
  EyeAnchors      anchors;   // straight ahead per eye, set with the fitted quads
  FramePacer      pacer;  // DataBlock::refreshMode
  // The last due frame drew non-empty geometry, so skipped frames may resubmit
  // its image.
  bool            overlayReady = false;
  uint32_t        drawnSignature = 0;  // TimeCriticalSignature of the last drawn frame
};

// Defined (non-static) in session.cpp; referenced via extern in endframe.cpp.
extern std::unordered_map<XrSession, SessionState> g_sessions;
