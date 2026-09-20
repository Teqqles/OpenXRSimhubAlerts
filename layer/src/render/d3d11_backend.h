#pragma once
#include "../hooks.h"        // full OpenXR + D3D11 types (ID3D11*, XR_NULL_HANDLE, PFN_*)
#include "render_backend.h"
#include <vector>

// Real Direct3D 11 overlay renderer. Draws flat, per-vertex-coloured quads into
// an OpenXR-owned swapchain image; the swapchain is then referenced by a quad
// composition layer submitted from the endFrame hook. Screen-space simple: no
// depth, straight-alpha blending. Never throws; failed Init() => disabled.
class D3D11Backend : public IRenderBackend {
public:
  bool Init(XrSession session, const void* graphicsBinding, XrInstance instance) override;
  XrSwapchain Swapchain() const override { return _swapchain; }
  int32_t Width() const override { return _width; }
  int32_t Height() const override { return _height; }
  void Render(const std::vector<OverlayQuad>& quads) override;
  void Release() override;
  ~D3D11Backend() override { Release(); }

private:
  ID3D11Device*        _device  = nullptr;
  ID3D11DeviceContext* _ctx     = nullptr;

  XrSwapchain _swapchain = XR_NULL_HANDLE;
  int32_t     _width  = 0;
  int32_t     _height = 0;

  std::vector<ID3D11RenderTargetView*> _rtvs;

  ID3D11VertexShader* _vs     = nullptr;
  ID3D11PixelShader*  _ps     = nullptr;
  ID3D11InputLayout*  _layout = nullptr;
  ID3D11Buffer*       _vbuf   = nullptr;
  ID3D11BlendState*   _blend  = nullptr;

  // OpenXR swapchain image lifecycle, resolved once in Init().
  PFN_xrAcquireSwapchainImage _acquire = nullptr;
  PFN_xrWaitSwapchainImage    _wait    = nullptr;
  PFN_xrReleaseSwapchainImage _release = nullptr;
};
