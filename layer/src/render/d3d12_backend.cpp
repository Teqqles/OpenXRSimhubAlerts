// Direct3D 12 overlay backend: draws shapes, text and icons, all sampled from
// the shared distance field atlas, into an OpenXR swapchain that the endFrame
// hook composites as a head-locked quad layer.
// Mirrors d3d11_backend.cpp; D3D12 specifics (own allocator/list, fence-sync,
// resource-state transitions) are documented inline.
#include "d3d12_backend.h"
#include "../atlas.h"       // OverlayAtlas, kAtlasSize
#include "d3d_common.h"     // kOverlayHlsl, SafeRelease, DXGI format constants
#include "log.h"

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

  // Root signature: parameter 0 is a descriptor table holding the atlas SRV
  // (t0); the atlas sampler (s0) is static, so it needs no descriptor heap.
  {
    D3D12_DESCRIPTOR_RANGE srvRange{};
    srvRange.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors                    = 1;
    srvRange.BaseShaderRegister                = 0;
    srvRange.RegisterSpace                     = 0;
    srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER param{};
    param.ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    param.DescriptorTable.NumDescriptorRanges = 1;
    param.DescriptorTable.pDescriptorRanges   = &srvRange;
    param.ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_STATIC_SAMPLER_DESC sampler{};
    sampler.Filter           = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.MipLODBias       = 0.0f;
    sampler.MaxAnisotropy    = 1;
    sampler.ComparisonFunc   = D3D12_COMPARISON_FUNC_NEVER;
    sampler.BorderColor      = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
    sampler.MinLOD           = 0.0f;
    sampler.MaxLOD           = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister   = 0;
    sampler.RegisterSpace    = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rsDesc{};
    rsDesc.NumParameters     = 1;
    rsDesc.pParameters       = &param;
    rsDesc.NumStaticSamplers = 1;
    rsDesc.pStaticSamplers   = &sampler;
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
    {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 8,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    {"COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
  };

  D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
  pso.pRootSignature        = _rootSig;
  pso.VS                    = {vsBlob->GetBufferPointer(), vsBlob->GetBufferSize()};
  pso.PS                    = {psBlob->GetBufferPointer(), psBlob->GetBufferSize()};
  pso.InputLayout           = {ied, 3};
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

  if (!CreateAtlas()) return false;

  Log("d3d12: overlay backend initialised");
  return true;
}

bool D3D12Backend::CreateAtlas() {
  const Atlas& atlas = OverlayAtlas();
  const size_t atlasBytes = static_cast<size_t>(kAtlasSize) * kAtlasSize;
  if (atlas.pixels.size() != atlasBytes) {
    Log("d3d12: atlas pixel buffer has the wrong size");
    return false;
  }

  // The texture lives on a DEFAULT heap and starts as a copy destination.
  D3D12_HEAP_PROPERTIES defaultHeap{};
  defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

  D3D12_RESOURCE_DESC td{};
  td.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  td.Width            = kAtlasSize;
  td.Height           = kAtlasSize;
  td.DepthOrArraySize = 1;
  td.MipLevels        = 1;
  td.Format           = DXGI_FORMAT_R8_UNORM;
  td.SampleDesc.Count = 1;
  td.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  td.Flags            = D3D12_RESOURCE_FLAG_NONE;
  if (FAILED(_device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &td,
                                              D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                              IID_PPV_ARGS(&_atlasTex))) || !_atlasTex) {
    Log("d3d12: atlas texture creation failed");
    return false;
  }

  // Shader visible heap holding the one SRV the root table points at.
  D3D12_DESCRIPTOR_HEAP_DESC hd{};
  hd.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  hd.NumDescriptors = 1;
  hd.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  if (FAILED(_device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&_srvHeap))) || !_srvHeap) {
    Log("d3d12: atlas SRV heap creation failed");
    return false;
  }
  D3D12_SHADER_RESOURCE_VIEW_DESC srvd{};
  srvd.Format                    = DXGI_FORMAT_R8_UNORM;
  srvd.ViewDimension             = D3D12_SRV_DIMENSION_TEXTURE2D;
  srvd.Shader4ComponentMapping   = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srvd.Texture2D.MostDetailedMip = 0;
  srvd.Texture2D.MipLevels       = 1;
  _device->CreateShaderResourceView(_atlasTex, &srvd,
                                    _srvHeap->GetCPUDescriptorHandleForHeapStart());

  // Staging buffer laid out as the copy engine wants it: each row starts at a
  // multiple of the footprint's RowPitch.
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
  UINT numRows = 0;
  UINT64 rowBytes = 0, uploadBytes = 0;
  _device->GetCopyableFootprints(&td, 0, 1, 0, &fp, &numRows, &rowBytes, &uploadBytes);
  if (numRows != static_cast<UINT>(kAtlasSize) || rowBytes < static_cast<UINT64>(kAtlasSize)) {
    Log("d3d12: unexpected atlas copy footprint");
    return false;
  }

  D3D12_HEAP_PROPERTIES uploadHeap{};
  uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;

  D3D12_RESOURCE_DESC bd{};
  bd.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
  bd.Width            = uploadBytes;
  bd.Height           = 1;
  bd.DepthOrArraySize = 1;
  bd.MipLevels        = 1;
  bd.Format           = DXGI_FORMAT_UNKNOWN;
  bd.SampleDesc.Count = 1;
  bd.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  bd.Flags            = D3D12_RESOURCE_FLAG_NONE;
  ID3D12Resource* upload = nullptr;
  if (FAILED(_device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &bd,
                                              D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                              IID_PPV_ARGS(&upload))) || !upload) {
    Log("d3d12: atlas upload buffer creation failed");
    return false;
  }

  void* mapped = nullptr;
  D3D12_RANGE noRead{0, 0};   // we only write from the CPU
  if (FAILED(upload->Map(0, &noRead, &mapped)) || !mapped) {
    Log("d3d12: atlas upload buffer map failed");
    SafeRelease(upload);
    return false;
  }
  auto* dst = static_cast<uint8_t*>(mapped) + fp.Offset;
  for (UINT y = 0; y < numRows; ++y) {
    std::memcpy(dst + static_cast<size_t>(y) * fp.Footprint.RowPitch,
                atlas.pixels.data() + static_cast<size_t>(y) * kAtlasSize, kAtlasSize);
  }
  upload->Unmap(0, nullptr);

  // Record the copy and the transition to a shader resource on our own list,
  // which Init left closed. It is closed again below, as Render() expects.
  if (FAILED(_alloc->Reset()) || FAILED(_cmdList->Reset(_alloc, nullptr))) {
    Log("d3d12: command list reset for the atlas upload failed");
    SafeRelease(upload);
    return false;
  }

  D3D12_TEXTURE_COPY_LOCATION copyDst{};
  copyDst.pResource        = _atlasTex;
  copyDst.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  copyDst.SubresourceIndex = 0;
  D3D12_TEXTURE_COPY_LOCATION copySrc{};
  copySrc.pResource       = upload;
  copySrc.Type            = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  copySrc.PlacedFootprint = fp;
  _cmdList->CopyTextureRegion(&copyDst, 0, 0, 0, &copySrc, nullptr);

  D3D12_RESOURCE_BARRIER toSrv{};
  toSrv.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  toSrv.Flags                  = D3D12_RESOURCE_BARRIER_FLAG_NONE;
  toSrv.Transition.pResource   = _atlasTex;
  toSrv.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  toSrv.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
  toSrv.Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
  _cmdList->ResourceBarrier(1, &toSrv);

  if (FAILED(_cmdList->Close())) {
    Log("d3d12: atlas upload command list close failed");
    SafeRelease(upload);
    return false;
  }
  ID3D12CommandList* lists[] = {_cmdList};
  _queue->ExecuteCommandLists(1, lists);

  // The upload buffer must outlive the copy, so wait for the GPU before freeing
  // it. Signal or wait setup only fails when the device is lost, and then the
  // GPU no longer reads the buffer either.
  const UINT64 signalTo = ++_fenceValue;
  bool done = SUCCEEDED(_queue->Signal(_fence, signalTo));
  if (done && _fence->GetCompletedValue() < signalTo) {
    done = SUCCEEDED(_fence->SetEventOnCompletion(signalTo, _fenceEvent)) &&
           WaitForSingleObject(_fenceEvent, INFINITE) == WAIT_OBJECT_0;
  }
  SafeRelease(upload);
  if (!done) {
    Log("d3d12: atlas upload fence wait failed");
    return false;
  }
  return true;
}

bool D3D12Backend::Render(const OverlayGeometry& geo) {
  if (_sc.handle() == XR_NULL_HANDLE || _images.empty() || !_cmdList || !_queue || !_fence ||
      !_srvHeap) {
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
      // release. Not yet verified in-headset.
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
      _cmdList->SetDescriptorHeaps(1, &_srvHeap);
      _cmdList->SetGraphicsRootDescriptorTable(0, _srvHeap->GetGPUDescriptorHandleForHeapStart());
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
  SafeRelease(_srvHeap);
  SafeRelease(_atlasTex);
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
