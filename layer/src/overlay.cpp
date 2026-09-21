#include "overlay.h"
#include <cmath>

// ---------------------------------------------------------------------------
// Canonical overlay geometry. This is the single source of truth for how every
// shape looks (Highlander): flags and radar blips are emitted here as triangle
// lists in NDC (y up), and the graphics backends draw those triangles verbatim.
// The in-plugin WPF preview mirrors this math -- keep the two in sync.
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

// Emit a flag shape (config.shape) at (u,v), base half-extent sz.
void EmitFlag(std::vector<OverlayVertex>& o, uint8_t shape, float u, float v, float sz, const Rgba& c) {
  switch (shape) {
    case 0: PushEllipse(o, u, v, 0.4f * sz, 0.4f * sz, c); break; // dot
    case 1: PushRect(o, u, v, sz, 0.35f * sz, c);          break; // bar
    case 2: PushRect(o, u, v, sz, 0.6f * sz, c);           break; // rect
    case 3: PushRect(o, u, v, sz, sz, c);                  break; // square
    case 4: PushEllipse(o, u, v, sz, sz, c);               break; // circle
    case 5: PushTriangle(o, u, v, sz, sz, 0.0f, c);        break; // triangle
    default: PushRect(o, u, v, sz, sz, c);                 break;
  }
}

// Emit a radar blip (config.radarShape) at (u,v), base half-extent sz, pointing
// along `bearing` when it is an arrow.
void EmitRadar(std::vector<OverlayVertex>& o, uint8_t shape, float u, float v, float sz, float bearing, const Rgba& c) {
  if (shape == 1) PushTriangle(o, u, v, sz, 1.4f * sz, bearing, c);  // arrow
  else            PushRect(o, u, v, 0.6f * sz, 1.4f * sz, c);        // car (vertical bar)
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
        float sz = 0.15f * scaleFlag;
        Rgba c = Decode(FlagColor(bit), flagAlpha, 1.0f);
        EmitFlag(out.leftEye,  cfg.shape, cfg.posFlag.x, cfg.posFlag.y, sz, c);
        EmitFlag(out.rightEye, cfg.shape, cfg.posFlag.x, cfg.posFlag.y, sz, c);
        break;
      }
    }
  }

  // Radar: one blip per tracked car. Cars purely ahead (side 3) are never drawn.
  if (cfg.enableRadar) {
    for (uint32_t i = 0; i < b.carCount && i < MAX_CARS; ++i) {
      const CarBlip& c = b.cars[i];
      if (c.side == 3 || c.side == 0) continue;        // never render ahead / none

      float t = c.distance / (cfg.radarRange > 0 ? cfg.radarRange : 1);
      if (t > 1) t = 1;
      float closeness = 1.0f - t;                      // 1 = right on top, 0 = at range
      float bearing = std::atan2(c.rel.x, -c.rel.y);   // 0 = behind, +/- = sides
      float radius = 0.5f + 0.4f * t;
      float u = radius * std::sin(bearing);
      float v = -0.6f + radius * (std::cos(bearing) * 0.2f);
      float sz = ((c.flags & 1) ? 0.05f : 0.03f) * scaleRadar;  // emphasise closest threat

      // Opacity + brightness rise as the car gets closer, up to the ceiling.
      float alpha  = radarCeil * (0.35f + 0.65f * closeness);
      float bright = 0.5f + 0.5f * closeness;
      uint32_t base = (c.side == 4) ? 0xFFFFFFFFu : 0xFFFFC000u;  // behind white, sides amber
      Rgba col = Decode(base, alpha, bright);

      // Per-eye stereo: behind -> both eyes; left -> left eye; right -> right eye.
      if (c.side == 4) {
        EmitRadar(out.leftEye,  cfg.radarShape, u, v, sz, bearing, col);
        EmitRadar(out.rightEye, cfg.radarShape, u, v, sz, bearing, col);
      } else if (c.side == 1) {
        EmitRadar(out.leftEye,  cfg.radarShape, u, v, sz, bearing, col);
      } else if (c.side == 2) {
        EmitRadar(out.rightEye, cfg.radarShape, u, v, sz, bearing, col);
      }
    }
  }
}
