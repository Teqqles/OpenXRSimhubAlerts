// Direct3D 12 overlay backend: draws flat coloured quads into an OpenXR
// swapchain that the endFrame hook composites as a head-locked quad layer.
// Mirrors d3d11_backend.cpp; D3D12 specifics (own allocator/list, fence-sync,
// resource-state transitions) are documented inline.
#include "d3d12_backend.h"
#include "log.h"
#include "shm_contract.h"   // MAX_CARS

#include <d3d12.h>
#include <d3dcompiler.h>
#include <cstring>
#include <vector>

namespace {

// Overlay resolution. Small and fixed: the quads are simple shapes, and the
// composition-layer quad in the world is what determines apparent size.
constexpr int32_t  kDim      = 512;
// One flag quad + up to MAX_CARS radar blips, plus slack, 6 verts each.
constexpr uint32_t kMaxQuads = MAX_CARS + 2;
constexpr uint32_t kMaxVerts = kMaxQuads * 6;

struct Vertex {
  float x, y;        // NDC position
  float r, g, b, a;  // straight-alpha colour
};

// Minimal screen-space passthrough: position already in NDC, per-vertex colour.
// Identical to the D3D11 backend's shaders (compiled for shader model 5.1,
// valid under D3D12).
const char* kHlsl =
    "struct VSIn  { float2 pos : POSITION; float4 col : COLOR; };\n"
    "struct VSOut { float4 pos : SV_POSITION; float4 col : COLOR; };\n"
    "VSOut vs_main(VSIn i){ VSOut o; o.pos = float4(i.pos, 0.0f, 1.0f); o.col = i.col; return o; }\n"
    "float4 ps_main(VSOut i) : SV_TARGET { return i.col; }\n";

template <class T>
void SafeRelease(T*& p) { if (p) { p->Release(); p = nullptr; } }

}  // namespace

