#pragma once
#include <cstdint>

// Shared bits for the Direct3D 11 and Direct3D 12 overlay backends. Both draw
// the same passthrough shader over the same vertex layout and both hold raw COM
// pointers, so the shader source, the COM release helper, and the DXGI colour
// format values live here rather than being duplicated per backend. Change them
// in ONE place.

// DXGI_FORMAT values as int64, matching the OpenXR swapchain-format list. Named
// constants avoid depending on the DXGI symbolic names being in scope for the
// int64 comparison (and document the magic numbers).
constexpr int64_t kDxgiFormatR8G8B8A8Unorm = 28;   // DXGI_FORMAT_R8G8B8A8_UNORM
constexpr int64_t kDxgiFormatR8G8B8A8Srgb  = 29;   // DXGI_FORMAT_R8G8B8A8_UNORM_SRGB

// Screen-space passthrough vertex shader (position already in NDC) feeding a
// distance-field pixel shader: it samples the shared atlas at the vertex UV
// and turns the signed distance into coverage, so the same draw call paints
// flat shapes (which sample the atlas's solid block), glyphs and icons alike.
// Compiled for shader model 5.0 under D3D11 and 5.1 under D3D12 (both valid).
inline const char* kOverlayHlsl =
    "Texture2D atlas : register(t0);\n"
    "SamplerState samp : register(s0);\n"
    "struct VSIn  { float2 pos : POSITION; float2 uv : TEXCOORD0; float4 col : COLOR; };\n"
    "struct VSOut { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; float4 col : COLOR; };\n"
    "VSOut vs_main(VSIn i){ VSOut o; o.pos = float4(i.pos, 0.0f, 1.0f); o.uv = i.uv; o.col = i.col; return o; }\n"
    "float4 ps_main(VSOut i) : SV_TARGET {\n"
    "  float d = atlas.Sample(samp, i.uv).r;\n"
    "  float w = max(fwidth(d), 1.0f / 255.0f);\n"
    "  float cover = saturate((d - 0.5f) / (2.0f * w) + 0.5f);\n"
    "  return float4(i.col.rgb, i.col.a * cover);\n"
    "}\n";

// Release + null a COM interface pointer, safe on nullptr.
template <class T>
inline void SafeRelease(T*& p) { if (p) { p->Release(); p = nullptr; } }
