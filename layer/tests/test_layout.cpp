// layer/tests/test_layout.cpp
#include <catch2/catch_test_macros.hpp>
#include "shm_contract.h"
#include <cstddef>

TEST_CASE("contract layout is pinned") {
  REQUIRE(sizeof(Vec2) == 8);
  REQUIRE(sizeof(CarBlip) == 16);
  REQUIRE(sizeof(Config) == 76);
  REQUIRE(offsetof(DataBlock, carCount) == 12);
  REQUIRE(offsetof(DataBlock, cars) == 16);
  REQUIRE(offsetof(DataBlock, config) == 16 + 16*MAX_CARS);
  // Pin the total; C# parity test asserts the same number.
  REQUIRE(sizeof(DataBlock) == 16 + 16*MAX_CARS + sizeof(Config));
}
