// Direct3D 11 overlay backend: draws flat coloured quads into an OpenXR
// swapchain that the endFrame hook composites as a head-locked quad layer.
#include "d3d11_backend.h"
#include "log.h"
#include "shm_contract.h"   // MAX_CARS

#include <d3dcompiler.h>
#include <cstring>
#include <vector>

namespace {

// Overlay resolution. The texture is split into two eye halves side by side:
// kEyeDim x kEyeDim each, so kEyeDim*2 wide. The left half is composited to the
// left eye, the right half to the right eye (see endframe.cpp).
constexpr int32_t  kEyeDim   = 512;
// Generous cap: both eye lists concatenated, worst case a flag disc (~72 verts)
// plus MAX_CARS blips (<=6 verts each) per eye. 4096 leaves ample slack.
constexpr uint32_t kMaxVerts = 4096;

// Layout matches OverlayVertex exactly, so we memcpy the emitted geometry.
struct Vertex {
  float x, y;        // NDC position
  float r, g, b, a;  // straight-alpha colour
};

// Minimal screen-space passthrough: position already in NDC, per-vertex colour.
const char* kHlsl =
    "struct VSIn  { float2 pos : POSITION; float4 col : COLOR; };\n"
    "struct VSOut { float4 pos : SV_POSITION; float4 col : COLOR; };\n"
    "VSOut vs_main(VSIn i){ VSOut o; o.pos = float4(i.pos, 0.0f, 1.0f); o.col = i.col; return o; }\n"
    "float4 ps_main(VSOut i) : SV_TARGET { return i.col; }\n";

template <class T>
void SafeRelease(T*& p) { if (p) { p->Release(); p = nullptr; } }

}  // namespace

