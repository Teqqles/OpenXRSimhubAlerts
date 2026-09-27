#include "overlay.h"
#include <cmath>

// Draws the plugin's element list. How flags and radar look is decided in the
// plugin (OverlayComposer.cs); this file only turns shapes into triangles.

namespace {

struct Rgba { float r, g, b, a; };

Rgba Decode(uint32_t argb) {
  return { ((argb >> 16) & 0xFF) / 255.0f, ((argb >> 8) & 0xFF) / 255.0f,
           (argb & 0xFF) / 255.0f, (argb >> 24) / 255.0f };
}

void PushTri(std::vector<OverlayVertex>& o,
             float ax, float ay, float bx, float by, float cx, float cy,
             const Rgba& c) {
  o.push_back({ ax, ay, c.r, c.g, c.b, c.a });
  o.push_back({ bx, by, c.r, c.g, c.b, c.a });
  o.push_back({ cx, cy, c.r, c.g, c.b, c.a });
}

void PushRect(std::vector<OverlayVertex>& o, float u, float v, float hw, float hh, const Rgba& c) {
  float x0 = u - hw, x1 = u + hw, y0 = v - hh, y1 = v + hh;
  PushTri(o, x0, y0, x0, y1, x1, y1, c);
  PushTri(o, x0, y0, x1, y1, x1, y0, c);
}

// Triangle fan.
void PushEllipse(std::vector<OverlayVertex>& o, float u, float v, float hw, float hh, const Rgba& c) {
  const int kSeg = 24;
  float prevx = u + hw, prevy = v;
  for (int i = 1; i <= kSeg; ++i) {
    float a = 6.2831853f * i / kSeg;
    float x = u + hw * std::cos(a);
    float y = v + hh * std::sin(a);
    PushTri(o, u, v, prevx, prevy, x, y, c);
    prevx = x; prevy = y;
  }
}

// Triangle fan like PushEllipse, but only the centre vertex is opaque; rim
// vertices share the colour at alpha 0 so blending fades the halo out.
void PushGlow(std::vector<OverlayVertex>& o, float u, float v, float hw, float hh, const Rgba& c) {
  const int kSeg = 24;
  float prevx = u + hw, prevy = v;
  for (int i = 1; i <= kSeg; ++i) {
    float a = 6.2831853f * i / kSeg;
    float x = u + hw * std::cos(a);
    float y = v + hh * std::sin(a);
    o.push_back({ u, v, c.r, c.g, c.b, c.a });
    o.push_back({ prevx, prevy, c.r, c.g, c.b, 0.0f });
    o.push_back({ x, y, c.r, c.g, c.b, 0.0f });
    prevx = x; prevy = y;
  }
}

// Isosceles triangle with its apex up, rotated clockwise by `angle` about its centre.
void PushTriangle(std::vector<OverlayVertex>& o, float u, float v, float hw, float hh, float angle, const Rgba& c) {
  float sa = std::sin(angle), ca = std::cos(angle);
  float lx[3] = { 0.0f, -hw,  hw };
  float ly[3] = {  hh, -hh, -hh };
  float wx[3], wy[3];
  for (int i = 0; i < 3; ++i) {
    wx[i] = u + (lx[i] * ca + ly[i] * sa);
    wy[i] = v + (-lx[i] * sa + ly[i] * ca);
  }
  PushTri(o, wx[0], wy[0], wx[1], wy[1], wx[2], wy[2], c);
}

void Emit(const Element& e, float du, std::vector<OverlayVertex>& o) {
  const Rgba c = Decode(e.color);
  const float u = e.u + du;
  switch (e.kind) {
    case ELEMENT_RECT:     PushRect(o, u, e.v, e.hw, e.hh, c);              break;
    case ELEMENT_ELLIPSE:  PushEllipse(o, u, e.v, e.hw, e.hh, c);           break;
    case ELEMENT_TRIANGLE: PushTriangle(o, u, e.v, e.hw, e.hh, e.angle, c); break;
    case ELEMENT_GLOW:     PushGlow(o, u, e.v, e.hw, e.hh, c);              break;
    default: break;  // none; text and icon arrive with #4
  }
}

uint32_t ElementCount(const DataBlock& b) {
  return b.elementCount < MAX_ELEMENTS ? b.elementCount : MAX_ELEMENTS;
}

}  // namespace

void BuildOverlay(const DataBlock& b, OverlayGeometry& out, const EyeAnchors& anchors) {
  out.leftEye.clear();
  out.rightEye.clear();
  // Telemetry disconnected: emit nothing so stale alerts clear instead of freezing.
  if (!b.connected) return;

  // Counting sort by priority: stable, allocation-free, linear in the element count.
  const uint32_t n = ElementCount(b);
  uint16_t start[257] = {};
  for (uint32_t i = 0; i < n; ++i) ++start[b.elements[i].priority + 1];
  for (int p = 1; p <= 256; ++p) start[p] += start[p - 1];
  uint8_t order[MAX_ELEMENTS];
  for (uint32_t i = 0; i < n; ++i) order[start[b.elements[i].priority]++] = static_cast<uint8_t>(i);

  for (uint32_t i = 0; i < n; ++i) {
    const Element& e = b.elements[order[i]];
    const bool anchored = (e.flags & ELEMENT_FORWARD_ANCHORED) != 0;
    if (e.eyes & EYE_LEFT)  Emit(e, anchored ? anchors.leftU : 0.0f, out.leftEye);
    if (e.eyes & EYE_RIGHT) Emit(e, anchored ? anchors.rightU : 0.0f, out.rightEye);
  }
}

uint32_t TimeCriticalSignature(const DataBlock& b) {
  if (!b.connected) return 0;
  uint32_t left = 0, right = 0;
  const uint32_t n = ElementCount(b);
  for (uint32_t i = 0; i < n; ++i) {
    const Element& e = b.elements[i];
    if (!(e.flags & ELEMENT_TIME_CRITICAL)) continue;
    if (e.eyes & EYE_LEFT)  ++left;
    if (e.eyes & EYE_RIGHT) ++right;
  }
  return (left << 16) | right;
}
