#include "atlas.h"
#include "embedded_assets.h"
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstring>

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#define STBI_ONLY_PNG
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace {

constexpr int kEmPx = 64;                          // glyph em size in atlas texels
constexpr int kPad = 6;                            // distance field spread, texels
constexpr unsigned char kOnEdge = 128;
constexpr float kPerTexel = float(kOnEdge) / kPad; // field value change per texel
constexpr int kIconPx = 128;
constexpr int kSolid = 4, kGap = 1;

// Left-to-right shelves, top to bottom. The first shelf starts after the solid block.
struct Packer {
  int x = kSolid + kGap, y = 0, rowH = kSolid;
  bool Place(int w, int h, int& px, int& py) {
    if (x + w > kAtlasSize) { y += rowH + kGap; x = 0; rowH = 0; }
    if (w > kAtlasSize || y + h > kAtlasSize) return false;
    px = x; py = y;
    x += w + kGap;
    rowH = std::max(rowH, h);
    return true;
  }
};

void Blit(Atlas& a, const uint8_t* src, int w, int h, int px, int py) {
  for (int row = 0; row < h; ++row)
    std::memcpy(&a.pixels[size_t(py + row) * kAtlasSize + px], src + size_t(row) * w, size_t(w));
}

void SetRect(AtlasEntry& e, int px, int py, int w, int h) {
  e.u0 = float(px) / kAtlasSize;     e.v0 = float(py) / kAtlasSize;
  e.u1 = float(px + w) / kAtlasSize; e.v1 = float(py + h) / kAtlasSize;
}

Atlas SolidOnly() {
  Atlas a;
  a.pixels.assign(size_t(kAtlasSize) * kAtlasSize, 0);
  for (int y = 0; y < kSolid; ++y)
    for (int x = 0; x < kSolid; ++x) a.pixels[size_t(y) * kAtlasSize + x] = 255;
  return a;
}

// Distance field of a coverage mask (inside where alpha >= 128), padded by kPad on
// every side. Brute force over a small window: icons are 128 px, so this is cheap.
std::vector<uint8_t> MaskSdf(const unsigned char* rgba, int n) {
  auto inside = [&](int x, int y) {
    return x >= 0 && y >= 0 && x < n && y < n && rgba[(size_t(y) * n + x) * 4 + 3] >= 128;
  };
  const int size = n + 2 * kPad, reach = kPad + 1;
  std::vector<uint8_t> out(size_t(size) * size);
  for (int oy = 0; oy < size; ++oy) {
    for (int ox = 0; ox < size; ++ox) {
      const int x = ox - kPad, y = oy - kPad;
      const bool in = inside(x, y);
      int best = INT_MAX;
      for (int dy = -reach; dy <= reach; ++dy)
        for (int dx = -reach; dx <= reach; ++dx)
          if (inside(x + dx, y + dy) != in) best = std::min(best, dx * dx + dy * dy);
      const float dist = best == INT_MAX ? float(reach) : std::sqrt(float(best)) - 0.5f;
      const float value = kOnEdge + (in ? dist : -dist) * kPerTexel;
      out[size_t(oy) * size + ox] = uint8_t(std::clamp(value, 0.0f, 255.0f));
    }
  }
  return out;
}

bool AddGlyphs(Atlas& a, Packer& pack, const AtlasSources& src) {
  stbtt_fontinfo font;
  if (!src.font || src.fontSize < 12) return false;
  // stbtt_GetFontOffsetForIndex returns -1 for data with no recognisable font
  // signature. stbtt_InitFont takes that offset unsigned, so passing -1 through
  // wraps to a huge value and reads far outside the buffer; reject it here.
  const int offset = stbtt_GetFontOffsetForIndex(src.font, 0);
  if (offset < 0 || !stbtt_InitFont(&font, src.font, offset)) return false;
  const float scale = stbtt_ScaleForMappingEmToPixels(&font, float(kEmPx));
  for (int c = kFirstGlyph; c <= kLastGlyph; ++c) {
    AtlasEntry& g = a.glyphs[c - kFirstGlyph];
    int adv = 0, lsb = 0;
    stbtt_GetCodepointHMetrics(&font, c, &adv, &lsb);
    g.advance = adv * scale / kEmPx;
    int w = 0, h = 0, xoff = 0, yoff = 0;
    unsigned char* sdf = stbtt_GetCodepointSDF(&font, scale, c, kPad, kOnEdge, kPerTexel,
                                               &w, &h, &xoff, &yoff);
    if (!sdf) continue;   // no outline (space)
    int px = 0, py = 0;
    const bool placed = pack.Place(w, h, px, py);
    if (placed) Blit(a, sdf, w, h, px, py);
    stbtt_FreeSDF(sdf, nullptr);
    if (!placed) return false;
    SetRect(g, px, py, w, h);
    g.x0 = float(xoff) / kEmPx;       g.x1 = float(xoff + w) / kEmPx;
    g.y0 = float(-(yoff + h)) / kEmPx; g.y1 = float(-yoff) / kEmPx;
    g.present = true;
  }
  return true;
}

bool AddIcons(Atlas& a, Packer& pack, const AtlasSources& src) {
  const float edge = 1.0f + 2.0f * kPad / kIconPx;   // padding in box half-extents
  for (int i = 0; i < ICON_COUNT; ++i) {
    if (!src.icons[i] || src.iconSizes[i] == 0) return false;
    int w = 0, h = 0, channels = 0;
    unsigned char* rgba = stbi_load_from_memory(src.icons[i], int(src.iconSizes[i]),
                                                &w, &h, &channels, 4);
    if (!rgba) return false;
    const bool square = w == kIconPx && h == kIconPx;
    std::vector<uint8_t> sdf;
    if (square) sdf = MaskSdf(rgba, kIconPx);
    stbi_image_free(rgba);
    if (!square) return false;
    const int size = kIconPx + 2 * kPad;
    int px = 0, py = 0;
    if (!pack.Place(size, size, px, py)) return false;
    Blit(a, sdf.data(), size, size, px, py);
    AtlasEntry& e = a.icons[i];
    SetRect(e, px, py, size, size);
    e.x0 = -edge; e.x1 = edge; e.y0 = -edge; e.y1 = edge;
    e.present = true;
  }
  return true;
}

}  // namespace

Atlas BuildAtlas(const AtlasSources& src) {
  Atlas a = SolidOnly();
  Packer pack;
  if (AddGlyphs(a, pack, src) && AddIcons(a, pack, src)) {
    a.ok = true;
    return a;
  }
  return SolidOnly();
}

const Atlas& OverlayAtlas() {
  static const Atlas atlas = BuildAtlas(EmbeddedAtlasSources());
  return atlas;
}
