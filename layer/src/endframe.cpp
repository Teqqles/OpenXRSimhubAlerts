#include "hooks.h"
#include "session_state.h"
#include "overlay.h"
#include "log.h"
#include <vector>

// Defined in session.cpp.
extern std::unordered_map<XrSession, SessionState> g_sessions;

// Real frame submission: append our overlay as an extra quad composition layer.
// FAULT TRANSPARENCY is the rule here -- on absolutely any problem (unknown
// session, no backend, stale/absent SHM, render failure, exception) we submit
// the app's ORIGINAL frame unchanged. The overlay never blocks or crashes the
// host.
XrResult MyEndFrame(XrSession session, const XrFrameEndInfo* info) {
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
      q.pose.position           = {0.0f, 0.0f, -1.0f};  // 1m in front of the view
      q.size                    = {1.6f, 1.6f};

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
