#include "overlay.h"
#include <cmath>

// ---------------------------------------------------------------------------
// Canonical overlay geometry. This is the single source of truth for how every
// shape looks (Highlander): flags and radar blips are emitted here as triangle
// lists in NDC (y up), and the graphics backends draw those triangles verbatim.
// The in-plugin WPF preview mirrors this math; keep the two in sync.
// ---------------------------------------------------------------------------

namespace {

struct Rgba { float r, g, b, a; };

// Decode 0xAARRGGBB, override alpha, and scale RGB brightness (clamped).
Rgba Decode(uint32_t argb, float alpha, float bright) {
  float r = ((argb >> 16) & 0xFF) / 255.0f * bright;
  float g = ((argb >> 8)  & 0xFF) / 255.0f * bright;
  float b = ( argb        & 0xFF) / 255.0f * bright;
  auto sat = [](float v) { return v < 0 ? 0.0f : (v > 1 ? 1.0f : v); };
  return { sat(r), sat(g), sat(b), sat(alpha) };
}

void PushTri(std::vector<OverlayVertex>& o,
             float ax, float ay, float bx, float by, float cx, float cy,
             const Rgba& c) {
  o.push_back({ ax, ay, c.r, c.g, c.b, c.a });
  o.push_back({ bx, by, c.r, c.g, c.b, c.a });
  o.push_back({ cx, cy, c.r, c.g, c.b, c.a });
}

// Axis-aligned filled rectangle: centre (u,v), half-extents (hw,hh).
void PushRect(std::vector<OverlayVertex>& o, float u, float v, float hw, float hh, const Rgba& c) {
  float x0 = u - hw, x1 = u + hw, y0 = v - hh, y1 = v + hh;
  PushTri(o, x0, y0, x0, y1, x1, y1, c);
  PushTri(o, x0, y0, x1, y1, x1, y0, c);
}

// Filled ellipse via a triangle fan, half-extents (hw,hh).
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

// Isoceles triangle, apex "up" in NDC, then rotated clockwise by `angle`
// (radians, matching a bearing measured as atan2(x,-y)). half-extents (hw,hh).
void PushTriangle(std::vector<OverlayVertex>& o, float u, float v, float hw, float hh, float angle, const Rgba& c) {
  float sa = std::sin(angle), ca = std::cos(angle);
  // Local points: apex up (+y), base corners bottom.
  float lx[3] = { 0.0f, -hw,  hw };
  float ly[3] = {  hh, -hh, -hh };
  float wx[3], wy[3];
  for (int i = 0; i < 3; ++i) {
    // Rotate clockwise by angle about the centre (NDC y-up).
    wx[i] = u + (lx[i] * ca + ly[i] * sa);
    wy[i] = v + (-lx[i] * sa + ly[i] * ca);
  }
  PushTri(o, wx[0], wy[0], wx[1], wy[1], wx[2], wy[2], c);
}

uint32_t FlagColor(uint8_t bit) {
  switch (bit) {
    case FLAG_RED:      return 0xFFFF2020u;
    case FLAG_MEATBALL: return 0xFFFF8000u; // orange
    case FLAG_BLACK:    return 0xFF101010u;
    case FLAG_BLUE:     return 0xFF2060FFu;
    case FLAG_YELLOW:   return 0xFFFFE000u;
    case FLAG_WHITE:    return 0xFFF0F0F0u;
    case FLAG_GREEN:    return 0xFF20D040u;
    default:            return 0x00000000u;
  }
}

// Non-bar flag shapes are scaled to equal visual AREA, using the circle of
// radius sz (area = pi*sz^2) as the reference, so swapping shape never changes
// apparent size. Each shape keeps its own aspect ratio; only its overall size is matched.
//   square  side s:            s^2 = pi        -> half = sqrt(pi)/2
//   triangle base=height=2k:   2k^2 = pi       -> k    = sqrt(pi/2)
//   rect (h = 0.6*w), 4*hw*hh: 2.4*hw^2 = pi   -> hw   = sqrt(pi/2.4)
constexpr float kSquareHalf = 0.8862269f;  // sqrt(pi)/2
constexpr float kTriHalf    = 1.2533141f;  // sqrt(pi/2)
constexpr float kRectHalfW  = 1.1441037f;  // sqrt(pi/2.4)
constexpr float kRectHalfH  = 0.6864622f;  // 0.6 * kRectHalfW

// Emit a flag shape (config.shape) at (u,v), base half-extent sz.
void EmitFlag(std::vector<OverlayVertex>& o, uint8_t shape, float u, float v, float sz, const Rgba& c) {
  switch (shape) {
    case 0: PushRect(o, u, v, sz, 0.35f * sz, c);                       break; // bar (edge marker, not area-matched)
    case 1: PushRect(o, u, v, kRectHalfW * sz, kRectHalfH * sz, c);     break; // rect
    case 2: PushRect(o, u, v, kSquareHalf * sz, kSquareHalf * sz, c);   break; // square
    case 3: PushEllipse(o, u, v, sz, sz, c);                            break; // circle (reference)
    case 4: PushTriangle(o, u, v, kTriHalf * sz, kTriHalf * sz, 0.0f, c); break; // triangle
    default: PushRect(o, u, v, kSquareHalf * sz, kSquareHalf * sz, c);  break;
  }
}

// Emit a radar blip (config.radarShape) on the arc at (u,v). The "car" shape is a
// small vertical rectangle marker (angle ignored); the "arrow" shape is a triangle
// rotated by `angle` to point at the car. `halfW` is uniform for every blip;
// colour and opacity convey closeness, not size.
void EmitRadar(std::vector<OverlayVertex>& o, uint8_t shape, float u, float v, float halfW, float angle, const Rgba& c) {
  if (shape == 1) PushTriangle(o, u, v, 1.2f * halfW, 1.7f * halfW, angle, c);  // arrow
  else            PushRect(o, u, v, halfW, 1.8f * halfW, c);                    // car (vertical rect)
}

}  // namespace

