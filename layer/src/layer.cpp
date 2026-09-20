#include "hooks.h"
#include "log.h"

#include <cstring>

Dispatch g_dispatch;

// Forward declarations of our own layer entry points.
static XRAPI_ATTR XrResult XRAPI_CALL MyGetInstanceProcAddr(
    XrInstance instance, const char* name, PFN_xrVoidFunction* function);

// Route the calls we care about through our hooks; everything else is delegated
// down the chain unchanged.
static XRAPI_ATTR XrResult XRAPI_CALL MyGetInstanceProcAddr(
    XrInstance instance, const char* name, PFN_xrVoidFunction* function) {
  if (name && function) {
    if (std::strcmp(name, "xrCreateSession") == 0) {
      *function = reinterpret_cast<PFN_xrVoidFunction>(MyCreateSession);
      return XR_SUCCESS;
    }
    if (std::strcmp(name, "xrEndFrame") == 0) {
      *function = reinterpret_cast<PFN_xrVoidFunction>(MyEndFrame);
      return XR_SUCCESS;
    }
  }
  if (!g_dispatch.getInstanceProcAddr) {
    if (function) *function = nullptr;
    return XR_ERROR_HANDLE_INVALID;
  }
  return g_dispatch.getInstanceProcAddr(instance, name, function);
}

// Layer creation: chain to the next layer/loader, then resolve the functions we
// dispatch to. All overlay behaviour is guarded elsewhere; here we only wire the
// table and never throw.
static XRAPI_ATTR XrResult XRAPI_CALL MyCreateApiLayerInstance(
    const XrInstanceCreateInfo* info,
    const XrApiLayerCreateInfo* apiLayerInfo,
    XrInstance* instance) {
  if (!apiLayerInfo || !apiLayerInfo->nextInfo) {
    Log("CreateApiLayerInstance: missing nextInfo");
    return XR_ERROR_INITIALIZATION_FAILED;
  }

  XrApiLayerNextInfo* nextInfo = apiLayerInfo->nextInfo;
  g_dispatch.getInstanceProcAddr = nextInfo->nextGetInstanceProcAddr;

  // Advance the chain: hand the next layer its own nextInfo.
  XrApiLayerCreateInfo nextApiLayerInfo = *apiLayerInfo;
  nextApiLayerInfo.nextInfo = nextInfo->next;

  XrResult res =
      nextInfo->nextCreateApiLayerInstance(info, &nextApiLayerInfo, instance);
  if (XR_FAILED(res)) {
    Log("CreateApiLayerInstance: downstream create failed");
    return res;
  }

  // Resolve the functions we dispatch to through the resolved gipa.
  PFN_xrGetInstanceProcAddr gipa = g_dispatch.getInstanceProcAddr;
  if (gipa) {
    gipa(*instance, "xrCreateSession",
         reinterpret_cast<PFN_xrVoidFunction*>(&g_dispatch.createSession));
    gipa(*instance, "xrDestroySession",
         reinterpret_cast<PFN_xrVoidFunction*>(&g_dispatch.destroySession));
    gipa(*instance, "xrEndFrame",
         reinterpret_cast<PFN_xrVoidFunction*>(&g_dispatch.endFrame));
  }

  Log("CreateApiLayerInstance ok");
  return res;
}

extern "C" __declspec(dllexport) XRAPI_ATTR XrResult XRAPI_CALL
xrNegotiateLoaderApiLayerInterface(const XrNegotiateLoaderInfo* loaderInfo,
                                   const char* apiLayerName,
                                   XrNegotiateApiLayerRequest* apiLayerRequest) {
  (void)apiLayerName;
  if (!loaderInfo || !apiLayerRequest) {
    Log("negotiate: null arguments");
    return XR_ERROR_INITIALIZATION_FAILED;
  }

  apiLayerRequest->layerInterfaceVersion = XR_CURRENT_LOADER_API_LAYER_VERSION;
  apiLayerRequest->layerApiVersion = XR_CURRENT_API_VERSION;
  apiLayerRequest->getInstanceProcAddr = MyGetInstanceProcAddr;
  apiLayerRequest->createApiLayerInstance = MyCreateApiLayerInstance;

  Log("negotiate ok");
  return XR_SUCCESS;
}
