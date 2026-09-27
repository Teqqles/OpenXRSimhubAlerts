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
// overlay.cpp is the single source of shape geometry: it emits flags and radar
// cars/arrows as triangles, and the backends draw them unchanged.
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

// ---- Shared overlay render constants and helpers ----
// API-neutral (no graphics headers), so they live here rather than being
// redefined in every backend. Change them in ONE place.

// Overlay resolution. The swapchain texture is two eye halves side by side:
// kEyeDim x kEyeDim each (left eye -> left half, right eye -> right half), so
// the full texture is kEyeDim*2 wide. Supersampled: sharper edges when the
// runtime bilinear-filters the composited quad. See endframe.cpp.
constexpr int32_t kEyeDim = 1024;

// Vertex-buffer cap: both eye triangle lists concatenated. Sized for a full
// element list of feathered ellipses (the costliest shape, 216 vertices) in both
// eyes, so the clamp never drops geometry. About 1.5 MB.
// Backends clamp emitted geometry to this via ClampEyeCounts().
constexpr uint32_t kMaxVerts = 65536;

// Transparent clear for the overlay target (straight-alpha RGBA).
constexpr float kOverlayClearColor[4] = {0.0f, 0.0f, 0.0f, 0.0f};

// Clamp the per-eye vertex counts so the concatenated left+right lists fit in a
// kMaxVerts buffer: the left eye is capped first, then the right takes whatever
// slack remains. Backends memcpy nL/nR vertices into their vertex buffer.
inline void ClampEyeCounts(const OverlayGeometry& geo, uint32_t& nL, uint32_t& nR) {
  nL = static_cast<uint32_t>(geo.leftEye.size());
  nR = static_cast<uint32_t>(geo.rightEye.size());
  if (nL > kMaxVerts) nL = kMaxVerts;
  if (nL + nR > kMaxVerts) nR = kMaxVerts - nL;
}

// From the runtime's advertised swapchain formats, pick `preferred`, else
// `fallback`, else the first listed. Callers guarantee `formats` is non-empty
// (they bail earlier on a zero count), so the -1 branch never fires in practice.
inline int64_t PickSwapchainFormat(const std::vector<int64_t>& formats,
                                   int64_t preferred, int64_t fallback) {
  for (int64_t f : formats) if (f == preferred) return f;
  for (int64_t f : formats) if (f == fallback)  return f;
  return formats.empty() ? -1 : formats[0];
}

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
  // in a composition layer (see the fault-transparency contract in endframe.cpp).
  virtual bool Render(const OverlayGeometry&) = 0;
  virtual void Release() = 0;
  virtual ~IRenderBackend() {}
};

IRenderBackend* CreateD3D11Backend();
IRenderBackend* CreateD3D12Backend();
IRenderBackend* CreateVulkanBackend();
