#pragma once
#include "../hooks.h"        // full OpenXR + D3D12 types (ID3D12*, XR_NULL_HANDLE, PFN_*)
#include "render_backend.h"
#include "xr_swapchain.h"     // XrOverlaySwapchain
#include <vector>

// Real Direct3D 12 overlay renderer. Mirrors D3D11Backend: draws shapes, text
// and icons, all sampled from the shared distance field atlas, into an
// OpenXR-owned swapchain image which the endFrame hook then references as a
// head-locked quad composition layer.
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
  XrSwapchain Swapchain() const override { return _sc.handle(); }
  int32_t Width() const override { return _sc.width(); }
  int32_t Height() const override { return _sc.height(); }
  bool Render(const OverlayGeometry& geo) override;
  void Release() override;
  ~D3D12Backend() override { Release(); }

private:
  // Creates the atlas texture and its SRV, and uploads the atlas pixels through
  // the backend's own command list. Needs the list, fence and event to exist;
  // leaves the list closed.
  bool CreateAtlas();

  ID3D12Device*       _device = nullptr;   // app-owned, AddRef'd while we hold it
  ID3D12CommandQueue* _queue  = nullptr;   // app-owned, AddRef'd while we hold it

  XrOverlaySwapchain _sc;   // OpenXR swapchain + acquire/wait/release lifecycle
  DXGI_FORMAT        _format = DXGI_FORMAT_UNKNOWN;

  // One RTV per swapchain image in a single RTV heap. The image resources are
  // runtime-owned; we hold raw (non-AddRef'd) pointers only to issue resource
  // barriers on them, valid for the swapchain's lifetime (never destroyed here).
  ID3D12DescriptorHeap*             _rtvHeap   = nullptr;
  UINT                              _rtvStride = 0;
  std::vector<ID3D12Resource*>      _images;

  ID3D12RootSignature* _rootSig = nullptr;   // atlas SRV table (t0) + static sampler (s0)
  ID3D12PipelineState* _pso     = nullptr;

  ID3D12Resource*       _atlasTex = nullptr;   // DEFAULT heap, R8, PIXEL_SHADER_RESOURCE
  ID3D12DescriptorHeap* _srvHeap  = nullptr;   // shader visible, one SRV: the atlas

  ID3D12Resource*          _vbuf       = nullptr;   // UPLOAD heap, persistently mapped
  void*                    _vbufMapped = nullptr;
  D3D12_VERTEX_BUFFER_VIEW _vbv{};

  ID3D12CommandAllocator*    _alloc   = nullptr;
  ID3D12GraphicsCommandList* _cmdList = nullptr;

  ID3D12Fence* _fence      = nullptr;
  UINT64       _fenceValue = 0;
  HANDLE       _fenceEvent = nullptr;
};
