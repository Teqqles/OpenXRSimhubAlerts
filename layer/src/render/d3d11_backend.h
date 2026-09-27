#pragma once
#include "../hooks.h"        // full OpenXR + D3D11 types (ID3D11*, XR_NULL_HANDLE, PFN_*)
#include "render_backend.h"
#include "xr_swapchain.h"     // XrOverlaySwapchain
#include <vector>

// Real Direct3D 11 overlay renderer. Draws shapes, text and icons, all sampled
// from the shared distance field atlas, into an OpenXR-owned swapchain image;
// the swapchain is then referenced by a quad composition layer submitted from
// the endFrame hook. Screen-space simple: no depth, straight-alpha blending.
// Never throws; failed Init() => disabled.
class D3D11Backend : public IRenderBackend {
public:
  bool Init(XrSession session, const void* graphicsBinding, XrInstance instance) override;
  XrSwapchain Swapchain() const override { return _sc.handle(); }
  int32_t Width() const override { return _sc.width(); }
  int32_t Height() const override { return _sc.height(); }
  bool Render(const OverlayGeometry& geo) override;
  void Release() override;
  ~D3D11Backend() override { Release(); }

private:
  ID3D11Device*        _device  = nullptr;
  ID3D11DeviceContext* _ctx     = nullptr;

  XrOverlaySwapchain _sc;   // OpenXR swapchain + acquire/wait/release lifecycle

  std::vector<ID3D11RenderTargetView*> _rtvs;

  ID3D11VertexShader* _vs     = nullptr;
  ID3D11PixelShader*  _ps     = nullptr;
  ID3D11InputLayout*  _layout = nullptr;
  ID3D11Buffer*       _vbuf   = nullptr;
  ID3D11BlendState*   _blend  = nullptr;

  ID3D11Texture2D*          _atlasTex = nullptr;
  ID3D11ShaderResourceView* _atlasSrv = nullptr;
  ID3D11SamplerState*       _sampler  = nullptr;
};
