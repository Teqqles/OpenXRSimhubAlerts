#pragma once
#include "../hooks.h"        // OpenXR types, PFN_*, g_dispatch
#include "render_backend.h"  // kEyeDim, PickSwapchainFormat
#include <vector>

// Shared OpenXR swapchain plumbing for the overlay backends. One copy of the
// lifecycle that D3D11, D3D12 and Vulkan all drive identically: resolve the
// swapchain entry points, create the kEyeDim*2 x kEyeDim side-by-side stereo
// swapchain, enumerate its images, and acquire/wait/release one image per frame.
//
// The ONLY per-backend variation is the graphics-API image struct type
// (XrSwapchainImage{D3D11,D3D12,Vulkan}KHR), handled by the templated
// EnumerateImages(). The runtime owns the XrSwapchain (destroyed with the
// session), so this holds a borrowed handle and never destroys it. Call
// forget() during teardown to drop it.
class XrOverlaySwapchain {
public:
  // Resolve the swapchain entry points, pick a colour format (preferred, then
  // fallback, then whatever the runtime lists first) and create the swapchain.
  // On success sets chosenFormat and returns true. `logTag` prefixes any log
  // line (e.g. "d3d11: ").
  bool Create(XrSession session, XrInstance instance,
              int64_t preferredFormat, int64_t fallbackFormat,
              const char* logTag, int64_t& chosenFormat);

  // Enumerate the swapchain images into `out`. The caller supplies the
  // API-specific image struct type and its sType (e.g. XrSwapchainImageD3D11KHR
  // + XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR). Returns false on any failure.
  template <class ImgT>
  bool EnumerateImages(XrStructureType imageType, const char* logTag,
                       std::vector<ImgT>& out) const {
    if (!_enumImg || _swapchain == XR_NULL_HANDLE) return false;
    uint32_t count = 0;
    if (XR_FAILED(_enumImg(_swapchain, 0, &count, nullptr)) || count == 0) {
      LogTagged(logTag, "no swapchain images");
      return false;
    }
    out.assign(count, ImgT{imageType});
    if (XR_FAILED(_enumImg(_swapchain, count, &count,
                           reinterpret_cast<XrSwapchainImageBaseHeader*>(out.data())))) {
      LogTagged(logTag, "enumerate images failed");
      return false;
    }
    return true;
  }

  // Acquire + wait the next image. On success `index` is set and the image is
  // ready to render into; the caller MUST later call Release(). On failure
  // returns false with any acquired image already released.
  bool AcquireWait(uint32_t& index) const;

  // Release the current image back to the runtime.
  bool Release() const;

  XrSwapchain handle() const { return _swapchain; }
  int32_t     width() const  { return _width; }
  int32_t     height() const { return _height; }
  void        forget()       { _swapchain = XR_NULL_HANDLE; }  // runtime owns it

private:
  static void LogTagged(const char* tag, const char* msg);

  XrSwapchain _swapchain = XR_NULL_HANDLE;
  int32_t     _width  = 0;
  int32_t     _height = 0;

  PFN_xrEnumerateSwapchainImages _enumImg = nullptr;
  PFN_xrAcquireSwapchainImage    _acquire = nullptr;
  PFN_xrWaitSwapchainImage       _wait    = nullptr;
  PFN_xrReleaseSwapchainImage    _release = nullptr;
};
