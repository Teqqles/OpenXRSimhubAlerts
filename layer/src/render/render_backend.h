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

// One pre-expanded triangle vertex: NDC position (y up) + straight-alpha colour.
// Layout matches every backend's internal Vertex, so backends memcpy directly.
// All shape geometry (flags, radar car/arrow) is emitted as triangles by
// overlay.cpp -- the single source of truth -- and backends just draw them.
struct OverlayVertex { float x, y; float r, g, b, a; };

// Per-eye triangle lists. The overlay is stereo: cars behind the driver and
// flags go to BOTH eyes; cars to the left appear only in the left eye and cars
// to the right only in the right eye (matching where they sit in the driver's
// vision). Backends render `leftEye` into the left half of the swapchain and
// `rightEye` into the right half; endframe submits one eye-visibility quad per
// half. Both lists are TRIANGLELIST (vertex count is a multiple of 3).
struct OverlayGeometry {
  std::vector<OverlayVertex> leftEye;
  std::vector<OverlayVertex> rightEye;
  bool empty() const { return leftEye.empty() && rightEye.empty(); }
};

// A graphics-API-specific overlay renderer. All methods are called from inside
// the layer's xrEndFrame hook, which wraps them in try/catch and falls back to
// pass-through on any failure, so implementations may return/fail freely.
struct IRenderBackend {
  virtual bool Init(XrSession, const void* graphicsBinding, XrInstance) = 0;
  virtual XrSwapchain Swapchain() const = 0;
  virtual int32_t Width() const = 0;   // full texture width (both eye halves)
  virtual int32_t Height() const = 0;
  // Returns true only when the overlay image was acquired, waited, drawn and
  // released successfully. On false the caller MUST NOT reference the swapchain
  // in a composition layer -- see endframe.cpp fault-transparency contract.
  virtual bool Render(const OverlayGeometry&) = 0;
  virtual void Release() = 0;
  virtual ~IRenderBackend() {}
};

IRenderBackend* CreateD3D11Backend();
IRenderBackend* CreateD3D12Backend();
IRenderBackend* CreateVulkanBackend();
