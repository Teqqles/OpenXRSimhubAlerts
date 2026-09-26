#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "shm_contract.h"
#include "overlay.h"
#include <vector>
#include <cmath>
#include <algorithm>

static DataBlock Base() {
  DataBlock b{}; b.version = SHM_VERSION; b.connected = 1;
  b.config.enableFlags = 1; b.config.enableRadar = 1;
  b.config.scaleFlag = 1; b.config.radarRange = 80;
  b.config.posFlag = { 0.8f, 0.8f };
  return b;
}

// Horizontal extent (max-min x) of a triangle list; proportional to blip width.
static float SpanX(const std::vector<OverlayVertex>& v) {
  if (v.empty()) return 0.0f;
  float lo = v[0].x, hi = v[0].x;
  for (auto& x : v) { lo = x.x < lo ? x.x : lo; hi = x.x > hi ? x.x : hi; }
  return hi - lo;
}

TEST_CASE("disconnected telemetry emits nothing (stale overlay clears)") {
  auto b = Base(); b.connected = 0;
  b.activeFlags = FLAG_RED;
  b.carCount = 1; b.cars[0] = { {3,0}, 3, 2, 1, {0,0} };
  OverlayGeometry g; BuildOverlay(b, g);
  REQUIRE(g.empty());
}

TEST_CASE("flag priority picks red over yellow, in both eyes") {
  auto b = Base(); b.activeFlags = FLAG_RED | FLAG_YELLOW;
  OverlayGeometry g; BuildOverlay(b, g);
  REQUIRE_FALSE(g.leftEye.empty());
  REQUIRE_FALSE(g.rightEye.empty());
  // Red (0xFFFF2020): strong red channel and weak green, unlike yellow.
  bool red = false;
  for (auto& v : g.leftEye) if (v.r > 0.5f && v.g < 0.3f) red = true;
  REQUIRE(red);
}

TEST_CASE("meatball flag is a black flag with an orange centre dot") {
  auto b = Base(); b.config.enableRadar = 0;
  b.config.shape = 3;                 // circle marker (not the edge bar)
  b.activeFlags = FLAG_MEATBALL;
  OverlayGeometry g; BuildOverlay(b, g);
  REQUIRE_FALSE(g.leftEye.empty());
  bool black = false, orange = false;
  for (auto& v : g.leftEye) {
    if (v.r < 0.1f && v.g < 0.1f && v.b < 0.1f) black = true;             // 0x101010 body
    if (v.r > 0.8f && v.g > 0.3f && v.g < 0.7f && v.b < 0.1f) orange = true; // 0xFF8000 dot
  }
  REQUIRE(black);
  REQUIRE(orange);
}

TEST_CASE("cars ahead are not rendered") {
  auto b = Base(); b.config.enableFlags = 0;
  b.carCount = 1; b.cars[0] = { {0,10}, 10, /*side ahead*/3, 0, {0,0} };
  OverlayGeometry g; BuildOverlay(b, g);
  REQUIRE(g.empty());
}

TEST_CASE("radar disabled emits nothing") {
  auto b = Base(); b.config.enableRadar = 0; b.config.enableFlags = 0;
  b.carCount = 1; b.cars[0] = { {3,0}, 3, 2, 1, {0,0} };
  OverlayGeometry g; BuildOverlay(b, g);
  REQUIRE(g.empty());
}

TEST_CASE("left-side car draws only in the left eye") {
  auto b = Base(); b.config.enableFlags = 0;
  b.carCount = 1; b.cars[0] = { {-3,0}, 3, /*side left*/1, 0, {0,0} };
  OverlayGeometry g; BuildOverlay(b, g);
  REQUIRE_FALSE(g.leftEye.empty());
  REQUIRE(g.rightEye.empty());
}

TEST_CASE("right-side car draws only in the right eye") {
  auto b = Base(); b.config.enableFlags = 0;
  b.carCount = 1; b.cars[0] = { {3,0}, 3, /*side right*/2, 0, {0,0} };
  OverlayGeometry g; BuildOverlay(b, g);
  REQUIRE(g.leftEye.empty());
  REQUIRE_FALSE(g.rightEye.empty());
}

TEST_CASE("car behind draws in both eyes (stereo)") {
  auto b = Base(); b.config.enableFlags = 0;
  b.carCount = 1; b.cars[0] = { {0,-3}, 3, /*side behind*/4, 0, {0,0} };
  OverlayGeometry g; BuildOverlay(b, g);
  REQUIRE_FALSE(g.leftEye.empty());
  REQUIRE_FALSE(g.rightEye.empty());
}

