#pragma once
#define XR_USE_GRAPHICS_API_D3D11
#define XR_USE_GRAPHICS_API_D3D12
#define XR_USE_GRAPHICS_API_VULKAN

// openxr_platform.h emits graphics-API structs that reference the native
// D3D/Vulkan types, so those platform headers must be visible first.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <d3d12.h>
#include <vulkan/vulkan.h>

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <openxr/openxr_loader_negotiation.h>

struct Dispatch {
  PFN_xrGetInstanceProcAddr getInstanceProcAddr = nullptr;
  PFN_xrCreateSession       createSession       = nullptr;
  PFN_xrDestroySession      destroySession      = nullptr;
  PFN_xrEndFrame            endFrame            = nullptr;
  PFN_xrDestroySpace        destroySpace        = nullptr;
  PFN_xrLocateViews         locateViews         = nullptr;
};

extern Dispatch g_dispatch;

XRAPI_ATTR XrResult XRAPI_CALL MyCreateSession(XrInstance, const XrSessionCreateInfo*, XrSession*);
XRAPI_ATTR XrResult XRAPI_CALL MyDestroySession(XrSession);
XRAPI_ATTR XrResult XRAPI_CALL MyEndFrame(XrSession, const XrFrameEndInfo*);
