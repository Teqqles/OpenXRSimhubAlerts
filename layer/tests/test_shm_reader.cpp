// layer/tests/test_shm_reader.cpp
#include <catch2/catch_test_macros.hpp>
#include "shm_contract.h"
#include "shm_reader.h"
#include <windows.h>
#include <cstring>

TEST_CASE("reads a stable even-seq block") {
  HANDLE h = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
      0, sizeof(DataBlock), SHM_NAME);
  auto* p = (DataBlock*)MapViewOfFile(h, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(DataBlock));
  memset(p, 0, sizeof(DataBlock));
  p->version = SHM_VERSION; p->seq = 2; p->activeFlags = FLAG_YELLOW; p->carCount = 0;

  ShmReader r; REQUIRE(r.Open());
  DataBlock out{};
  REQUIRE(r.Read(out));
  REQUIRE(out.activeFlags == FLAG_YELLOW);
  REQUIRE(out.version == SHM_VERSION);

  UnmapViewOfFile(p); CloseHandle(h);
}

TEST_CASE("odd seq (writer in progress) never settles -> Read returns false") {
  HANDLE h = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
      0, sizeof(DataBlock), SHM_NAME);
  auto* p = (DataBlock*)MapViewOfFile(h, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(DataBlock));
  memset(p, 0, sizeof(DataBlock));
  p->version = SHM_VERSION; p->seq = 1; p->activeFlags = FLAG_RED; p->carCount = 0;

  ShmReader r; REQUIRE(r.Open());
  DataBlock out{};
  REQUIRE_FALSE(r.Read(out));

  UnmapViewOfFile(p); CloseHandle(h);
}
