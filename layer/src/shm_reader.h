#pragma once
#include "shm_contract.h"
#include <windows.h>
class ShmReader {
public:
  ShmReader() = default;
  // Owns a Win32 HANDLE + mapped view; copying/moving would risk a double
  // CloseHandle/UnmapViewOfFile. SessionState holds one by value in a node-based
  // map (never relocated), so non-copyable/non-movable is safe and correct.
  ShmReader(const ShmReader&)            = delete;
  ShmReader& operator=(const ShmReader&) = delete;
  ShmReader(ShmReader&&)                 = delete;
  ShmReader& operator=(ShmReader&&)      = delete;
  bool Open() noexcept;
  bool Read(DataBlock& out) noexcept;
  void Close() noexcept;
  ~ShmReader() { Close(); }
private:
  HANDLE _h = nullptr;
  volatile DataBlock* _p = nullptr;
};
