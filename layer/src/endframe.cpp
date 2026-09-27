#include "hooks.h"
#include "session_state.h"
#include "overlay.h"
#include "log.h"
#include <vector>
#include <cmath>
#include <atomic>
#include <cstdio>

// Distance (metres) the head-locked overlay quad sits ahead of the view.
static constexpr float kQuadDistance = 1.0f;

// Latest predictedDisplayPeriod in ns (0 until known), for Auto miss detection.
// Atomic because apps may call xrWaitFrame and xrEndFrame on different threads.
static std::atomic<XrDuration> g_displayPeriod{0};

// Pass-through that records predictedDisplayPeriod.
XRAPI_ATTR XrResult XRAPI_CALL MyWaitFrame(XrSession session, const XrFrameWaitInfo* info,
                                           XrFrameState* state) {
  if (!g_dispatch.waitFrame) return XR_ERROR_FUNCTION_UNSUPPORTED;
  const XrResult res = g_dispatch.waitFrame(session, info, state);
  if (XR_SUCCEEDED(res) && state) {
    g_displayPeriod.store(state->predictedDisplayPeriod, std::memory_order_relaxed);
  }
  return res;
}

// Logs an Auto step change (at most once per second).
static void LogAutoLevel(int fps) {
  char msg[64];
  if (fps == 0) std::snprintf(msg, sizeof(msg), "endFrame: auto refresh -> unlimited");
  else          std::snprintf(msg, sizeof(msg), "endFrame: auto refresh -> %d fps", fps);
  Log(msg);
}

// Fits each eye's overlay quad to that eye's view frustum (angles, position and
// orientation from the runtime), so overlay NDC -1..+1 covers exactly what the eye
// renders: the preview's square. Called until one xrLocateViews succeeds; an
// implausible FOV keeps the fallback quads.
static void ResolveQuadFov(XrSession session, const XrFrameEndInfo* info, SessionState& st) {
  if (st.fovResolved || !g_dispatch.locateViews || st.viewSpace == XR_NULL_HANDLE) return;

  XrViewLocateInfo li{XR_TYPE_VIEW_LOCATE_INFO};
  li.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
  li.displayTime           = info->displayTime;
  li.space                 = st.viewSpace;

  XrViewState vs{XR_TYPE_VIEW_STATE};
  XrView views[2] = {{XR_TYPE_VIEW}, {XR_TYPE_VIEW}};
  uint32_t count = 0;
  if (XR_FAILED(g_dispatch.locateViews(session, &li, &vs, 2, &count, views)) || count < 2) {
    return;  // keep fallback quads; retry next frame
  }
  st.fovResolved = true;

  const bool oriented = (vs.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT) != 0;
  const bool placed   = (vs.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT) != 0;
  QuadPlacement fitted[2];
  bool ok = true;
  for (int i = 0; i < 2; ++i) {   // PRIMARY_STEREO: views[0] left, views[1] right
    const XrPosef& p = views[i].pose;
    const XrFovf& f = views[i].fov;
    EyeView eye{
      oriented ? Quatf{p.orientation.x, p.orientation.y, p.orientation.z, p.orientation.w} : Quatf{0, 0, 0, 1},
      placed ? Vec3f{p.position.x, p.position.y, p.position.z} : Vec3f{0, 0, 0},
      f.angleLeft, f.angleRight, f.angleUp, f.angleDown};
    ok = ok && FitQuadToEye(eye, kQuadDistance, fitted[i]);
  }
  if (ok) { st.eyeQuad[0] = fitted[0]; st.eyeQuad[1] = fitted[1]; }

  // Per-eye FOV in degrees and the quad each eye got.
  char msg[320];
  int len = std::snprintf(msg, sizeof(msg), "endFrame: overlay quads %s",
                          ok ? "fitted to runtime FOV" : "kept at fallback (implausible FOV)");
  const float deg = 57.29578f;
  for (int i = 0; i < 2; ++i) {
    const XrFovf& f = views[i].fov;
    const QuadPlacement& q = st.eyeQuad[i];
    len += std::snprintf(msg + len, sizeof(msg) - len,
                         "; %s up %.1f down %.1f left %.1f right %.1f -> %.2f x %.2f at (%.2f, %.2f, %.2f)",
                         i == 0 ? "L" : "R", f.angleUp * deg, f.angleDown * deg, f.angleLeft * deg,
                         f.angleRight * deg, q.width, q.height, q.position.x, q.position.y, q.position.z);
  }
  Log(msg);
}

