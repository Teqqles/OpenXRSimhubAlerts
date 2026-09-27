// layer/tests/test_shm_reader.cpp
#include <catch2/catch_test_macros.hpp>
#include "shm_contract.h"
#include "shm_reader.h"
#include <windows.h>
#include <cstring>

// A private mapping name, so tests never touch the live one SimHub writes.
static const char* kTestName = "OpenXRSimHubAlertsReaderTest";

struct TestMapping {
  HANDLE h;
  DataBlock* p;
  explicit TestMapping(DWORD size = sizeof(DataBlock)) {
    h = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, size, kTestName);
    p = (DataBlock*)MapViewOfFile(h, FILE_MAP_ALL_ACCESS, 0, 0, size);
    memset(p, 0, size);
  }
  ~TestMapping() { UnmapViewOfFile(p); CloseHandle(h); }
};

TEST_CASE("reads a stable even-seq block") {
  TestMapping m;
  m.p->version = SHM_VERSION; m.p->seq = 2; m.p->connected = 1; m.p->elementCount = 1;
  m.p->elements[0].kind = ELEMENT_ELLIPSE;

  ShmReader r(kTestName);
  DataBlock out{};
  REQUIRE(r.Read(out));
  REQUIRE(out.elementCount == 1);
  REQUIRE(out.elements[0].kind == ELEMENT_ELLIPSE);
}

TEST_CASE("odd seq (writer in progress) never settles -> Read returns false") {
  TestMapping m;
  m.p->version = SHM_VERSION; m.p->seq = 1;

  ShmReader r(kTestName);
  DataBlock out{};
  REQUIRE_FALSE(r.Read(out));
}

TEST_CASE("a mapping smaller than DataBlock (older plugin) reads false") {
  TestMapping m(64);

  ShmReader r(kTestName);
  DataBlock out{};
  REQUIRE_FALSE(r.Read(out));
  REQUIRE_FALSE(r.Read(out));   // retrying must not leak the mapping handle
}
