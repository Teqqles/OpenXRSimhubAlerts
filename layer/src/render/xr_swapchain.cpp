#include "xr_swapchain.h"
#include "../log.h"
#include <string>

void XrOverlaySwapchain::LogTagged(const char* tag, const char* msg) {
  Log((std::string(tag) + msg).c_str());
}

bool XrOverlaySwapchain::Create(XrSession session, XrInstance instance,
                                int64_t preferredFormat, int64_t fallbackFormat,
                                const char* tag, int64_t& chosenFormat) {
  PFN_xrGetInstanceProcAddr gipa = g_dispatch.getInstanceProcAddr;
  if (!gipa) { LogTagged(tag, "no getInstanceProcAddr"); return false; }

  PFN_xrCreateSwapchain           pfnCreate  = nullptr;
  PFN_xrEnumerateSwapchainFormats pfnEnumFmt = nullptr;
  gipa(instance, "xrCreateSwapchain",           reinterpret_cast<PFN_xrVoidFunction*>(&pfnCreate));
  gipa(instance, "xrEnumerateSwapchainImages",  reinterpret_cast<PFN_xrVoidFunction*>(&_enumImg));
  gipa(instance, "xrEnumerateSwapchainFormats", reinterpret_cast<PFN_xrVoidFunction*>(&pfnEnumFmt));
  gipa(instance, "xrAcquireSwapchainImage",     reinterpret_cast<PFN_xrVoidFunction*>(&_acquire));
  gipa(instance, "xrWaitSwapchainImage",        reinterpret_cast<PFN_xrVoidFunction*>(&_wait));
  gipa(instance, "xrReleaseSwapchainImage",     reinterpret_cast<PFN_xrVoidFunction*>(&_release));
  if (!pfnCreate || !_enumImg || !pfnEnumFmt || !_acquire || !_wait || !_release) {
    LogTagged(tag, "swapchain entry points unresolved");
    return false;
  }

  // Pick a runtime-supported colour format, preferring `preferredFormat`, then
  // `fallbackFormat`, else whatever the runtime lists first.
  uint32_t fmtCount = 0;
  if (XR_FAILED(pfnEnumFmt(session, 0, &fmtCount, nullptr)) || fmtCount == 0) {
    LogTagged(tag, "no swapchain formats");
    return false;
  }
  std::vector<int64_t> formats(fmtCount);
  if (XR_FAILED(pfnEnumFmt(session, fmtCount, &fmtCount, formats.data()))) {
    LogTagged(tag, "enumerate formats failed");
    return false;
  }
  chosenFormat = PickSwapchainFormat(formats, preferredFormat, fallbackFormat);

  // The texture is two eye halves side by side: kEyeDim x kEyeDim each (left eye
  // left half, right eye right half), so kEyeDim*2 wide. See endframe.cpp.
  XrSwapchainCreateInfo sci{XR_TYPE_SWAPCHAIN_CREATE_INFO};
  sci.usageFlags  = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
  sci.format      = chosenFormat;
  sci.sampleCount = 1;
  sci.width       = kEyeDim * 2;
  sci.height      = kEyeDim;
  sci.faceCount   = 1;
  sci.arraySize   = 1;
  sci.mipCount    = 1;
  if (XR_FAILED(pfnCreate(session, &sci, &_swapchain)) || _swapchain == XR_NULL_HANDLE) {
    LogTagged(tag, "xrCreateSwapchain failed");
    return false;
  }
  _width  = kEyeDim * 2;
  _height = kEyeDim;
  return true;
}

bool XrOverlaySwapchain::AcquireWait(uint32_t& index) const {
  index = 0;
  XrSwapchainImageAcquireInfo ai{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
  if (XR_FAILED(_acquire(_swapchain, &ai, &index))) return false;

  XrSwapchainImageWaitInfo wi{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
  wi.timeout = XR_INFINITE_DURATION;
  if (XR_FAILED(_wait(_swapchain, &wi))) {
    XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    _release(_swapchain, &ri);   // release what we acquired
    return false;
  }
  return true;
}

bool XrOverlaySwapchain::Release() const {
  XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
  return XR_SUCCEEDED(_release(_swapchain, &ri));
}
