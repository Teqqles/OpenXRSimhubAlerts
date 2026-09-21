#include <catch2/catch_test_macros.hpp>
#include "shm_contract.h"
#include "overlay.h"
#include <vector>

static DataBlock Base() {
  DataBlock b{}; b.version = SHM_VERSION; b.connected = 1;
  b.config.enableFlags = 1; b.config.enableRadar = 1;
  b.config.scaleFlag = 1; b.config.radarRange = 80;
  b.config.posFlag = { 0.8f, 0.8f };
  return b;
}

// Vertical extent (max-min y) of a triangle list -- proportional to blip size.
static float SpanY(const std::vector<OverlayVertex>& v) {
  if (v.empty()) return 0.0f;
  float lo = v[0].y, hi = v[0].y;
  for (auto& x : v) { lo = x.y < lo ? x.y : lo; hi = x.y > hi ? x.y : hi; }
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
  // Red (0xFFFF2020): strong red channel, weak green -- distinct from yellow.
  bool red = false;
  for (auto& v : g.leftEye) if (v.r > 0.5f && v.g < 0.3f) red = true;
  REQUIRE(red);
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

TEST_CASE("closest-threat blip is larger than a normal blip") {
  auto threat = Base(); threat.config.enableFlags = 0;
  threat.carCount = 1; threat.cars[0] = { {-3,0}, 3, 1, /*closest threat*/1, {0,0} };
  OverlayGeometry gt; BuildOverlay(threat, gt);

  auto normal = Base(); normal.config.enableFlags = 0;
  normal.carCount = 1; normal.cars[0] = { {-3,0}, 3, 1, /*not threat*/0, {0,0} };
  OverlayGeometry gn; BuildOverlay(normal, gn);

  REQUIRE(SpanY(gt.leftEye) > SpanY(gn.leftEye));
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
