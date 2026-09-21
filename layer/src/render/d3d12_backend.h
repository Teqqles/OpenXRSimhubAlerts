#pragma once
#include "../hooks.h"        // full OpenXR + D3D12 types (ID3D12*, XR_NULL_HANDLE, PFN_*)
#include "render_backend.h"
#include <vector>

// Real Direct3D 12 overlay renderer. Mirrors D3D11Backend: draws flat,
// per-vertex-coloured quads into an OpenXR-owned swapchain image which the
// endFrame hook then references as a head-locked quad composition layer.
// Screen-space simple: no depth, straight-alpha blending. Never throws; a
// failed Init() => the overlay stays disabled (session.cpp -> pass-through).
//
// D3D12 differs from D3D11 in that we do NOT share the app's command list or
// allocator: we own our own command allocator + graphics command list and
// submit to the app's command QUEUE. Each command list begins with fully
// cleared state, so there is no host render-state to save/restore (unlike the
// D3D11 immediate-context path). We DO fence-wait after ExecuteCommandLists so
// the GPU has finished writing the image before we release it back to the
// runtime.
class D3D12Backend : public IRenderBackend {
public:
  bool Init(XrSession session, const void* graphicsBinding, XrInstance instance) override;
  XrSwapchain Swapchain() const override { return _swapchain; }
  int32_t Width() const override { return _width; }
  int32_t Height() const override { return _height; }
  bool Render(const OverlayGeometry& geo) override;
  void Release() override;
  ~D3D12Backend() override { Release(); }

private:
  ID3D12Device*       _device = nullptr;   // app-owned, AddRef'd while we hold it
  ID3D12CommandQueue* _queue  = nullptr;   // app-owned, AddRef'd while we hold it

  XrSwapchain _swapchain = XR_NULL_HANDLE;
  int32_t     _width  = 0;
  int32_t     _height = 0;
  DXGI_FORMAT _format = DXGI_FORMAT_UNKNOWN;

  // One RTV per swapchain image in a single RTV heap. The image resources are
  // runtime-owned; we hold raw (non-AddRef'd) pointers only to issue resource
  // barriers on them, valid for the swapchain's lifetime (never destroyed here).
  ID3D12DescriptorHeap*             _rtvHeap   = nullptr;
  UINT                              _rtvStride = 0;
  std::vector<ID3D12Resource*>      _images;

  ID3D12RootSignature* _rootSig = nullptr;   // empty: colour is per-vertex
  ID3D12PipelineState* _pso     = nullptr;

  ID3D12Resource*          _vbuf       = nullptr;   // UPLOAD heap, persistently mapped
  void*                    _vbufMapped = nullptr;
  D3D12_VERTEX_BUFFER_VIEW _vbv{};

  ID3D12CommandAllocator*    _alloc   = nullptr;
  ID3D12GraphicsCommandList* _cmdList = nullptr;

  ID3D12Fence* _fence      = nullptr;
  UINT64       _fenceValue = 0;
  HANDLE       _fenceEvent = nullptr;

  // OpenXR swapchain image lifecycle, resolved once in Init().
  PFN_xrAcquireSwapchainImage _acquire = nullptr;
  PFN_xrWaitSwapchainImage    _wait    = nullptr;
  PFN_xrReleaseSwapchainImage _release = nullptr;
};
