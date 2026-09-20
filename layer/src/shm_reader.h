#pragma once
#include "shm_contract.h"
#include <windows.h>
class ShmReader {
public:
  bool Open() noexcept;
  bool Read(DataBlock& out) noexcept;
  void Close() noexcept;
  ~ShmReader() { Close(); }
private:
  HANDLE _h = nullptr;
  volatile DataBlock* _p = nullptr;
};