TEST_CASE("blip size is uniform regardless of the closest-threat flag") {
  auto threat = Base(); threat.config.enableFlags = 0;
  threat.carCount = 1; threat.cars[0] = { {-3,0}, 3, 1, /*closest threat*/1, {0,0} };
  OverlayGeometry gt; BuildOverlay(threat, gt);

  auto normal = Base(); normal.config.enableFlags = 0;
  normal.carCount = 1; normal.cars[0] = { {-3,0}, 3, 1, /*not threat*/0, {0,0} };
  OverlayGeometry gn; BuildOverlay(normal, gn);

  // Size no longer encodes threat; colour and opacity alone show closeness.
  REQUIRE(SpanX(gt.leftEye) == Catch::Approx(SpanX(gn.leftEye)));
}

TEST_CASE("radar blips sit at the lens edge, never inside the car") {
  auto b = Base(); b.config.enableFlags = 0;
  b.carCount = 1; b.cars[0] = { {-3,0}, 3, /*left*/1, 0, {0,0} };
  OverlayGeometry g; BuildOverlay(b, g);
  REQUIRE_FALSE(g.leftEye.empty());
  // Every vertex hugs the left edge (well outside centre), none near the car.
  for (auto& v : g.leftEye) REQUIRE(v.x < -0.7f);
}

TEST_CASE("behind car sits at bottom-center of both eyes") {
  auto b = Base(); b.config.enableFlags = 0;
  b.carCount = 1; b.cars[0] = { {0,-3}, 3, /*behind*/4, 0, {0,0} };
  OverlayGeometry g; BuildOverlay(b, g);
  REQUIRE_FALSE(g.leftEye.empty());
  REQUIRE_FALSE(g.rightEye.empty());
  // bearing 0 => u=0, v=-R: bottom-center, centered horizontally, in both eyes.
  for (auto& v : g.leftEye)  { REQUIRE(v.y < -0.7f); REQUIRE(std::abs(v.x) < 0.2f); }
  for (auto& v : g.rightEye) { REQUIRE(v.y < -0.7f); REQUIRE(std::abs(v.x) < 0.2f); }
}

TEST_CASE("directly-left car rides the outer rim, vertically centered") {
  auto b = Base(); b.config.enableFlags = 0;
  b.carCount = 1; b.cars[0] = { {-3,0}, 3, /*left*/1, 0, {0,0} };
  OverlayGeometry g; BuildOverlay(b, g);
  REQUIRE_FALSE(g.leftEye.empty());
  // Ring is centered on the lens middle: a due-left car sits at v~0, not the
  // old bottom bias.
  for (auto& v : g.leftEye) REQUIRE(std::abs(v.y) < 0.2f);
}

TEST_CASE("radar radius clamp keeps blips on-lens at large scaleRadar") {
  auto b = Base(); b.config.enableFlags = 0;
  b.config.scaleRadar = 5.0f;   // unclamped R would be 4.5, all off-screen
  b.carCount = 1; b.cars[0] = { {-3,0}, 3, 1, 0, {0,0} };
  OverlayGeometry g; BuildOverlay(b, g);
  REQUIRE_FALSE(g.leftEye.empty());
  // At least part of the blip stays within the visible lens.
  float maxX = -2.0f;
  for (auto& v : g.leftEye) maxX = std::max(maxX, v.x);
  REQUIRE(maxX > -1.0f);
}

TEST_CASE("radar blips are shades of red") {
  auto b = Base(); b.config.enableFlags = 0;
  b.carCount = 1; b.cars[0] = { {-3,0}, 3, 1, 0, {0,0} };
  OverlayGeometry g; BuildOverlay(b, g);
  REQUIRE_FALSE(g.leftEye.empty());
  for (auto& v : g.leftEye) { REQUIRE(v.r > 0.2f); REQUIRE(v.g < 0.05f); REQUIRE(v.b < 0.05f); }
}

TEST_CASE("radar max opacity caps blip alpha") {
  auto b = Base(); b.config.enableFlags = 0;
  b.config.radarMaxOpacity = 0.5f;
  // Far car (t~1) => closeness~0 => alpha ~= ceil*0.35, well under the cap.
  b.carCount = 1; b.cars[0] = { {0,-80}, 80, 4, 0, {0,0} };
  OverlayGeometry g; BuildOverlay(b, g);
  REQUIRE_FALSE(g.leftEye.empty());
  for (auto& v : g.leftEye) REQUIRE(v.a <= 0.5f + 1e-4f);
}
