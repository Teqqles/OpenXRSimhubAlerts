// Direct3D 12 overlay backend: draws flat coloured quads into an OpenXR
// swapchain that the endFrame hook composites as a head-locked quad layer.
// Mirrors d3d11_backend.cpp; D3D12 specifics (own allocator/list, fence-sync,
// resource-state transitions) are documented inline.
#include "d3d12_backend.h"
#include "d3d_common.h"     // kOverlayHlsl, SafeRelease, DXGI format constants
#include "log.h"
#include "shm_contract.h"   // MAX_CARS

#include <d3d12.h>
#include <d3dcompiler.h>
#include <cstring>
#include <vector>

// Overlay resolution (kEyeDim), vertex cap (kMaxVerts), clear colour, and the
// shared eye/format helpers live in render_backend.h; the shader, SafeRelease
// and DXGI format constants in d3d_common.h. The vertex layout is OverlayVertex.

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

  int64_t chosen = 0;
  if (!_sc.Create(session, instance, kDxgiFormatR8G8B8A8Unorm, kDxgiFormatR8G8B8A8Srgb,
                  "d3d12: ", chosen)) {
    return false;
  }
  _format = static_cast<DXGI_FORMAT>(chosen);

  std::vector<XrSwapchainImageD3D12KHR> images;
  if (!_sc.EnumerateImages(XR_TYPE_SWAPCHAIN_IMAGE_D3D12_KHR, "d3d12: ", images)) {
    return false;
  }
  const uint32_t imgCount = static_cast<uint32_t>(images.size());

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
  HRESULT hr = D3DCompile(kOverlayHlsl, std::strlen(kOverlayHlsl), "overlay", nullptr, nullptr,
                          "vs_main", "vs_5_1", 0, 0, &vsBlob, &err);
  if (FAILED(hr) || !vsBlob) { Log("d3d12: VS compile failed"); SafeRelease(err); SafeRelease(vsBlob); return false; }
  SafeRelease(err);
  hr = D3DCompile(kOverlayHlsl, std::strlen(kOverlayHlsl), "overlay", nullptr, nullptr,
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
    rd.Width            = sizeof(OverlayVertex) * kMaxVerts;
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
    _vbv.SizeInBytes    = sizeof(OverlayVertex) * kMaxVerts;
    _vbv.StrideInBytes  = sizeof(OverlayVertex);
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

bool D3D12Backend::Render(const OverlayGeometry& geo) {
  if (_sc.handle() == XR_NULL_HANDLE || _images.empty() || !_cmdList || !_queue || !_fence) {
    return false;
  }

  uint32_t index = 0;
  if (!_sc.AcquireWait(index)) return false;

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

      _cmdList->ClearRenderTargetView(rtv, kOverlayClearColor, 0, nullptr);

      // Concatenate both eye triangle lists into the persistently mapped buffer
      // (no per-frame heap alloc), then draw each into its half of the target via
      // viewport+scissor: left eye -> left half, right eye -> right half.
      uint32_t nL = 0, nR = 0;
      ClampEyeCounts(geo, nL, nR);
      OverlayVertex* v = static_cast<OverlayVertex*>(_vbufMapped);
      if (nL) std::memcpy(v,      geo.leftEye.data(),  nL * sizeof(OverlayVertex));
      if (nR) std::memcpy(v + nL, geo.rightEye.data(), nR * sizeof(OverlayVertex));

      _cmdList->SetGraphicsRootSignature(_rootSig);
      _cmdList->SetPipelineState(_pso);
      _cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
      _cmdList->IASetVertexBuffers(0, 1, &_vbv);

      D3D12_VIEWPORT vp{};
      vp.Width    = static_cast<float>(kEyeDim);
      vp.Height   = static_cast<float>(kEyeDim);
      vp.MinDepth = 0.0f;
      vp.MaxDepth = 1.0f;
      vp.TopLeftX = 0.0f;                              // left eye -> left half
      vp.TopLeftY = 0.0f;
      _cmdList->RSSetViewports(1, &vp);
      D3D12_RECT scL{0, 0, kEyeDim, kEyeDim};
      _cmdList->RSSetScissorRects(1, &scL);
      if (nL) _cmdList->DrawInstanced(nL, 1, 0, 0);

      vp.TopLeftX = static_cast<float>(kEyeDim);       // right eye -> right half
      _cmdList->RSSetViewports(1, &vp);
      D3D12_RECT scR{kEyeDim, 0, kEyeDim * 2, kEyeDim};
      _cmdList->RSSetScissorRects(1, &scR);
      if (nR) _cmdList->DrawInstanced(nR, 1, nL, 0);

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

  const bool released = _sc.Release();

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
  _sc.forget();
  SafeRelease(_queue);
  SafeRelease(_device);
}

IRenderBackend* CreateD3D12Backend() { return new D3D12Backend(); }