// Defined in session.cpp.
extern std::unordered_map<XrSession, SessionState> g_sessions;

// Real frame submission: append our overlay as an extra quad composition layer.
// Fault transparency: on any problem (unknown session, no backend, stale or
// absent SHM, render failure, exception) we submit the app's ORIGINAL frame
// unchanged, so an overlay fault cannot fail the host's frame.
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

    // Size the composition quad to the real FOV (once), so overlay u,v edges
    // land at the true peripheral edge of view rather than an arbitrary ~77deg.
    ResolveQuadFov(session, info, st);

    // Rebuild and redraw only on frames the pacer marks due. A time-critical
    // element appearing or disappearing redraws now, whatever the cap.
    const uint32_t signature = TimeCriticalSignature(st.last);
    const bool due = st.pacer.ShouldRender(
        st.last.refreshMode, info->displayTime,
        g_displayPeriod.load(std::memory_order_relaxed), signature != st.drawnSignature);
    if (st.pacer.TakeStepChanged()) LogAutoLevel(st.pacer.AutoLevelFps());

    if (due) {
      // Reused across frames: BuildOverlay clears the vectors but keeps
      // capacity, so no per-frame heap allocation after warm-up.
      static OverlayGeometry geo;
      BuildOverlay(st.last, geo);
      st.drawnSignature = signature;
      // Only reference the overlay swapchain in a composition layer when the
      // render fully succeeded (image acquired, waited, drawn, released). A
      // partial render failure must NOT submit a broken extended layer: the
      // runtime could reject it and fail the app's xrEndFrame because of us.
      st.overlayReady = !geo.empty() && st.backend->Render(geo);
    }

    // Skipped frames resubmit the last good image. With none, fall through to
    // the pass-through below.
    if (st.overlayReady) {
      // The overlay texture is two eye halves side by side. Submit one quad per
      // eye, each pointing at its half, so left-only / right-only radar blips
      // land in the correct eye while flags + cars-behind (drawn to both halves)
      // appear in both. Both quads share the same head-locked pose/size.
      const int32_t halfW = st.backend->Width() / 2;
      const int32_t h     = st.backend->Height();

      auto makeQuad = [&](XrCompositionLayerQuad& q, XrEyeVisibility eye, int32_t xOffset,
                          const QuadPlacement& place) {
        q.type                      = XR_TYPE_COMPOSITION_LAYER_QUAD;
        q.next                      = nullptr;
        q.layerFlags                = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
        q.space                     = st.viewSpace;
        q.eyeVisibility             = eye;
        q.subImage.swapchain        = st.backend->Swapchain();
        q.subImage.imageRect.offset = {xOffset, 0};
        q.subImage.imageRect.extent = {halfW, h};
        q.subImage.imageArrayIndex  = 0;
        q.pose.orientation          = {place.orientation.x, place.orientation.y,
                                       place.orientation.z, place.orientation.w};
        q.pose.position             = {place.position.x, place.position.y, place.position.z};
        q.size                      = {place.width, place.height};
      };
      static XrCompositionLayerQuad qL{XR_TYPE_COMPOSITION_LAYER_QUAD};
      static XrCompositionLayerQuad qR{XR_TYPE_COMPOSITION_LAYER_QUAD};
      makeQuad(qL, XR_EYE_VISIBILITY_LEFT,  0,     st.eyeQuad[0]);
      makeQuad(qR, XR_EYE_VISIBILITY_RIGHT, halfW, st.eyeQuad[1]);

      // Reused layer-pointer list: app layers first, our two overlay quads after.
      static std::vector<const XrCompositionLayerBaseHeader*> layers;
      layers.clear();
      for (uint32_t i = 0; i < info->layerCount; ++i) layers.push_back(info->layers[i]);
      layers.push_back(reinterpret_cast<const XrCompositionLayerBaseHeader*>(&qL));
      layers.push_back(reinterpret_cast<const XrCompositionLayerBaseHeader*>(&qR));

      XrFrameEndInfo ext = *info;
      ext.layerCount = info->layerCount + 2;
      ext.layers     = layers.data();
      return g_dispatch.endFrame(session, &ext);
    }
  } catch (...) {
    Log("endFrame: overlay path threw -> pass-through");
  }

  return g_dispatch.endFrame(session, info);
}
