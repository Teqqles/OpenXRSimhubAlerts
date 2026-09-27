// layer/tests/test_overlay.cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "shm_contract.h"
#include "overlay.h"
#include <vector>

// Flag and radar layout is tested on the plugin side (OverlayComposerTests.cs);
// these cover turning elements into per-eye triangles.

static DataBlock Block() {
  DataBlock b{}; b.version = SHM_VERSION; b.connected = 1;
  return b;
}

static Element& Add(DataBlock& b, uint8_t kind, uint8_t eyes, uint32_t color = 0xFFFFFFFFu,
                    uint8_t priority = 0, uint8_t flags = 0) {
  Element& e = b.elements[b.elementCount++];
  e = Element{};
  e.kind = kind; e.eyes = eyes; e.color = color; e.priority = priority; e.flags = flags;
  e.hw = 0.1f; e.hh = 0.1f;
  return e;
}

TEST_CASE("disconnected telemetry emits nothing (stale overlay clears)") {
  auto b = Block(); b.connected = 0;
  Add(b, ELEMENT_RECT, EYE_BOTH);
  OverlayGeometry g; BuildOverlay(b, g);
  REQUIRE(g.empty());
}

TEST_CASE("each kind emits its triangles") {
  auto rect = Block();     Add(rect, ELEMENT_RECT, EYE_LEFT);
  auto ellipse = Block();  Add(ellipse, ELEMENT_ELLIPSE, EYE_LEFT);
  auto tri = Block();      Add(tri, ELEMENT_TRIANGLE, EYE_LEFT);
  OverlayGeometry g;
  BuildOverlay(rect, g);    REQUIRE(g.leftEye.size() == 6);
  BuildOverlay(ellipse, g); REQUIRE(g.leftEye.size() == 72);
  BuildOverlay(tri, g);     REQUIRE(g.leftEye.size() == 3);
}

TEST_CASE("none, text and icon elements draw nothing yet") {
  auto b = Block();
  Add(b, ELEMENT_NONE, EYE_BOTH);
  Add(b, ELEMENT_TEXT, EYE_BOTH);
  Add(b, ELEMENT_ICON, EYE_BOTH);
  OverlayGeometry g; BuildOverlay(b, g);
  REQUIRE(g.empty());
}

TEST_CASE("elements draw only into the eyes in their mask") {
  auto b = Block();
  Add(b, ELEMENT_RECT, EYE_LEFT);
  Add(b, ELEMENT_TRIANGLE, EYE_RIGHT);
  Add(b, ELEMENT_RECT, EYE_BOTH);
  OverlayGeometry g; BuildOverlay(b, g);
  REQUIRE(g.leftEye.size() == 12);
  REQUIRE(g.rightEye.size() == 9);
}

TEST_CASE("rect spans centre plus and minus half-size") {
  auto b = Block();
  Element& e = Add(b, ELEMENT_RECT, EYE_LEFT);
  e.u = 0.5f; e.v = -0.25f; e.hw = 0.1f; e.hh = 0.2f;
  OverlayGeometry g; BuildOverlay(b, g);
  for (auto& v : g.leftEye) {
    REQUIRE((v.x == Catch::Approx(0.4f) || v.x == Catch::Approx(0.6f)));
    REQUIRE((v.y == Catch::Approx(-0.45f) || v.y == Catch::Approx(-0.05f)));
  }
}

TEST_CASE("triangle angle rotates the apex clockwise") {
  auto b = Block();
  Element& e = Add(b, ELEMENT_TRIANGLE, EYE_LEFT);
  e.hw = 0.1f; e.hh = 0.2f; e.angle = 1.5707963f;   // quarter turn: apex points right
  OverlayGeometry g; BuildOverlay(b, g);
  REQUIRE(g.leftEye[0].x == Catch::Approx(0.2f));
  REQUIRE(g.leftEye[0].y == Catch::Approx(0.0f).margin(1e-6));
}