bool D3D12Backend::Init(XrSession session, const void* graphicsBinding, XrInstance instance) {
  const auto* binding = reinterpret_cast<const XrGraphicsBindingD3D12KHR*>(graphicsBinding);
  if (!binding || !binding->device || !binding->queue) {
    Log("d3d12: no device/queue in binding");
    return false;
  }

  _device = binding->device;
  _queue  = binding->queue;
  _device->AddRef();
  _queue->AddRef();

  PFN_xrGetInstanceProcAddr gipa = g_dispatch.getInstanceProcAddr;
  if (!gipa) { Log("d3d12: no getInstanceProcAddr"); return false; }

  PFN_xrCreateSwapchain           pfnCreate = nullptr;
  PFN_xrEnumerateSwapchainImages  pfnEnumImg = nullptr;
  PFN_xrEnumerateSwapchainFormats pfnEnumFmt = nullptr;
  gipa(instance, "xrCreateSwapchain",           reinterpret_cast<PFN_xrVoidFunction*>(&pfnCreate));
  gipa(instance, "xrEnumerateSwapchainImages",  reinterpret_cast<PFN_xrVoidFunction*>(&pfnEnumImg));
  gipa(instance, "xrEnumerateSwapchainFormats", reinterpret_cast<PFN_xrVoidFunction*>(&pfnEnumFmt));
  gipa(instance, "xrAcquireSwapchainImage",     reinterpret_cast<PFN_xrVoidFunction*>(&_acquire));
  gipa(instance, "xrWaitSwapchainImage",        reinterpret_cast<PFN_xrVoidFunction*>(&_wait));
  gipa(instance, "xrReleaseSwapchainImage",     reinterpret_cast<PFN_xrVoidFunction*>(&_release));
  if (!pfnCreate || !pfnEnumImg || !pfnEnumFmt || !_acquire || !_wait || !_release) {
    Log("d3d12: swapchain entry points unresolved");
    return false;
  }

  // Pick a runtime-supported colour format, preferring plain RGBA8 UNORM (28),
  // then its sRGB sibling (29), else whatever the runtime lists first.
  uint32_t fmtCount = 0;
  if (XR_FAILED(pfnEnumFmt(session, 0, &fmtCount, nullptr)) || fmtCount == 0) {
    Log("d3d12: no swapchain formats");
    return false;
  }
  std::vector<int64_t> formats(fmtCount);
  if (XR_FAILED(pfnEnumFmt(session, fmtCount, &fmtCount, formats.data()))) {
    Log("d3d12: enumerate formats failed");
    return false;
  }
  int64_t chosen = -1;
  for (int64_t f : formats) if (f == 28) { chosen = f; break; }   // R8G8B8A8_UNORM
  if (chosen < 0) for (int64_t f : formats) if (f == 29) { chosen = f; break; }  // _UNORM_SRGB
  if (chosen < 0) chosen = formats[0];
  _format = static_cast<DXGI_FORMAT>(chosen);

  XrSwapchainCreateInfo sci{XR_TYPE_SWAPCHAIN_CREATE_INFO};
  sci.usageFlags  = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
  sci.format      = chosen;
  sci.sampleCount = 1;
  sci.width       = kDim;
  sci.height      = kDim;
  sci.faceCount   = 1;
  sci.arraySize   = 1;
  sci.mipCount    = 1;
  if (XR_FAILED(pfnCreate(session, &sci, &_swapchain)) || _swapchain == XR_NULL_HANDLE) {
    Log("d3d12: xrCreateSwapchain failed");
    return false;
  }
  _width  = kDim;
  _height = kDim;

  uint32_t imgCount = 0;
  if (XR_FAILED(pfnEnumImg(_swapchain, 0, &imgCount, nullptr)) || imgCount == 0) {
    Log("d3d12: no swapchain images");
    return false;
  }
  std::vector<XrSwapchainImageD3D12KHR> images(
      imgCount, XrSwapchainImageD3D12KHR{XR_TYPE_SWAPCHAIN_IMAGE_D3D12_KHR});
  if (XR_FAILED(pfnEnumImg(_swapchain, imgCount, &imgCount,
                           reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data())))) {
    Log("d3d12: enumerate images failed");
    return false;
  }

  // RTV heap: one descriptor per swapchain image.
  D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
  heapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
  heapDesc.NumDescriptors = imgCount;
  heapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
  if (FAILED(_device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&_rtvHeap))) || !_rtvHeap) {
    Log("d3d12: CreateDescriptorHeap failed");
    return false;
  }
  _rtvStride = _device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

  D3D12_RENDER_TARGET_VIEW_DESC rtvd{};
  rtvd.Format        = _format;
  rtvd.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
  D3D12_CPU_DESCRIPTOR_HANDLE handle = _rtvHeap->GetCPUDescriptorHandleForHeapStart();
  _images.reserve(imgCount);
  for (uint32_t i = 0; i < imgCount; ++i) {
    if (!images[i].texture) { Log("d3d12: null swapchain image texture"); return false; }
    _device->CreateRenderTargetView(images[i].texture, &rtvd, handle);
    _images.push_back(images[i].texture);   // raw ref; runtime owns the resource
    handle.ptr += _rtvStride;
  }

  // Empty root signature: nothing is bound beyond the vertex stream (colour is
  // per-vertex), so we only need the input-assembler input-layout flag.
  {
    D3D12_ROOT_SIGNATURE_DESC rsDesc{};
    rsDesc.NumParameters     = 0;
    rsDesc.pParameters       = nullptr;
    rsDesc.NumStaticSamplers = 0;
    rsDesc.pStaticSamplers   = nullptr;
    rsDesc.Flags             = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    ID3DBlob* sig = nullptr;
    ID3DBlob* rerr = nullptr;
    HRESULT hr = D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &rerr);
    if (FAILED(hr) || !sig) {
      Log("d3d12: root signature serialize failed");
      SafeRelease(sig); SafeRelease(rerr);
      return false;
    }
    hr = _device->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(),
                                      IID_PPV_ARGS(&_rootSig));
    SafeRelease(sig); SafeRelease(rerr);
    if (FAILED(hr) || !_rootSig) { Log("d3d12: CreateRootSignature failed"); return false; }
  }

  // Compile the inline shaders once (shader model 5.1 is valid for D3D12).
  ID3DBlob* vsBlob = nullptr;
  ID3DBlob* psBlob = nullptr;
  ID3DBlob* err    = nullptr;
  HRESULT hr = D3DCompile(kHlsl, std::strlen(kHlsl), "overlay", nullptr, nullptr,
                          "vs_main", "vs_5_1", 0, 0, &vsBlob, &err);
  if (FAILED(hr) || !vsBlob) { Log("d3d12: VS compile failed"); SafeRelease(err); SafeRelease(vsBlob); return false; }
  SafeRelease(err);
  hr = D3DCompile(kHlsl, std::strlen(kHlsl), "overlay", nullptr, nullptr,
                  "ps_main", "ps_5_1", 0, 0, &psBlob, &err);
  if (FAILED(hr) || !psBlob) { Log("d3d12: PS compile failed"); SafeRelease(err); SafeRelease(vsBlob); SafeRelease(psBlob); return false; }
  SafeRelease(err);

  const D3D12_INPUT_ELEMENT_DESC ied[] = {
    {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    {"COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 8, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
  };

  D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
  pso.pRootSignature        = _rootSig;
  pso.VS                    = {vsBlob->GetBufferPointer(), vsBlob->GetBufferSize()};
  pso.PS                    = {psBlob->GetBufferPointer(), psBlob->GetBufferSize()};
  pso.InputLayout           = {ied, 2};
  pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  pso.NumRenderTargets      = 1;
  pso.RTVFormats[0]         = _format;
  pso.DSVFormat             = DXGI_FORMAT_UNKNOWN;
  pso.SampleDesc.Count      = 1;
  pso.SampleMask            = UINT_MAX;
  pso.NodeMask              = 0;

  // Rasterizer: solid, no culling (2D overlay, either winding must show),
  // depth clip on. Set explicitly rather than relying on defaults.
  pso.RasterizerState.FillMode              = D3D12_FILL_MODE_SOLID;
  pso.RasterizerState.CullMode              = D3D12_CULL_MODE_NONE;
  pso.RasterizerState.FrontCounterClockwise = FALSE;
  pso.RasterizerState.DepthClipEnable       = TRUE;
  pso.RasterizerState.ConservativeRaster    = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

  // No depth/stencil.
  pso.DepthStencilState.DepthEnable   = FALSE;
  pso.DepthStencilState.StencilEnable = FALSE;

  // Straight-alpha "over" blend, matching the D3D11 backend.
  pso.BlendState.AlphaToCoverageEnable  = FALSE;
  pso.BlendState.IndependentBlendEnable = FALSE;
  pso.BlendState.RenderTarget[0].BlendEnable           = TRUE;
  pso.BlendState.RenderTarget[0].LogicOpEnable         = FALSE;
  pso.BlendState.RenderTarget[0].SrcBlend              = D3D12_BLEND_SRC_ALPHA;
  pso.BlendState.RenderTarget[0].DestBlend             = D3D12_BLEND_INV_SRC_ALPHA;
  pso.BlendState.RenderTarget[0].BlendOp               = D3D12_BLEND_OP_ADD;
  pso.BlendState.RenderTarget[0].SrcBlendAlpha         = D3D12_BLEND_ONE;
  pso.BlendState.RenderTarget[0].DestBlendAlpha        = D3D12_BLEND_INV_SRC_ALPHA;
  pso.BlendState.RenderTarget[0].BlendOpAlpha          = D3D12_BLEND_OP_ADD;
  pso.BlendState.RenderTarget[0].LogicOp               = D3D12_LOGIC_OP_NOOP;
  pso.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

  hr = _device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&_pso));
  SafeRelease(vsBlob);
  SafeRelease(psBlob);
  if (FAILED(hr) || !_pso) { Log("d3d12: CreateGraphicsPipelineState failed"); return false; }

  // Vertex buffer on an UPLOAD heap: persistently mapped so per-frame fills need
  // no allocation. UPLOAD resources start (and stay) in GENERIC_READ state.
  {
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type                 = D3D12_HEAP_TYPE_UPLOAD;
    hp.CPUPageProperty      = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    hp.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

    D3D12_RESOURCE_DESC rd{};
    rd.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Alignment        = 0;
    rd.Width            = sizeof(Vertex) * kMaxVerts;
    rd.Height           = 1;
    rd.DepthOrArraySize = 1;
    rd.MipLevels        = 1;
    rd.Format           = DXGI_FORMAT_UNKNOWN;
    rd.SampleDesc.Count = 1;
    rd.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    rd.Flags            = D3D12_RESOURCE_FLAG_NONE;

    if (FAILED(_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                                                D3D12_RESOURCE_STATE_GENERIC_READ,
                                                nullptr, IID_PPV_ARGS(&_vbuf))) || !_vbuf) {
      Log("d3d12: vertex buffer creation failed");
      return false;
    }
    D3D12_RANGE noRead{0, 0};   // we only write from the CPU
    if (FAILED(_vbuf->Map(0, &noRead, &_vbufMapped)) || !_vbufMapped) {
      Log("d3d12: vertex buffer map failed");
      return false;
    }
    _vbv.BufferLocation = _vbuf->GetGPUVirtualAddress();
    _vbv.SizeInBytes    = sizeof(Vertex) * kMaxVerts;
    _vbv.StrideInBytes  = sizeof(Vertex);
  }

  // Command allocator + a graphics command list. Reset() the list immediately
  // so we hold it in the closed state expected at the top of Render().
  if (FAILED(_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                             IID_PPV_ARGS(&_alloc))) || !_alloc) {
    Log("d3d12: CreateCommandAllocator failed");
    return false;
  }
  if (FAILED(_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, _alloc, _pso,
                                        IID_PPV_ARGS(&_cmdList))) || !_cmdList) {
    Log("d3d12: CreateCommandList failed");
    return false;
  }
  _cmdList->Close();   // created open; Render() resets before recording

  // Fence + event for CPU-GPU sync (block until the GPU has written the image).
  if (FAILED(_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&_fence))) || !_fence) {
    Log("d3d12: CreateFence failed");
    return false;
  }
  _fenceValue = 0;
  _fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  if (!_fenceEvent) { Log("d3d12: CreateEvent failed"); return false; }

  Log("d3d12: overlay backend initialised");
  return true;
}

