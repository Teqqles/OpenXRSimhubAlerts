// layer/tests/test_layout.cpp
#include <catch2/catch_test_macros.hpp>
#include "shm_contract.h"
#include <cstddef>

// plugin.tests/LayoutParityTests.cs pins the same numbers on the C# side.
TEST_CASE("contract layout is pinned") {
  REQUIRE(sizeof(Element) == 32);
  REQUIRE(offsetof(Element, u) == 4);
  REQUIRE(offsetof(Element, angle) == 20);
  REQUIRE(offsetof(Element, color) == 24);
  REQUIRE(offsetof(Element, ref) == 28);

  REQUIRE(offsetof(DataBlock, refreshMode) == 9);
  REQUIRE(offsetof(DataBlock, elementCount) == 12);
  REQUIRE(offsetof(DataBlock, elements) == 16);
  REQUIRE(sizeof(DataBlock) == 16 + 32 * MAX_ELEMENTS);
}

TEST_CASE("contract version and kinds are pinned") {
  REQUIRE(SHM_VERSION == 5u);
  REQUIRE(ELEMENT_GLOW == 6);
  REQUIRE(ELEMENT_FORWARD_ANCHORED == 2);
}