TEST_CASE("colour decodes 0xAARRGGBB into straight RGBA") {
  auto b = Block();
  Add(b, ELEMENT_RECT, EYE_LEFT, 0x80FF4000u);
  OverlayGeometry g; BuildOverlay(b, g);
  const auto& v = g.leftEye[0];
  REQUIRE(v.r == Catch::Approx(1.0f));
  REQUIRE(v.g == Catch::Approx(64 / 255.0f));
  REQUIRE(v.b == Catch::Approx(0.0f));
  REQUIRE(v.a == Catch::Approx(128 / 255.0f));
}

TEST_CASE("higher priority draws later, equal priority keeps list order") {
  auto b = Block();
  Add(b, ELEMENT_TRIANGLE, EYE_LEFT, 0xFF0000FFu, /*priority*/200);  // blue on top
  Add(b, ELEMENT_TRIANGLE, EYE_LEFT, 0xFFFF0000u, 100);              // red first
  Add(b, ELEMENT_TRIANGLE, EYE_LEFT, 0xFF00FF00u, 100);              // then green
  OverlayGeometry g; BuildOverlay(b, g);
  REQUIRE(g.leftEye.size() == 9);
  REQUIRE(g.leftEye[0].r == 1.0f);
  REQUIRE(g.leftEye[3].g == 1.0f);
  REQUIRE(g.leftEye[6].b == 1.0f);
}

TEST_CASE("element count is clamped to the contract capacity") {
  auto b = Block();
  for (uint32_t i = 0; i < MAX_ELEMENTS; ++i) Add(b, ELEMENT_TRIANGLE, EYE_LEFT);
  b.elementCount = MAX_ELEMENTS + 50;   // corrupt count must not read past the array
  OverlayGeometry g; BuildOverlay(b, g);
  REQUIRE(g.leftEye.size() == 3 * MAX_ELEMENTS);
}

TEST_CASE("time-critical signature counts per eye and ignores movement") {
  auto b = Block();
  Element& blip = Add(b, ELEMENT_RECT, EYE_LEFT, 0xFFFF0000u, 100, ELEMENT_TIME_CRITICAL);
  Add(b, ELEMENT_RECT, EYE_BOTH, 0xFFFFFFFFu, 200);   // not time-critical
  const uint32_t before = TimeCriticalSignature(b);

  blip.u = 0.7f; blip.color = 0x40FF0000u;           // moves and fades
  REQUIRE(TimeCriticalSignature(b) == before);

  blip.eyes = EYE_BOTH;                               // now also in the right eye
  REQUIRE(TimeCriticalSignature(b) != before);
}

TEST_CASE("time-critical signature changes when a blip appears or telemetry drops") {
  auto b = Block();
  const uint32_t empty = TimeCriticalSignature(b);
  Add(b, ELEMENT_RECT, EYE_RIGHT, 0xFFFF0000u, 100, ELEMENT_TIME_CRITICAL);
  const uint32_t one = TimeCriticalSignature(b);
  REQUIRE(one != empty);
  b.connected = 0;
  REQUIRE(TimeCriticalSignature(b) == empty);
}

TEST_CASE("glow fades from the element colour at the centre to transparent at the rim") {
  auto b = Block();
  Add(b, ELEMENT_GLOW, EYE_LEFT, 0x80FF0000u);
  OverlayGeometry g; BuildOverlay(b, g);
  REQUIRE(g.leftEye.size() == 72);   // 24 segment fan
  for (size_t i = 0; i < g.leftEye.size(); ++i) {
    const auto& p = g.leftEye[i];
    REQUIRE(p.r == Catch::Approx(1.0f));
    REQUIRE(p.g == Catch::Approx(0.0f));
    if (i % 3 == 0) REQUIRE(p.a == Catch::Approx(0x80 / 255.0f));   // centre
    else            REQUIRE(p.a == 0.0f);                           // rim
  }
}
