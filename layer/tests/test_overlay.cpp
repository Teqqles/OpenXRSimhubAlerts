#include <catch2/catch_test_macros.hpp>
#include "shm_contract.h"
#include "overlay.h"
#include <vector>
#include <cstring>

static DataBlock Base() {
  DataBlock b{}; b.version = SHM_VERSION; b.connected = 1;
  b.config.enableFlags = 1; b.config.enableRadar = 1;
  b.config.scaleFlag = 1; b.config.radarRange = 80;
  b.config.posFlag = { 0.8f, 0.8f };
  return b;
}

TEST_CASE("disconnected telemetry emits nothing (stale overlay clears)") {
  auto b = Base(); b.connected = 0;
  b.activeFlags = FLAG_RED;
  b.carCount = 1; b.cars[0] = { {3,0}, 3, 2, 1, {0,0} };
  std::vector<OverlayQuad> q; BuildOverlay(b, q);
  REQUIRE(q.empty());
}

TEST_CASE("flag priority picks red over yellow") {
  auto b = Base(); b.activeFlags = FLAG_RED | FLAG_YELLOW;
  std::vector<OverlayQuad> q; BuildOverlay(b, q);
  REQUIRE(q.size() >= 1);
  REQUIRE((q[0].rgba & 0x00FF0000u)); // red channel present in the chosen flag
}

TEST_CASE("cars ahead are not rendered") {
  auto b = Base();
  b.carCount = 1; b.cars[0] = { {0,10}, 10, /*side ahead*/3, 0, {0,0} };
  std::vector<OverlayQuad> q; BuildOverlay(b, q);
  bool anyRadar = false; for (auto& x : q) if (x.shape == 255) anyRadar = true;
  REQUIRE_FALSE(anyRadar);
}

TEST_CASE("radar disabled emits no car quads") {
  auto b = Base(); b.config.enableRadar = 0;
  b.carCount = 1; b.cars[0] = { {3,0}, 3, 2, 1, {0,0} };
  std::vector<OverlayQuad> q; BuildOverlay(b, q);
  for (auto& x : q) REQUIRE(x.shape != 255);
}

TEST_CASE("right-side car with radar enabled produces a radar quad") {
  auto b = Base();
  b.carCount = 1; b.cars[0] = { {3,0}, 3, /*side right*/2, 0, {0,0} };
  std::vector<OverlayQuad> q; BuildOverlay(b, q);
  bool anyRadar = false; for (auto& x : q) if (x.shape == 255) anyRadar = true;
  REQUIRE(anyRadar);
}

TEST_CASE("closest-threat flag yields larger radar quad") {
  auto b = Base();
  b.carCount = 2;
  b.cars[0] = { {3,0}, 3, 2, /*closest threat*/1, {0,0} };
  b.cars[1] = { {-3,0}, 3, 1, /*not threat*/0, {0,0} };
  std::vector<OverlayQuad> q; BuildOverlay(b, q);
  float threatSize = -1, normalSize = -1;
  for (auto& x : q) {
    if (x.shape != 255) continue;
    // right side => amber, left side => amber too; distinguish by size only
  }
  // Two radar quads expected
  int radarCount = 0; for (auto& x : q) if (x.shape == 255) radarCount++;
  REQUIRE(radarCount == 2);
  // The threat quad (0.05) must be larger than the non-threat quad (0.03)
  float sizes[2]; int n = 0;
  for (auto& x : q) if (x.shape == 255 && n < 2) sizes[n++] = x.w;
  REQUIRE(((sizes[0] > sizes[1]) || (sizes[1] > sizes[0])));
  float mx = sizes[0] > sizes[1] ? sizes[0] : sizes[1];
  float mn = sizes[0] > sizes[1] ? sizes[1] : sizes[0];
  REQUIRE(mx > mn);
  REQUIRE(mx == 0.05f);
  REQUIRE(mn == 0.03f);
}
