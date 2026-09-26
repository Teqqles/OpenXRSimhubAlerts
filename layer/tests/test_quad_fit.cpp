// layer/tests/test_quad_fit.cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "quad_fit.h"
#include <cmath>

namespace {

constexpr float kDeg = 3.14159265f / 180.0f;
const Quatf kIdentity{0, 0, 0, 1};

EyeView Eye(float left, float right, float up, float down) {
  return EyeView{kIdentity, {0, 0, 0}, left * kDeg, right * kDeg, up * kDeg, down * kDeg};
}

}  // namespace

TEST_CASE("symmetric FOV centres the quad on the view axis", "[quad]") {
  QuadPlacement q;
  REQUIRE(FitQuadToEye(Eye(-45, 45, 45, -45), 1.0f, q));
  REQUIRE(q.position.x == Catch::Approx(0.0f).margin(1e-6));
  REQUIRE(q.position.y == Catch::Approx(0.0f).margin(1e-6));
  REQUIRE(q.position.z == Catch::Approx(-1.0f));
  REQUIRE(q.width == Catch::Approx(2.0f));
  REQUIRE(q.height == Catch::Approx(2.0f));
}

TEST_CASE("quad edges land on each edge of an asymmetric FOV", "[quad]") {
  // Quest 3 left eye via Virtual Desktop.
  QuadPlacement q;
  REQUIRE(FitQuadToEye(Eye(-54, 40, 44, -55), 1.0f, q));
  REQUIRE(q.position.y + q.height / 2 == Catch::Approx(std::tan(44 * kDeg)));
  REQUIRE(q.position.y - q.height / 2 == Catch::Approx(std::tan(-55 * kDeg)));
  REQUIRE(q.position.x - q.width / 2 == Catch::Approx(std::tan(-54 * kDeg)));
  REQUIRE(q.position.x + q.width / 2 == Catch::Approx(std::tan(40 * kDeg)));
}

TEST_CASE("quad scales with distance", "[quad]") {
  QuadPlacement near, far;
  REQUIRE(FitQuadToEye(Eye(-54, 40, 44, -55), 1.0f, near));
  REQUIRE(FitQuadToEye(Eye(-54, 40, 44, -55), 2.0f, far));
  REQUIRE(far.width == Catch::Approx(2 * near.width));
  REQUIRE(far.position.y == Catch::Approx(2 * near.position.y));
  REQUIRE(far.position.z == Catch::Approx(-2.0f));
}

TEST_CASE("eye position offsets the quad", "[quad]") {
  EyeView eye = Eye(-45, 45, 45, -45);
  eye.position = {-0.032f, 0.01f, 0.0f};   // left eye, half an IPD from centre
  QuadPlacement q;
  REQUIRE(FitQuadToEye(eye, 1.0f, q));
  REQUIRE(q.position.x == Catch::Approx(-0.032f));
  REQUIRE(q.position.y == Catch::Approx(0.01f));
}

TEST_CASE("canted eye rotates the quad with it", "[quad]") {
  // Display yawed 10 degrees outward (left), as on canted-display headsets.
  const float half = 5 * kDeg;
  EyeView eye = Eye(-45, 45, 45, -45);
  eye.orientation = {0, std::sin(half), 0, std::cos(half)};
  QuadPlacement q;
  REQUIRE(FitQuadToEye(eye, 1.0f, q));
  REQUIRE(q.position.x == Catch::Approx(-std::sin(10 * kDeg)));
  REQUIRE(q.position.z == Catch::Approx(-std::cos(10 * kDeg)));
  REQUIRE(q.orientation.y == Catch::Approx(eye.orientation.y));
  REQUIRE(q.orientation.w == Catch::Approx(eye.orientation.w));
}

TEST_CASE("degenerate or garbage FOV is rejected", "[quad]") {
  QuadPlacement q;
  REQUIRE_FALSE(FitQuadToEye(Eye(0, 0, 0, 0), 1.0f, q));
  REQUIRE_FALSE(FitQuadToEye(Eye(40, -40, 40, -40), 1.0f, q));   // edges swapped
  REQUIRE_FALSE(FitQuadToEye(Eye(-45, 45, NAN, -45), 1.0f, q));
  REQUIRE_FALSE(FitQuadToEye(Eye(-89.9f, 89.9f, 89.9f, -89.9f), 1.0f, q));   // absurdly wide
}
