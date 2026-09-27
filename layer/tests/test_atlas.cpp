#include <catch2/catch_test_macros.hpp>
#include "atlas.h"
#include "embedded_assets.h"
#include <algorithm>
#include <fstream>
#include <string>
#include <vector>

static uint8_t At(const Atlas& a, float u, float v) {
  const int x = static_cast<int>(u * kAtlasSize), y = static_cast<int>(v * kAtlasSize);
  return a.pixels[static_cast<size_t>(y) * kAtlasSize + x];
}

static const AtlasEntry& Glyph(const Atlas& a, char c) { return a.glyphs[c - kFirstGlyph]; }

TEST_CASE("the embedded font and every icon are present") {
  const AtlasSources s = EmbeddedAtlasSources();
  REQUIRE(s.font != nullptr);
  REQUIRE(s.fontSize == 420428);
  for (int i = 0; i < ICON_COUNT; ++i) {
    REQUIRE(s.icons[i] != nullptr);
    REQUIRE(s.iconSizes[i] > 8);
    REQUIRE(s.icons[i][1] == 'P');   // PNG signature
  }
}

TEST_CASE("the atlas holds every printable glyph and every icon") {
  const Atlas& a = OverlayAtlas();
  REQUIRE(a.ok);
  REQUIRE(a.pixels.size() == size_t(kAtlasSize) * kAtlasSize);
  for (char c = '!'; c <= '~'; ++c) {
    const AtlasEntry& g = Glyph(a, c);
    REQUIRE(g.present);
    REQUIRE(g.u1 > g.u0);
    REQUIRE(g.v1 > g.v0);
    REQUIRE(g.x1 > g.x0);
    REQUIRE(g.y1 > g.y0);
    REQUIRE(g.advance > 0.1f);
  }
  for (int i = 0; i < ICON_COUNT; ++i) {
    REQUIRE(a.icons[i].present);
    REQUIRE(a.icons[i].x0 < -1.0f);   // padding extends past the box
    REQUIRE(a.icons[i].x1 > 1.0f);
  }
}

TEST_CASE("space has an advance but nothing to draw") {
  const AtlasEntry& g = Glyph(OverlayAtlas(), ' ');
  REQUIRE_FALSE(g.present);
  REQUIRE(g.advance > 0.1f);
}

TEST_CASE("the solid block is fully inside") {
  const Atlas& a = OverlayAtlas();
  for (int y = 0; y < 4; ++y)
    for (int x = 0; x < 4; ++x) REQUIRE(a.pixels[size_t(y) * kAtlasSize + x] == 255);
  REQUIRE(At(a, kSolidU, kSolidV) == 255);
}

TEST_CASE("glyph distance fields are inside at a stroke and outside at the padding") {
  const Atlas& a = OverlayAtlas();
  const AtlasEntry& g = Glyph(a, 'I');   // a single vertical stroke
  REQUIRE(At(a, (g.u0 + g.u1) / 2, (g.v0 + g.v1) / 2) > 128);
  REQUIRE(At(a, g.u0 + 0.5f / kAtlasSize, g.v0 + 0.5f / kAtlasSize) < 128);
}

TEST_CASE("digits sit on the baseline and rise about three quarters of an em") {
  const AtlasEntry& g = Glyph(OverlayAtlas(), '0');
  REQUIRE(g.y0 < 0.0f);                 // padding dips below the baseline
  REQUIRE(g.y0 > -0.2f);
  REQUIRE(g.y1 > 0.7f);
  REQUIRE(g.y1 < 1.0f);
}

TEST_CASE("icon distance fields are inside at the centre of a filled shape") {
  const Atlas& a = OverlayAtlas();
  const AtlasEntry& fuel = a.icons[ICON_FUEL];   // solid pump body left of centre
  const float u = fuel.u0 + (fuel.u1 - fuel.u0) * 0.4f;
  const float v = fuel.v0 + (fuel.v1 - fuel.v0) * 0.6f;
  REQUIRE(At(a, u, v) > 128);
  REQUIRE(At(a, fuel.u0 + 0.5f / kAtlasSize, fuel.v0 + 0.5f / kAtlasSize) < 128);
}

TEST_CASE("a broken font leaves only the solid block") {
  AtlasSources s = EmbeddedAtlasSources();
  const uint8_t junk[16] = {};
  s.font = junk; s.fontSize = sizeof(junk);
  const Atlas a = BuildAtlas(s);
  REQUIRE_FALSE(a.ok);
  REQUIRE(a.pixels.size() == size_t(kAtlasSize) * kAtlasSize);
  REQUIRE(a.pixels[0] == 255);
  REQUIRE_FALSE(a.glyphs['A' - kFirstGlyph].present);
  REQUIRE_FALSE(a.icons[ICON_ABS].present);
}

TEST_CASE("a missing icon leaves only the solid block") {
  AtlasSources s = EmbeddedAtlasSources();
  s.icons[ICON_TC] = nullptr; s.iconSizes[ICON_TC] = 0;
  const Atlas a = BuildAtlas(s);
  REQUIRE_FALSE(a.ok);
  REQUIRE_FALSE(a.glyphs['A' - kFirstGlyph].present);
}

// assets.rc.in assigns the icon resource ids as literal numbers (200 to 205)
// in IconId order, so nothing but this test checks that id 200 + i is really
// the icon for IconId i. Compares each embedded icon's bytes against the file
// it is meant to come from, read straight off disk.
TEST_CASE("embedded icons match the shared asset files in IconId order") {
  static const char* kNames[ICON_COUNT] = {"fuel", "abs", "tc", "drs", "shift_up", "shift_down"};
  const AtlasSources s = EmbeddedAtlasSources();
  for (int i = 0; i < ICON_COUNT; ++i) {
    const std::string path = std::string(SHARED_ASSETS_DIR) + "/icons/" + kNames[i] + ".png";
    std::ifstream file(path, std::ios::binary);
    REQUIRE(file.good());
    const std::vector<uint8_t> onDisk((std::istreambuf_iterator<char>(file)),
                                       std::istreambuf_iterator<char>());
    REQUIRE(s.icons[i] != nullptr);
    REQUIRE(s.iconSizes[i] == onDisk.size());
    REQUIRE(std::equal(onDisk.begin(), onDisk.end(), s.icons[i]));
  }
}