bool D3D12Backend::Render(const std::vector<OverlayQuad>& quads) {
  if (_swapchain == XR_NULL_HANDLE || _images.empty() || !_cmdList || !_queue || !_fence) {
    return false;
  }

  uint32_t index = 0;
  XrSwapchainImageAcquireInfo ai{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
  if (XR_FAILED(_acquire(_swapchain, &ai, &index))) return false;

  XrSwapchainImageWaitInfo wi{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
  wi.timeout = XR_INFINITE_DURATION;
  if (XR_FAILED(_wait(_swapchain, &wi))) {
    XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    _release(_swapchain, &ri);   // release what we acquired
    return false;
  }

  bool drew = false;

  if (index < _images.size()) {
    // We own the allocator + list, so each frame starts from cleared GPU state;
    // no host render-state save/restore is needed (unlike the shared D3D11
    // immediate context).
    if (SUCCEEDED(_alloc->Reset()) && SUCCEEDED(_cmdList->Reset(_alloc, _pso))) {
      ID3D12Resource* target = _images[index];

      // Resource-state assumption: the OpenXR D3D12 runtime hands acquired
      // images back in a COMMON-compatible state. We transition
      // COMMON -> RENDER_TARGET to draw, then RENDER_TARGET -> COMMON before
      // release. (Flagged for in-headset verification in Task 13.)
      D3D12_RESOURCE_BARRIER toRT{};
      toRT.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
      toRT.Flags                  = D3D12_RESOURCE_BARRIER_FLAG_NONE;
      toRT.Transition.pResource   = target;
      toRT.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
      toRT.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
      toRT.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
      _cmdList->ResourceBarrier(1, &toRT);

      D3D12_CPU_DESCRIPTOR_HANDLE rtv = _rtvHeap->GetCPUDescriptorHandleForHeapStart();
      rtv.ptr += static_cast<SIZE_T>(index) * _rtvStride;
      _cmdList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);

      D3D12_VIEWPORT vp{};
      vp.TopLeftX = 0.0f;
      vp.TopLeftY = 0.0f;
      vp.Width    = static_cast<float>(_width);
      vp.Height   = static_cast<float>(_height);
      vp.MinDepth = 0.0f;
      vp.MaxDepth = 1.0f;
      _cmdList->RSSetViewports(1, &vp);

      D3D12_RECT scissor{0, 0, _width, _height};
      _cmdList->RSSetScissorRects(1, &scissor);

      const float clear[4] = {0.0f, 0.0f, 0.0f, 0.0f};   // transparent
      _cmdList->ClearRenderTargetView(rtv, clear, 0, nullptr);

      // Fill the persistently mapped vertex buffer (no per-frame heap alloc).
      const uint32_t n = static_cast<uint32_t>(quads.size()) < kMaxQuads
                             ? static_cast<uint32_t>(quads.size())
                             : kMaxQuads;
      Vertex* v = static_cast<Vertex*>(_vbufMapped);
      for (uint32_t i = 0; i < n; ++i) {
        const OverlayQuad& q = quads[i];
        // rgba is 0xAARRGGBB (see FlagColor). w,h are half-extents: the quad
        // spans u +/- w, v +/- h in NDC.
        const float a = ((q.rgba >> 24) & 0xFF) / 255.0f;
        const float r = ((q.rgba >> 16) & 0xFF) / 255.0f;
        const float g = ((q.rgba >> 8)  & 0xFF) / 255.0f;
        const float b = ( q.rgba        & 0xFF) / 255.0f;
        const float x0 = q.u - q.w, x1 = q.u + q.w;
        const float y0 = q.v - q.h, y1 = q.v + q.h;
        // Shape id (q.shape, incl. radar 255) is ignored in v1: every shape is
        // drawn as a plain filled quad. Shape masking is a future refinement.
        Vertex* t = v + i * 6;
        t[0] = {x0, y0, r, g, b, a};
        t[1] = {x0, y1, r, g, b, a};
        t[2] = {x1, y1, r, g, b, a};
        t[3] = {x0, y0, r, g, b, a};
        t[4] = {x1, y1, r, g, b, a};
        t[5] = {x1, y0, r, g, b, a};
      }

      _cmdList->SetGraphicsRootSignature(_rootSig);
      _cmdList->SetPipelineState(_pso);
      _cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
      _cmdList->IASetVertexBuffers(0, 1, &_vbv);
      if (n > 0) _cmdList->DrawInstanced(n * 6, 1, 0, 0);

      D3D12_RESOURCE_BARRIER toCommon = toRT;
      toCommon.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
      toCommon.Transition.StateAfter  = D3D12_RESOURCE_STATE_COMMON;
      _cmdList->ResourceBarrier(1, &toCommon);

      if (SUCCEEDED(_cmdList->Close())) {
        ID3D12CommandList* lists[] = {_cmdList};
        _queue->ExecuteCommandLists(1, lists);

        // Block until the GPU has finished writing the image, so it is ready
        // before we release it back to the runtime (D3D12 needs this explicit
        // sync; the D3D11 immediate context did not).
        const UINT64 signalTo = ++_fenceValue;
        if (SUCCEEDED(_queue->Signal(_fence, signalTo))) {
          if (_fence->GetCompletedValue() < signalTo) {
            if (SUCCEEDED(_fence->SetEventOnCompletion(signalTo, _fenceEvent))) {
              WaitForSingleObject(_fenceEvent, INFINITE);
            }
          }
          drew = true;
        }
      }
    }
  }

  XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
  const bool released = XR_SUCCEEDED(_release(_swapchain, &ri));

  // True only when acquired + waited + drawn + released all succeeded.
  return drew && released;
}

void D3D12Backend::Release() {
  if (_vbuf && _vbufMapped) { _vbuf->Unmap(0, nullptr); _vbufMapped = nullptr; }
  SafeRelease(_vbuf);
  SafeRelease(_cmdList);
  SafeRelease(_alloc);
  SafeRelease(_pso);
  SafeRelease(_rootSig);
  SafeRelease(_rtvHeap);
  _images.clear();   // raw, non-AddRef'd runtime-owned pointers; nothing to free
  SafeRelease(_fence);
  if (_fenceEvent) { CloseHandle(_fenceEvent); _fenceEvent = nullptr; }
  // Note: the XrSwapchain is owned by the runtime and destroyed with the
  // session; we deliberately do not call xrDestroySwapchain here (no hook).
  _swapchain = XR_NULL_HANDLE;
  SafeRelease(_queue);
  SafeRelease(_device);
}

IRenderBackend* CreateD3D12Backend() { return new D3D12Backend(); }
