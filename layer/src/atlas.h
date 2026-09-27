#pragma once
#include "shm_contract.h"
#include <cstddef>
#include <cstdint>
#include <vector>

// One R8 signed distance field texture holding the font's printable ASCII glyphs,
// every icon, and a solid block that plain shapes sample. Values: 128 on an
// outline, higher inside, lower outside; 6 px of spread either side.
constexpr int kAtlasSize = 1024;
// Centre of the 4 x 4 block of 255 at the atlas origin: shapes sample here so the
// distance field shader leaves their vertex alpha untouched.
constexpr float kSolidU = 2.0f / kAtlasSize, kSolidV = 2.0f / kAtlasSize;
constexpr int kFirstGlyph = 32, kLastGlyph = 126;

struct AtlasEntry {
  float u0 = 0, v0 = 0, u1 = 0, v1 = 0;   // texture rectangle, v down
  // Quad corners, y up. Glyphs: em units from the pen position on the baseline.
  // Icons: half-extents of the element box (the icon's own square is -1..1).
  float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
  float advance = 0;                       // glyphs: pen advance in em
  bool present = false;                    // false: nothing to draw (space, or no atlas)
};

struct Atlas {
  std::vector<uint8_t> pixels;             // kAtlasSize x kAtlasSize, row 0 at the top
  AtlasEntry glyphs[kLastGlyph - kFirstGlyph + 1];
  AtlasEntry icons[ICON_COUNT];
  bool ok = false;                         // false: only the solid block is usable
};

struct AtlasSources {
  const uint8_t* font = nullptr;
  size_t fontSize = 0;
  const uint8_t* icons[ICON_COUNT] = {};
  size_t iconSizes[ICON_COUNT] = {};
};

// Builds the atlas. Any failure (bad font, missing or undecodable icon, no room)
// returns a solid-block-only atlas with ok false, so shapes still draw.
Atlas BuildAtlas(const AtlasSources& src);

// The process-wide atlas, built once from the embedded assets on first use.
const Atlas& OverlayAtlas();