bool D3D11Backend::Init(XrSession session, const void* graphicsBinding, XrInstance instance) {
  const auto* binding = reinterpret_cast<const XrGraphicsBindingD3D11KHR*>(graphicsBinding);
  if (!binding || !binding->device) { Log("d3d11: no device in binding"); return false; }

  _device = binding->device;
  _device->AddRef();
  _device->GetImmediateContext(&_ctx);   // returns an AddRef'd context we own
  if (!_ctx) { Log("d3d11: GetImmediateContext failed"); return false; }

  PFN_xrGetInstanceProcAddr gipa = g_dispatch.getInstanceProcAddr;
  if (!gipa) { Log("d3d11: no getInstanceProcAddr"); return false; }

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
    Log("d3d11: swapchain entry points unresolved");
    return false;
  }

  // Pick a runtime-supported colour format, preferring plain RGBA8 UNORM (28),
  // then its sRGB sibling (29), else whatever the runtime lists first.
  uint32_t fmtCount = 0;
  if (XR_FAILED(pfnEnumFmt(session, 0, &fmtCount, nullptr)) || fmtCount == 0) {
    Log("d3d11: no swapchain formats");
    return false;
  }
  std::vector<int64_t> formats(fmtCount);
  if (XR_FAILED(pfnEnumFmt(session, fmtCount, &fmtCount, formats.data()))) {
    Log("d3d11: enumerate formats failed");
    return false;
  }
  int64_t chosen = -1;
  for (int64_t f : formats) if (f == 28) { chosen = f; break; }   // R8G8B8A8_UNORM
  if (chosen < 0) for (int64_t f : formats) if (f == 29) { chosen = f; break; }  // _UNORM_SRGB
  if (chosen < 0) chosen = formats[0];

  XrSwapchainCreateInfo sci{XR_TYPE_SWAPCHAIN_CREATE_INFO};
  sci.usageFlags  = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
  sci.format      = chosen;
  sci.sampleCount = 1;
  sci.width       = kEyeDim * 2;
  sci.height      = kEyeDim;
  sci.faceCount   = 1;
  sci.arraySize   = 1;
  sci.mipCount    = 1;
  if (XR_FAILED(pfnCreate(session, &sci, &_swapchain)) || _swapchain == XR_NULL_HANDLE) {
    Log("d3d11: xrCreateSwapchain failed");
    return false;
  }
  _width  = kEyeDim * 2;
  _height = kEyeDim;

  uint32_t imgCount = 0;
  if (XR_FAILED(pfnEnumImg(_swapchain, 0, &imgCount, nullptr)) || imgCount == 0) {
    Log("d3d11: no swapchain images");
    return false;
  }
  std::vector<XrSwapchainImageD3D11KHR> images(
      imgCount, XrSwapchainImageD3D11KHR{XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
  if (XR_FAILED(pfnEnumImg(_swapchain, imgCount, &imgCount,
                           reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data())))) {
    Log("d3d11: enumerate images failed");
    return false;
  }

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
  HRESULT hr = D3DCompile(kHlsl, std::strlen(kHlsl), "overlay", nullptr, nullptr,
                          "vs_main", "vs_5_0", 0, 0, &vsBlob, &err);
  if (FAILED(hr) || !vsBlob) { Log("d3d11: VS compile failed"); SafeRelease(err); SafeRelease(vsBlob); return false; }
  SafeRelease(err);
  hr = D3DCompile(kHlsl, std::strlen(kHlsl), "overlay", nullptr, nullptr,
                  "ps_main", "ps_5_0", 0, 0, &psBlob, &err);
  if (FAILED(hr) || !psBlob) { Log("d3d11: PS compile failed"); SafeRelease(err); SafeRelease(vsBlob); SafeRelease(psBlob); return false; }
  SafeRelease(err);

  bool ok = SUCCEEDED(_device->CreateVertexShader(
                vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &_vs)) &&
            SUCCEEDED(_device->CreatePixelShader(
                psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &_ps));

  if (ok) {
    const D3D11_INPUT_ELEMENT_DESC ied[] = {
      {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    ok = SUCCEEDED(_device->CreateInputLayout(ied, 2, vsBlob->GetBufferPointer(),
                                              vsBlob->GetBufferSize(), &_layout));
  }
  SafeRelease(vsBlob);
  SafeRelease(psBlob);
  if (!ok) { Log("d3d11: shader/layout creation failed"); return false; }

  D3D11_BUFFER_DESC bd{};
  bd.ByteWidth      = sizeof(Vertex) * kMaxVerts;
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

  Log("d3d11: overlay backend initialised");
  return true;
}

bool D3D11Backend::Render(const OverlayGeometry& geo) {
  if (_swapchain == XR_NULL_HANDLE || _rtvs.empty() || !_ctx) return false;

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

  // Whether the overlay was actually drawn. Only set true after a successful
  // draw; kept false on any early-out so we can gate the composition layer.
  bool drew = false;

  if (index < _rtvs.size()) {
    // ---- Save the host renderer's immediate-context state we are about to
    // clobber. We share the app's context, so anything we bind must be put
    // back before we return, on EVERY path out of this scope (draw success or
    // Map failure alike) -- the restore + Release block below runs
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

    // ---- Overlay draw. ----
    ID3D11RenderTargetView* rtv = _rtvs[index];
    const float clear[4] = {0.0f, 0.0f, 0.0f, 0.0f};   // transparent
    _ctx->ClearRenderTargetView(rtv, clear);
    _ctx->OMSetRenderTargets(1, &rtv, nullptr);

    // Concatenate both eye triangle lists into the cached dynamic buffer (no
    // per-frame heap allocation), then draw each into its half of the target
    // via a viewport: left eye -> left half, right eye -> right half. The
    // per-vertex NDC maps to whichever viewport is bound, and NDC clipping keeps
    // each eye's geometry inside its half.
    uint32_t nL = static_cast<uint32_t>(geo.leftEye.size());
    uint32_t nR = static_cast<uint32_t>(geo.rightEye.size());
    if (nL > kMaxVerts) nL = kMaxVerts;
    if (nL + nR > kMaxVerts) nR = kMaxVerts - nL;

    D3D11_MAPPED_SUBRESOURCE map{};
    if (SUCCEEDED(_ctx->Map(_vbuf, 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) {
      Vertex* v = static_cast<Vertex*>(map.pData);
      if (nL) std::memcpy(v,      geo.leftEye.data(),  nL * sizeof(Vertex));
      if (nR) std::memcpy(v + nL, geo.rightEye.data(), nR * sizeof(Vertex));
      _ctx->Unmap(_vbuf, 0);

      const UINT stride = sizeof(Vertex);
      const UINT offset = 0;
      _ctx->IASetInputLayout(_layout);
      _ctx->IASetVertexBuffers(0, 1, &_vbuf, &stride, &offset);
      _ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
      _ctx->VSSetShader(_vs, nullptr, 0);
      _ctx->PSSetShader(_ps, nullptr, 0);
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

    for (auto*& r : savedRTVs) SafeRelease(r);
    SafeRelease(savedDSV);
    SafeRelease(savedBlend);
    SafeRelease(savedLayout);
    SafeRelease(savedVB);
    SafeRelease(savedVS);
    SafeRelease(savedPS);
  }

  XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
  const bool released = XR_SUCCEEDED(_release(_swapchain, &ri));

  // True only when acquired + waited + drawn + released all succeeded.
  return drew && released;
}

void D3D11Backend::Release() {
  for (auto* rtv : _rtvs) if (rtv) rtv->Release();
  _rtvs.clear();
  SafeRelease(_blend);
  SafeRelease(_vbuf);
  SafeRelease(_layout);
  SafeRelease(_ps);
  SafeRelease(_vs);
  // Note: the XrSwapchain is owned by the runtime and destroyed with the
  // session; we deliberately do not call xrDestroySwapchain here (no hook).
  _swapchain = XR_NULL_HANDLE;
  SafeRelease(_ctx);
  SafeRelease(_device);
}

IRenderBackend* CreateD3D11Backend() { return new D3D11Backend(); }
