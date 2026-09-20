#include "overlay.h"
#include <cmath>
static uint32_t FlagColor(uint8_t bit) {
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
void BuildOverlay(const DataBlock& b, std::vector<OverlayQuad>& out) {
  out.clear();
  // Telemetry disconnected: emit nothing so stale flags/radar clear from the HUD
  // instead of freezing the last frame (e.g. a phantom red flag lingering).
  if (!b.connected) return;
  if (b.config.enableFlags && b.activeFlags) {
    static const uint8_t prio[] = { FLAG_RED, FLAG_MEATBALL, FLAG_BLACK, FLAG_BLUE, FLAG_YELLOW, FLAG_WHITE, FLAG_GREEN };
    for (uint8_t bit : prio) {
      if (b.activeFlags & bit) {
        float s = 0.15f * b.config.scaleFlag;
        out.push_back({ b.config.posFlag.x, b.config.posFlag.y, s, s, FlagColor(bit), b.config.shape });
        break;
      }
    }
  }
  if (b.config.enableRadar) {
    for (uint32_t i = 0; i < b.carCount && i < MAX_CARS; ++i) {
      const CarBlip& c = b.cars[i];
      if (c.side == 3 || c.side == 0) continue;       // never render ahead / none
      float t = c.distance / (b.config.radarRange > 0 ? b.config.radarRange : 1);
      if (t > 1) t = 1;
      float bearing = std::atan2(c.rel.x, -c.rel.y);   // 0 = behind, +/- = sides
      float radius = 0.5f + 0.4f * t;
      float u = radius * std::sin(bearing);
      float v = -0.6f + radius * (std::cos(bearing) * 0.2f);
      float sz = (c.flags & 1) ? 0.05f : 0.03f;        // emphasize closest threat
      uint32_t col = (c.side == 4) ? 0xFFFFFFFFu : 0xFFFFC000u;
      out.push_back({ u, v, sz, sz, col, /*shape=radar*/255 });
    }
  }
}
