// Direct3D 11 overlay backend: draws shapes, text and icons, all sampled from
// the shared distance field atlas, into an OpenXR swapchain that the endFrame
// hook composites as a head-locked quad layer.
#include "d3d11_backend.h"
#include "../atlas.h"       // OverlayAtlas, kAtlasSize
#include "d3d_common.h"     // kOverlayHlsl, SafeRelease, DXGI format constants
#include "log.h"

#include <d3dcompiler.h>
#include <cstring>
#include <vector>

// Overlay resolution (kEyeDim), vertex cap (kMaxVerts), clear colour, and the
// shared eye/format helpers live in render_backend.h; the shader, SafeRelease
// and DXGI format constants in d3d_common.h. The vertex layout is OverlayVertex.

bool D3D11Backend::Init(XrSession session, const void* graphicsBinding, XrInstance instance) {
  const auto* binding = reinterpret_cast<const XrGraphicsBindingD3D11KHR*>(graphicsBinding);
  if (!binding || !binding->device) { Log("d3d11: no device in binding"); return false; }

  _device = binding->device;
  _device->AddRef();
  _device->GetImmediateContext(&_ctx);   // returns an AddRef'd context we own
  if (!_ctx) { Log("d3d11: GetImmediateContext failed"); return false; }

  int64_t chosen = 0;
  if (!_sc.Create(session, instance, kDxgiFormatR8G8B8A8Unorm, kDxgiFormatR8G8B8A8Srgb,
                  "d3d11: ", chosen)) {
    return false;
  }

  std::vector<XrSwapchainImageD3D11KHR> images;
  if (!_sc.EnumerateImages(XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR, "d3d11: ", images)) {
    return false;
  }
  const uint32_t imgCount = static_cast<uint32_t>(images.size());

  D3D11_RENDER_TARGET_VIEW_DESC rtvd{};
  rtvd.Format        = static_cast<DXGI_FORMAT>(chosen);
  rtvd.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
  _rtvs.reserve(imgCount);
  for (uint32_t i = 0; i < imgCount; ++i) {
    ID3D11RenderTargetView* rtv = nullptr;
    if (FAILED(_device->CreateRenderTargetView(images[i].texture, &rtvd, &rtv)) || !rtv) {
      Log("d3d11: CreateRenderTargetView failed");
      return false;
    }
    _rtvs.push_back(rtv);
  }

  // Compile the inline shaders once.
  ID3DBlob* vsBlob = nullptr;
  ID3DBlob* psBlob = nullptr;
  ID3DBlob* err    = nullptr;
  HRESULT hr = D3DCompile(kOverlayHlsl, std::strlen(kOverlayHlsl), "overlay", nullptr, nullptr,
                          "vs_main", "vs_5_0", 0, 0, &vsBlob, &err);
  if (FAILED(hr) || !vsBlob) { Log("d3d11: VS compile failed"); SafeRelease(err); SafeRelease(vsBlob); return false; }
  SafeRelease(err);
  hr = D3DCompile(kOverlayHlsl, std::strlen(kOverlayHlsl), "overlay", nullptr, nullptr,
                  "ps_main", "ps_5_0", 0, 0, &psBlob, &err);
  if (FAILED(hr) || !psBlob) { Log("d3d11: PS compile failed"); SafeRelease(err); SafeRelease(vsBlob); SafeRelease(psBlob); return false; }
  SafeRelease(err);

  bool ok = SUCCEEDED(_device->CreateVertexShader(
                vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &_vs)) &&
            SUCCEEDED(_device->CreatePixelShader(
                psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &_ps));

  if (ok) {
    const D3D11_INPUT_ELEMENT_DESC ied[] = {
      {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 8,  D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    ok = SUCCEEDED(_device->CreateInputLayout(ied, 3, vsBlob->GetBufferPointer(),
                                              vsBlob->GetBufferSize(), &_layout));
  }
  SafeRelease(vsBlob);
  SafeRelease(psBlob);
  if (!ok) { Log("d3d11: shader/layout creation failed"); return false; }

  D3D11_BUFFER_DESC bd{};
  bd.ByteWidth      = sizeof(OverlayVertex) * kMaxVerts;
  bd.Usage          = D3D11_USAGE_DYNAMIC;
  bd.BindFlags      = D3D11_BIND_VERTEX_BUFFER;
  bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
  if (FAILED(_device->CreateBuffer(&bd, nullptr, &_vbuf))) {
    Log("d3d11: vertex buffer creation failed");
    return false;
  }

  D3D11_BLEND_DESC bs{};
  bs.RenderTarget[0].BlendEnable           = TRUE;
  bs.RenderTarget[0].SrcBlend              = D3D11_BLEND_SRC_ALPHA;
  bs.RenderTarget[0].DestBlend             = D3D11_BLEND_INV_SRC_ALPHA;
  bs.RenderTarget[0].BlendOp               = D3D11_BLEND_OP_ADD;
  bs.RenderTarget[0].SrcBlendAlpha         = D3D11_BLEND_ONE;
  bs.RenderTarget[0].DestBlendAlpha        = D3D11_BLEND_INV_SRC_ALPHA;
  bs.RenderTarget[0].BlendOpAlpha          = D3D11_BLEND_OP_ADD;
  bs.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
  if (FAILED(_device->CreateBlendState(&bs, &_blend))) {
    Log("d3d11: blend state creation failed");
    return false;
  }

  const Atlas& atlas = OverlayAtlas();
  D3D11_TEXTURE2D_DESC td{};
  td.Width            = kAtlasSize;
  td.Height           = kAtlasSize;
  td.MipLevels        = 1;
  td.ArraySize        = 1;
  td.Format           = DXGI_FORMAT_R8_UNORM;
  td.SampleDesc.Count = 1;
  td.Usage            = D3D11_USAGE_IMMUTABLE;
  td.BindFlags        = D3D11_BIND_SHADER_RESOURCE;
  D3D11_SUBRESOURCE_DATA sd{};
  sd.pSysMem     = atlas.pixels.data();
  sd.SysMemPitch = kAtlasSize;
  if (FAILED(_device->CreateTexture2D(&td, &sd, &_atlasTex)) || !_atlasTex) {
    Log("d3d11: atlas texture creation failed");
    return false;
  }

  D3D11_SHADER_RESOURCE_VIEW_DESC srvd{};
  srvd.Format                    = DXGI_FORMAT_R8_UNORM;
  srvd.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
  srvd.Texture2D.MostDetailedMip = 0;
  srvd.Texture2D.MipLevels       = 1;
  if (FAILED(_device->CreateShaderResourceView(_atlasTex, &srvd, &_atlasSrv)) || !_atlasSrv) {
    Log("d3d11: atlas SRV creation failed");
    return false;
  }

  D3D11_SAMPLER_DESC smd{};
  smd.Filter         = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
  smd.AddressU       = D3D11_TEXTURE_ADDRESS_CLAMP;
  smd.AddressV       = D3D11_TEXTURE_ADDRESS_CLAMP;
  smd.AddressW       = D3D11_TEXTURE_ADDRESS_CLAMP;
  smd.ComparisonFunc = D3D11_COMPARISON_NEVER;
  smd.MaxLOD         = D3D11_FLOAT32_MAX;
  if (FAILED(_device->CreateSamplerState(&smd, &_sampler)) || !_sampler) {
    Log("d3d11: atlas sampler creation failed");
    return false;
  }

  Log("d3d11: overlay backend initialised");
  return true;
}

bool D3D11Backend::Render(const OverlayGeometry& geo) {
  if (_sc.handle() == XR_NULL_HANDLE || _rtvs.empty() || !_ctx) return false;

  uint32_t index = 0;
  if (!_sc.AcquireWait(index)) return false;

  // Whether the overlay was actually drawn. Only set true after a successful
  // draw; kept false on any early-out so we can gate the composition layer.
  bool drew = false;

  if (index < _rtvs.size()) {
    // ---- Save the host renderer's immediate-context state we are about to
    // clobber. We share the app's context, so anything we bind must be put
    // back before we return, on EVERY path out of this scope (draw success or
    // Map failure alike), so the restore + Release block below runs
    // unconditionally. Each *Get* AddRef's the interfaces it returns; every one
    // is Released after restore so we never leak a host object. ----
    ID3D11RenderTargetView* savedRTVs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
    ID3D11DepthStencilView* savedDSV = nullptr;
    _ctx->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, savedRTVs, &savedDSV);

    ID3D11BlendState* savedBlend       = nullptr;
    float             savedBlendFac[4] = {0, 0, 0, 0};
    UINT              savedSampleMask  = 0xFFFFFFFFu;
    _ctx->OMGetBlendState(&savedBlend, savedBlendFac, &savedSampleMask);

    UINT           savedVpCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    D3D11_VIEWPORT savedVps[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE] = {};
    _ctx->RSGetViewports(&savedVpCount, savedVps);

    ID3D11InputLayout* savedLayout = nullptr;
    _ctx->IAGetInputLayout(&savedLayout);

    ID3D11Buffer* savedVB       = nullptr;
    UINT          savedVBStride = 0;
    UINT          savedVBOffset = 0;
    _ctx->IAGetVertexBuffers(0, 1, &savedVB, &savedVBStride, &savedVBOffset);

    D3D11_PRIMITIVE_TOPOLOGY savedTopo = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
    _ctx->IAGetPrimitiveTopology(&savedTopo);

    // Class-instance count 0 (nullptr) keeps this simple; we don't use them.
    ID3D11VertexShader* savedVS = nullptr;
    _ctx->VSGetShader(&savedVS, nullptr, nullptr);
    ID3D11PixelShader*  savedPS = nullptr;
    _ctx->PSGetShader(&savedPS, nullptr, nullptr);

    ID3D11ShaderResourceView* savedSrv = nullptr;
    _ctx->PSGetShaderResources(0, 1, &savedSrv);
    ID3D11SamplerState* savedSampler = nullptr;
    _ctx->PSGetSamplers(0, 1, &savedSampler);

    // ---- Overlay draw. ----
    ID3D11RenderTargetView* rtv = _rtvs[index];
    _ctx->ClearRenderTargetView(rtv, kOverlayClearColor);
    _ctx->OMSetRenderTargets(1, &rtv, nullptr);

    // Concatenate both eye triangle lists into the cached dynamic buffer (no
    // per-frame heap allocation), then draw each into its half of the target
    // via a viewport: left eye -> left half, right eye -> right half. The
    // per-vertex NDC maps to whichever viewport is bound, and NDC clipping keeps
    // each eye's geometry inside its half.
    uint32_t nL = 0, nR = 0;
    ClampEyeCounts(geo, nL, nR);

    D3D11_MAPPED_SUBRESOURCE map{};
    if (SUCCEEDED(_ctx->Map(_vbuf, 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) {
      OverlayVertex* v = static_cast<OverlayVertex*>(map.pData);
      if (nL) std::memcpy(v,      geo.leftEye.data(),  nL * sizeof(OverlayVertex));
      if (nR) std::memcpy(v + nL, geo.rightEye.data(), nR * sizeof(OverlayVertex));
      _ctx->Unmap(_vbuf, 0);

      const UINT stride = sizeof(OverlayVertex);
      const UINT offset = 0;
      _ctx->IASetInputLayout(_layout);
      _ctx->IASetVertexBuffers(0, 1, &_vbuf, &stride, &offset);
      _ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
      _ctx->VSSetShader(_vs, nullptr, 0);
      _ctx->PSSetShader(_ps, nullptr, 0);
      _ctx->PSSetShaderResources(0, 1, &_atlasSrv);
      _ctx->PSSetSamplers(0, 1, &_sampler);
      const float bf[4] = {0, 0, 0, 0};
      _ctx->OMSetBlendState(_blend, bf, 0xFFFFFFFFu);

      D3D11_VIEWPORT vp{};
      vp.Width    = static_cast<float>(kEyeDim);
      vp.Height   = static_cast<float>(kEyeDim);
      vp.MinDepth = 0.0f;
      vp.MaxDepth = 1.0f;
      vp.TopLeftX = 0.0f;                              // left eye -> left half
      vp.TopLeftY = 0.0f;
      _ctx->RSSetViewports(1, &vp);
      if (nL) _ctx->Draw(nL, 0);
      vp.TopLeftX = static_cast<float>(kEyeDim);       // right eye -> right half
      _ctx->RSSetViewports(1, &vp);
      if (nR) _ctx->Draw(nR, nL);

      drew = true;
    }

    // ---- Restore host state, then Release every AddRef'd *Get* result.
    // Passing back the saved (possibly nullptr) handles restores the exact
    // prior bindings, including "nothing bound" slots. ----
    _ctx->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, savedRTVs, savedDSV);
    _ctx->OMSetBlendState(savedBlend, savedBlendFac, savedSampleMask);
    _ctx->RSSetViewports(savedVpCount, savedVps);
    _ctx->IASetInputLayout(savedLayout);
    _ctx->IASetVertexBuffers(0, 1, &savedVB, &savedVBStride, &savedVBOffset);
    _ctx->IASetPrimitiveTopology(savedTopo);
    _ctx->VSSetShader(savedVS, nullptr, 0);
    _ctx->PSSetShader(savedPS, nullptr, 0);
    _ctx->PSSetShaderResources(0, 1, &savedSrv);
    _ctx->PSSetSamplers(0, 1, &savedSampler);

    for (auto*& r : savedRTVs) SafeRelease(r);
    SafeRelease(savedDSV);
    SafeRelease(savedBlend);
    SafeRelease(savedLayout);
    SafeRelease(savedVB);
    SafeRelease(savedVS);
    SafeRelease(savedPS);
    SafeRelease(savedSrv);
    SafeRelease(savedSampler);
  }

  const bool released = _sc.Release();

  // True only when acquired + waited + drawn + released all succeeded.
  return drew && released;
}

void D3D11Backend::Release() {
  for (auto* rtv : _rtvs) if (rtv) rtv->Release();
  _rtvs.clear();
  SafeRelease(_sampler);
  SafeRelease(_atlasSrv);
  SafeRelease(_atlasTex);
  SafeRelease(_blend);
  SafeRelease(_vbuf);
  SafeRelease(_layout);
  SafeRelease(_ps);
  SafeRelease(_vs);
  // Note: the XrSwapchain is owned by the runtime and destroyed with the
  // session; we deliberately do not call xrDestroySwapchain here (no hook).
  _sc.forget();
  SafeRelease(_ctx);
  SafeRelease(_device);
}

IRenderBackend* CreateD3D11Backend() { return new D3D11Backend(); }
