#pragma once
#include <cstdint>
#include <vector>

// OpenXR handle forward declarations (x64: handles are pointers to opaque
// structs, matching XR_DEFINE_HANDLE). Declaring them here rather than pulling
// in hooks.h keeps the heavy graphics-platform headers (d3d11/d3d12/vulkan) out
// of overlay.h and its consumers (e.g. the unit tests), which only need
// OverlayQuad. Backends that touch graphics types include hooks.h themselves.
typedef struct XrInstance_T*  XrInstance;
typedef struct XrSession_T*   XrSession;
typedef struct XrSwapchain_T* XrSwapchain;

struct OverlayQuad { float u, v, w, h; uint32_t rgba; uint8_t shape; };

// A graphics-API-specific overlay renderer. All methods are called from inside
// the layer's xrEndFrame hook, which wraps them in try/catch and falls back to
// pass-through on any failure, so implementations may return/fail freely.
struct IRenderBackend {
  virtual bool Init(XrSession, const void* graphicsBinding, XrInstance) = 0;
  virtual XrSwapchain Swapchain() const = 0;
  virtual int32_t Width() const = 0;
  virtual int32_t Height() const = 0;
  virtual void Render(const std::vector<OverlayQuad>&) = 0;
  virtual void Release() = 0;
  virtual ~IRenderBackend() {}
};

IRenderBackend* CreateD3D11Backend();
IRenderBackend* CreateD3D12Backend();   // Task 11 (stub returns nullptr for now)
IRenderBackend* CreateVulkanBackend();  // Task 12 (stub returns nullptr for now)
