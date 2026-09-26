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

// Minimal screen-space passthrough: position already in NDC, per-vertex colour.
// Compiled for shader model 5.0 under D3D11 and 5.1 under D3D12 (both valid).
inline const char* kOverlayHlsl =
    "struct VSIn  { float2 pos : POSITION; float4 col : COLOR; };\n"
    "struct VSOut { float4 pos : SV_POSITION; float4 col : COLOR; };\n"
    "VSOut vs_main(VSIn i){ VSOut o; o.pos = float4(i.pos, 0.0f, 1.0f); o.col = i.col; return o; }\n"
    "float4 ps_main(VSOut i) : SV_TARGET { return i.col; }\n";

// Release + null a COM interface pointer, safe on nullptr.
template <class T>
inline void SafeRelease(T*& p) { if (p) { p->Release(); p = nullptr; } }
