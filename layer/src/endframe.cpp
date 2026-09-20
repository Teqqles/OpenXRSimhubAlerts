#include "hooks.h"
#include "session_state.h"
#include "overlay.h"
#include "log.h"
#include <vector>
#include <cmath>

// Distance (metres) the head-locked overlay quad sits ahead of the view.
static constexpr float kQuadDistance = 1.0f;

// Resolve the overlay quad's half-extents from the runtime's real per-eye FOV so
// the quad spans the full field of view (u,v edges == peripheral edge). Called
// once per session; on any failure the seeded fallback extents are kept.
static void ResolveQuadFov(XrSession session, const XrFrameEndInfo* info, SessionState& st) {
  if (st.fovResolved || !g_dispatch.locateViews || st.viewSpace == XR_NULL_HANDLE) return;

  XrViewLocateInfo li{XR_TYPE_VIEW_LOCATE_INFO};
  li.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
  li.displayTime           = info->displayTime;
  li.space                 = st.viewSpace;

  XrViewState vs{XR_TYPE_VIEW_STATE};
  XrView views[2] = {{XR_TYPE_VIEW}, {XR_TYPE_VIEW}};
  uint32_t count = 0;
  if (XR_FAILED(g_dispatch.locateViews(session, &li, &vs, 2, &count, views)) || count == 0) {
    return;  // keep fallback extents; retry next frame
  }

  float maxTanX = 0.0f, maxTanY = 0.0f;
  for (uint32_t i = 0; i < count && i < 2; ++i) {
    const XrFovf& f = views[i].fov;
    maxTanX = std::max({maxTanX, std::fabs(std::tan(f.angleRight)), std::fabs(std::tan(f.angleLeft))});
    maxTanY = std::max({maxTanY, std::fabs(std::tan(f.angleUp)),    std::fabs(std::tan(f.angleDown))});
  }
  // Clamp against absurd/garbage FOV so a bad runtime can't produce a giant quad.
  auto clamp = [](float v) { return v < 0.1f ? 0.1f : (v > 5.0f ? 5.0f : v); };
  st.quadHalfW  = clamp(kQuadDistance * maxTanX);
  st.quadHalfH  = clamp(kQuadDistance * maxTanY);
  st.fovResolved = true;
  Log("endFrame: overlay quad sized to runtime FOV");
}

// Defined in session.cpp.
extern std::unordered_map<XrSession, SessionState> g_sessions;

// Real frame submission: append our overlay as an extra quad composition layer.
// FAULT TRANSPARENCY is the rule here -- on absolutely any problem (unknown
// session, no backend, stale/absent SHM, render failure, exception) we submit
// the app's ORIGINAL frame unchanged. The overlay never blocks or crashes the
// host.
XRAPI_ATTR XrResult XRAPI_CALL MyEndFrame(XrSession session, const XrFrameEndInfo* info) {
  if (!g_dispatch.endFrame) return XR_ERROR_FUNCTION_UNSUPPORTED;
  if (!info) return g_dispatch.endFrame(session, info);

  auto it = g_sessions.find(session);
  if (it == g_sessions.end()) return g_dispatch.endFrame(session, info);

  SessionState& st = it->second;
  if (!st.backend || st.viewSpace == XR_NULL_HANDLE) {
    return g_dispatch.endFrame(session, info);
  }

  try {
    // Refresh from SHM; on read failure keep the last good block. Only proceed
    // if we ever got a valid, version-matched block.
    DataBlock tmp;
    if (st.shm.Read(tmp) && tmp.version == SHM_VERSION) {
      st.last = tmp;
    }
    if (st.last.version != SHM_VERSION) {
      return g_dispatch.endFrame(session, info);
    }

    // Reused across frames: BuildOverlay clears but keeps capacity, so no
    // per-frame heap allocation after warm-up.
    static std::vector<OverlayQuad> quads;
    if (quads.capacity() < static_cast<size_t>(MAX_CARS + 2)) {
      quads.reserve(MAX_CARS + 2);
    }
    BuildOverlay(st.last, quads);

    // Size the composition quad to the real FOV (once), so overlay u,v edges
    // land at the true peripheral edge of view rather than an arbitrary ~77deg.
    ResolveQuadFov(session, info, st);

    // Only reference the overlay swapchain in a composition layer when the
    // render fully succeeded (image acquired, waited, drawn, released). A
    // partial render failure must NOT submit a broken extended layer -- the
    // runtime could reject it and fail the app's xrEndFrame purely due to us.
    // On failure we fall through to the untouched pass-through below.
    if (!quads.empty() && st.backend->Render(quads)) {
      static XrCompositionLayerQuad q{XR_TYPE_COMPOSITION_LAYER_QUAD};
      q.next                    = nullptr;
      q.layerFlags              = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
      q.space                   = st.viewSpace;
      q.eyeVisibility           = XR_EYE_VISIBILITY_BOTH;
      q.subImage.swapchain      = st.backend->Swapchain();
      q.subImage.imageRect.offset = {0, 0};
      q.subImage.imageRect.extent = {st.backend->Width(), st.backend->Height()};
      q.subImage.imageArrayIndex  = 0;
      q.pose.orientation        = {0.0f, 0.0f, 0.0f, 1.0f};
      q.pose.position           = {0.0f, 0.0f, -kQuadDistance};  // ahead of the view
      q.size                    = {2.0f * st.quadHalfW, 2.0f * st.quadHalfH};

      // Reused layer-pointer list: app layers first, our overlay appended.
      static std::vector<const XrCompositionLayerBaseHeader*> layers;
      layers.clear();
      for (uint32_t i = 0; i < info->layerCount; ++i) layers.push_back(info->layers[i]);
      layers.push_back(reinterpret_cast<const XrCompositionLayerBaseHeader*>(&q));

      XrFrameEndInfo ext = *info;
      ext.layerCount = info->layerCount + 1;
      ext.layers     = layers.data();
      return g_dispatch.endFrame(session, &ext);
    }
  } catch (...) {
    Log("endFrame: overlay path threw -> pass-through");
  }

  return g_dispatch.endFrame(session, info);
}
