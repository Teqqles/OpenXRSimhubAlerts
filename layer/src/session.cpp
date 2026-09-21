#include "hooks.h"
#include "session_state.h"
#include "render/render_backend.h"
#include "log.h"

// Owning table of per-session overlay state. Non-static so endframe.cpp can
// reach it via the extern declaration in session_state.h.
std::unordered_map<XrSession, SessionState> g_sessions;

// Real session creation: chain down first, then (best-effort) stand up the
// overlay renderer for the app's graphics API. Any failure here just disables
// the overlay for this session; it never changes the result handed back to the
// app, and never throws across the boundary.
XRAPI_ATTR XrResult XRAPI_CALL MyCreateSession(XrInstance instance, const XrSessionCreateInfo* info, XrSession* out) {
  if (!g_dispatch.createSession) return XR_ERROR_FUNCTION_UNSUPPORTED;
  XrResult r = g_dispatch.createSession(instance, info, out);
  if (XR_FAILED(r) || !out || !info) return r;

  try {
    // Walk the next-chain for a supported graphics binding.
    IRenderBackend* backend = nullptr;
    const void*     binding = nullptr;
    for (const XrBaseInStructure* p = static_cast<const XrBaseInStructure*>(info->next);
         p != nullptr; p = p->next) {
      if (p->type == XR_TYPE_GRAPHICS_BINDING_D3D11_KHR)  { backend = CreateD3D11Backend();  binding = p; break; }
      if (p->type == XR_TYPE_GRAPHICS_BINDING_D3D12_KHR)  { backend = CreateD3D12Backend();  binding = p; break; }
      if (p->type == XR_TYPE_GRAPHICS_BINDING_VULKAN_KHR) { backend = CreateVulkanBackend(); binding = p; break; }
    }

    // Insert (default-constructed) and fill in place: SessionState owns a
    // ShmReader whose handle must not be copied/double-closed, so we never
    // copy the state into the map.
    SessionState& st = g_sessions[*out];

    if (backend && backend->Init(*out, binding, instance)) {
      st.backend   = backend;
      st.swapchain = backend->Swapchain();
      Log("session: overlay backend active");
    } else {
      if (backend) { backend->Release(); delete backend; }
      Log("session: overlay disabled (no/failed backend) -> pass-through");
    }

    // Head-locked reference space for the quad layer.
    PFN_xrCreateReferenceSpace pfnCreateSpace = nullptr;
    g_dispatch.getInstanceProcAddr(instance, "xrCreateReferenceSpace",
                                   reinterpret_cast<PFN_xrVoidFunction*>(&pfnCreateSpace));
    if (pfnCreateSpace) {
      XrReferenceSpaceCreateInfo ci{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
      ci.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
      ci.poseInReferenceSpace.orientation = {0.0f, 0.0f, 0.0f, 1.0f};
      ci.poseInReferenceSpace.position    = {0.0f, 0.0f, 0.0f};
      if (XR_FAILED(pfnCreateSpace(*out, &ci, &st.viewSpace))) {
        st.viewSpace = XR_NULL_HANDLE;
        Log("session: xrCreateReferenceSpace failed -> overlay pass-through");
      }
    }

    st.shm.Open();   // best-effort; endFrame tolerates a closed/absent SHM
  } catch (...) {
    Log("session: overlay setup threw -> pass-through");
  }

  return r;
}

// Session teardown: release our per-session overlay resources while the session
// handle is still valid, then chain down. Fault-transparent: any failure in our
// cleanup must not stop the app's session from being destroyed.
XRAPI_ATTR XrResult XRAPI_CALL MyDestroySession(XrSession session) {
  try {
    auto it = g_sessions.find(session);
    if (it != g_sessions.end()) {
      SessionState& st = it->second;
      // Destroy the view space and backend (swapchain/GPU resources) before the
      // session is torn down, in the reverse order they were created.
      if (st.viewSpace != XR_NULL_HANDLE && g_dispatch.destroySpace) {
        g_dispatch.destroySpace(st.viewSpace);
        st.viewSpace = XR_NULL_HANDLE;
      }
      if (st.backend) {
        st.backend->Release();
        delete st.backend;
        st.backend = nullptr;
      }
      g_sessions.erase(it);   // ShmReader dtor closes its handle/mapping
    }
  } catch (...) {
    Log("destroySession: overlay teardown threw -> chaining anyway");
  }
  if (!g_dispatch.destroySession) return XR_ERROR_FUNCTION_UNSUPPORTED;
  return g_dispatch.destroySession(session);
}