void BuildOverlay(const DataBlock& b, OverlayGeometry& out) {
  out.leftEye.clear();
  out.rightEye.clear();
  // Telemetry disconnected: emit nothing so stale flags/radar clear from the HUD
  // instead of freezing the last frame (e.g. a phantom red flag lingering).
  if (!b.connected) return;

  const Config& cfg = b.config;
  const float flagAlpha  = cfg.flagOpacity   > 0 ? cfg.flagOpacity   : 1.0f;
  const float radarCeil  = cfg.radarMaxOpacity > 0 ? cfg.radarMaxOpacity : 1.0f;
  const float scaleFlag  = cfg.scaleFlag  > 0 ? cfg.scaleFlag  : 1.0f;
  const float scaleRadar = cfg.scaleRadar > 0 ? cfg.scaleRadar : 1.0f;

  // Flags: pick the highest-priority active flag; render in BOTH eyes.
  if (cfg.enableFlags && b.activeFlags) {
    static const uint8_t prio[] = { FLAG_RED, FLAG_MEATBALL, FLAG_BLACK, FLAG_BLUE, FLAG_YELLOW, FLAG_WHITE, FLAG_GREEN };
    for (uint8_t bit : prio) {
      if (b.activeFlags & bit) {
        // Meatball ("mechanical black") flag: a BLACK flag carrying an ORANGE
        // disc in its centre, per motorsport convention, rather than a solid
        // orange shape. Every other flag is a single solid colour.
        const bool meatball = (bit == FLAG_MEATBALL);
        Rgba c   = Decode(meatball ? 0xFF101010u : FlagColor(bit), flagAlpha, 1.0f);
        Rgba dot = Decode(0xFFFF8000u, flagAlpha, 1.0f);  // orange centre disc
        if (cfg.shape == 0) {
          // Bar: a full-height vertical bar down the outer edge of each eye
          // (left edge of the left eye, right edge of the right eye). Pulled in
          // from the extreme edge so it stays inside the headset visible area.
          const float kFlagEdge = 0.82f;
          float w = 0.05f * scaleFlag;
          PushRect(out.leftEye,  -kFlagEdge, 0.0f, w, 1.0f, c);
          PushRect(out.rightEye,  kFlagEdge, 0.0f, w, 1.0f, c);
          if (meatball) {
            float rdot = 2.0f * w;
            PushEllipse(out.leftEye,  -kFlagEdge, 0.0f, rdot, rdot, dot);
            PushEllipse(out.rightEye,  kFlagEdge, 0.0f, rdot, rdot, dot);
          }
        } else {
          // Other shapes: a marker mirrored to the outer edge of each eye.
          float sz = 0.15f * scaleFlag;
          float ex = std::fabs(cfg.posFlag.x);
          EmitFlag(out.leftEye,  cfg.shape, -ex, cfg.posFlag.y, sz, c);
          EmitFlag(out.rightEye, cfg.shape,  ex, cfg.posFlag.y, sz, c);
          if (meatball) {
            float rdot = 0.45f * sz;
            // Centre the disc on the shape's visual centroid. Every shape is
            // centred on posFlag.y except the triangle, whose apex-up centroid
            // sits a third of its half-height below centre.
            float dcy = cfg.posFlag.y - (cfg.shape == 4 ? kTriHalf * sz / 3.0f : 0.0f);
            PushEllipse(out.leftEye,  -ex, dcy, rdot, rdot, dot);
            PushEllipse(out.rightEye,  ex, dcy, rdot, rdot, dot);
          }
        }
        break;
      }
    }
  }

  // Radar: one blip per tracked car, on a circle centered on each lens middle.
  // Cars purely ahead (side 3) are never drawn. A left car rides the left eye's
  // outer rim, a right car the right eye's outer rim, and a car behind sits at
  // the bottom-center of BOTH eyes so it is always seen. The inner (nose-side)
  // half is never populated; the per-eye routing below guarantees that, so no
  // clip is needed. Colour and transparency alone encode distance: a close car
  // is a bright, opaque red and a far car a dim, faint red.
  if (cfg.enableRadar) {
    const float kPI = 3.14159265f;
    for (uint32_t i = 0; i < b.carCount && i < MAX_CARS; ++i) {
      const CarBlip& c = b.cars[i];
      if (c.side == 3 || c.side == 0) continue;        // never render ahead / none

      float t = c.distance / (cfg.radarRange > 0 ? cfg.radarRange : 1);
      if (t > 1) t = 1;
      float closeness = 1.0f - t;                      // 1 = right on top, 0 = at range
      float bearing = std::atan2(c.rel.x, -c.rel.y);   // 0 = behind, +/- = sides
      float halfW = 0.03f * scaleRadar;  // uniform size; threat shown by colour/opacity

      // Shades of red: closer = brighter + more opaque, farther = dimmer + fainter.
      float alpha  = radarCeil * (0.15f + 0.85f * closeness);
      float bright = 0.35f + 0.65f * closeness;
      Rgba col = Decode(0xFFFF0000u, alpha, bright);

      // Both shapes ride a circle centered on the lens middle (NDC origin) at the
      // car's bearing: bottom for behind, outer rim for the sides. Radius scales
      // with scaleRadar, clamped so the back + outer blips always stay on-lens
      // (the top/ahead of the ring is never populated, so it may run off-lens).
      // Arrows rotate to point at the car; the car marker is an upright rectangle.
      float radius = 0.8f * scaleRadar;
      if (radius > 0.85f) radius = 0.85f;
      float u =  radius * std::sin(bearing);
      float v = -radius * std::cos(bearing);
      float angle = kPI - bearing;

      // Per-eye stereo: behind -> both eyes; left -> left eye; right -> right eye.
      if (c.side == 4) {
        EmitRadar(out.leftEye,  cfg.radarShape, u, v, halfW, angle, col);
        EmitRadar(out.rightEye, cfg.radarShape, u, v, halfW, angle, col);
      } else if (c.side == 1) {
        EmitRadar(out.leftEye,  cfg.radarShape, u, v, halfW, angle, col);
      } else if (c.side == 2) {
        EmitRadar(out.rightEye, cfg.radarShape, u, v, halfW, angle, col);
      }
    }
  }
}
