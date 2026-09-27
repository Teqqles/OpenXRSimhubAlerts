#include "overlay.h"
#include <cmath>

// Draws the plugin's element list. How flags and radar look is decided in the
// plugin (OverlayComposer.cs); this file only turns shapes into triangles.
//
// Every shape is anti-aliased by feathering: an opaque core inset by half of
// kFeatherNdc, and a rim out to half of kFeatherNdc beyond the outline whose
// alpha fades to 0. Blending interpolates the fade across the outline.
//
// Text and icon elements are textured quads sampling the shared distance field
// atlas; every other shape samples the atlas's solid block so one pixel shader
// covers both.

namespace {

struct Rgba { float r, g, b, a; };

Rgba Decode(uint32_t argb) {
  return { ((argb >> 16) & 0xFF) / 255.0f, ((argb >> 8) & 0xFF) / 255.0f,
           (argb & 0xFF) / 255.0f, (argb >> 24) / 255.0f };
}

// A shape vertex: every plain shape samples the atlas's solid block, so its
// coverage passes straight through to the vertex alpha.
OverlayVertex Vtx(float x, float y, const Rgba& c, float a) {
  return { x, y, kSolidU, kSolidV, c.r, c.g, c.b, a };
}

void PushTri(std::vector<OverlayVertex>& o,
             float ax, float ay, float bx, float by, float cx, float cy,
             const Rgba& c) {
  o.push_back(Vtx(ax, ay, c, c.a));
  o.push_back(Vtx(bx, by, c, c.a));
  o.push_back(Vtx(cx, cy, c, c.a));
}

constexpr float kHalfFeather = kFeatherNdc / 2;

struct Pt { float x, y; };

// One strip of a feathered rim: inner edge a-b opaque, outer edge oa-ob transparent.
void PushRimStrip(std::vector<OverlayVertex>& o, Pt a, Pt b, Pt oa, Pt ob, const Rgba& c) {
  o.push_back(Vtx(a.x,  a.y,  c, c.a));
  o.push_back(Vtx(oa.x, oa.y, c, 0.0f));
  o.push_back(Vtx(ob.x, ob.y, c, 0.0f));
  o.push_back(Vtx(a.x,  a.y,  c, c.a));
  o.push_back(Vtx(ob.x, ob.y, c, 0.0f));
  o.push_back(Vtx(b.x,  b.y,  c, c.a));
}

// Opaque core polygon (fanned from its first corner) plus a rim strip per edge.
// `in` and `out` are the same polygon at the inset and outset outlines.
void PushFeathered(std::vector<OverlayVertex>& o, const Pt* in, const Pt* out, int n, const Rgba& c) {
  for (int i = 1; i + 1 < n; ++i)
    PushTri(o, in[0].x, in[0].y, in[i].x, in[i].y, in[i + 1].x, in[i + 1].y, c);
  for (int i = 0; i < n; ++i) {
    const int j = (i + 1) % n;
    PushRimStrip(o, in[i], in[j], out[i], out[j], c);
  }
}

void PushRect(std::vector<OverlayVertex>& o, float u, float v, float hw, float hh, const Rgba& c) {
  const float iw = std::fmax(hw - kHalfFeather, 0.0f), ih = std::fmax(hh - kHalfFeather, 0.0f);
  const float ow = hw + kHalfFeather, oh = hh + kHalfFeather;
  const Pt in[4]  = { {u - iw, v - ih}, {u - iw, v + ih}, {u + iw, v + ih}, {u + iw, v - ih} };
  const Pt out[4] = { {u - ow, v - oh}, {u - ow, v + oh}, {u + ow, v + oh}, {u + ow, v - oh} };
  PushFeathered(o, in, out, 4, c);
}

// Triangle fan core with a ring of rim strips.
void PushEllipse(std::vector<OverlayVertex>& o, float u, float v, float hw, float hh, const Rgba& c) {
  const int kSeg = 24;
  const float iw = std::fmax(hw - kHalfFeather, 0.0f), ih = std::fmax(hh - kHalfFeather, 0.0f);
  const float ow = hw + kHalfFeather, oh = hh + kHalfFeather;
  for (int i = 0; i < kSeg; ++i) {
    const float a0 = 6.2831853f * i / kSeg, a1 = 6.2831853f * (i + 1) / kSeg;
    const float c0 = std::cos(a0), s0 = std::sin(a0), c1 = std::cos(a1), s1 = std::sin(a1);
    PushTri(o, u, v, u + iw * c0, v + ih * s0, u + iw * c1, v + ih * s1, c);
  }
  for (int i = 0; i < kSeg; ++i) {
    const float a0 = 6.2831853f * i / kSeg, a1 = 6.2831853f * (i + 1) / kSeg;
    const float c0 = std::cos(a0), s0 = std::sin(a0), c1 = std::cos(a1), s1 = std::sin(a1);
    PushRimStrip(o, {u + iw * c0, v + ih * s0}, {u + iw * c1, v + ih * s1},
                    {u + ow * c0, v + oh * s0}, {u + ow * c1, v + oh * s1}, c);
  }
}

// Isosceles triangle with its apex up, rotated clockwise by `angle` about its
// centre. The core and outer outlines are scaled about the incentre, which moves
// every edge by exactly half a feather.
// Triangle fan like PushEllipse, but only the centre vertex is opaque; rim
// vertices share the colour at alpha 0 so blending fades the halo out.
void PushGlow(std::vector<OverlayVertex>& o, float u, float v, float hw, float hh, const Rgba& c) {
  const int kSeg = 24;
  float prevx = u + hw, prevy = v;
  for (int i = 1; i <= kSeg; ++i) {
    float a = 6.2831853f * i / kSeg;
    float x = u + hw * std::cos(a);
    float y = v + hh * std::sin(a);
    o.push_back(Vtx(u, v, c, c.a));
    o.push_back(Vtx(prevx, prevy, c, 0.0f));
    o.push_back(Vtx(x, y, c, 0.0f));
    prevx = x; prevy = y;
  }
}

// Isosceles triangle with its apex up, rotated clockwise by `angle` about its centre.
void PushTriangle(std::vector<OverlayVertex>& o, float u, float v, float hw, float hh, float angle, const Rgba& c) {
  const float lx[3] = { 0.0f, -hw,  hw };
  const float ly[3] = {  hh, -hh, -hh };
  const float side = std::sqrt(hw * hw + 4 * hh * hh);    // each slanted side
  const float inradius = 2 * hw * hh / (hw + side);
  if (!(inradius > 0)) return;                             // degenerate: nothing to draw
  const float cy = hh * (hw - side) / (hw + side);         // incentre (x is 0)
  const float kIn = std::fmax(inradius - kHalfFeather, 0.0f) / inradius;
  const float kOut = (inradius + kHalfFeather) / inradius;

  const float sa = std::sin(angle), ca = std::cos(angle);
  Pt in[3], out[3];
  for (int i = 0; i < 3; ++i) {
    const float dx = lx[i], dy = ly[i] - cy;
    const float ix = dx * kIn, iy = cy + dy * kIn;
    const float ox = dx * kOut, oy = cy + dy * kOut;
    in[i]  = { u + (ix * ca + iy * sa), v + (-ix * sa + iy * ca) };
    out[i] = { u + (ox * ca + oy * sa), v + (-ox * sa + oy * ca) };
  }
  PushFeathered(o, in, out, 3, c);
}

// A quad from (x0, y0) to (x1, y1) sampling the atlas rectangle of `e`; the top
// edge (y1) samples the top row (v0).
void PushTextured(std::vector<OverlayVertex>& o, float x0, float y0, float x1, float y1,
                  const AtlasEntry& e, const Rgba& c) {
  const OverlayVertex tl{ x0, y1, e.u0, e.v0, c.r, c.g, c.b, c.a };
  const OverlayVertex tr{ x1, y1, e.u1, e.v0, c.r, c.g, c.b, c.a };
  const OverlayVertex bl{ x0, y0, e.u0, e.v1, c.r, c.g, c.b, c.a };
  const OverlayVertex br{ x1, y0, e.u1, e.v1, c.r, c.g, c.b, c.a };
  o.push_back(tl); o.push_back(bl); o.push_back(br);
  o.push_back(tl); o.push_back(br); o.push_back(tr);
}

// The atlas entry an element draws, or null when there is nothing to draw.
const AtlasEntry* EntryFor(const Element& e, const Atlas* atlas) {
  if (!atlas || !atlas->ok) return nullptr;
  const AtlasEntry* entry = nullptr;
  if (e.kind == ELEMENT_TEXT && e.ref >= kFirstGlyph && e.ref <= kLastGlyph)
    entry = &atlas->glyphs[e.ref - kFirstGlyph];
  if (e.kind == ELEMENT_ICON && e.ref < ICON_COUNT)
    entry = &atlas->icons[e.ref];
  return entry && entry->present ? entry : nullptr;
}

void Emit(const Element& e, float du, std::vector<OverlayVertex>& o, const Atlas* atlas) {
  const Rgba c = Decode(e.color);
  const float u = e.u + du;
  switch (e.kind) {
    case ELEMENT_RECT:     PushRect(o, u, e.v, e.hw, e.hh, c);              break;
    case ELEMENT_ELLIPSE:  PushEllipse(o, u, e.v, e.hw, e.hh, c);           break;
    case ELEMENT_TRIANGLE: PushTriangle(o, u, e.v, e.hw, e.hh, e.angle, c); break;
    case ELEMENT_GLOW:     PushGlow(o, u, e.v, e.hw, e.hh, c);              break;
    case ELEMENT_TEXT:
      if (const AtlasEntry* g = EntryFor(e, atlas))
        PushTextured(o, u + g->x0 * e.hh, e.v + g->y0 * e.hh, u + g->x1 * e.hh, e.v + g->y1 * e.hh, *g, c);
      break;
    case ELEMENT_ICON:
      if (const AtlasEntry* ic = EntryFor(e, atlas))
        PushTextured(o, u + ic->x0 * e.hw, e.v + ic->y0 * e.hh, u + ic->x1 * e.hw, e.v + ic->y1 * e.hh, *ic, c);
      break;
    default: break;   // none
  }
}

uint32_t ElementCount(const DataBlock& b) {
  return b.elementCount < MAX_ELEMENTS ? b.elementCount : MAX_ELEMENTS;
}

}  // namespace

void BuildOverlay(const DataBlock& b, OverlayGeometry& out, const EyeAnchors& anchors, const Atlas* atlas) {
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
    if (e.eyes & EYE_LEFT)  Emit(e, anchored ? anchors.leftU : 0.0f, out.leftEye, atlas);
    if (e.eyes & EYE_RIGHT) Emit(e, anchored ? anchors.rightU : 0.0f, out.rightEye, atlas);
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
