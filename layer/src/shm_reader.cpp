#include "shm_reader.h"
#include <cstring>
bool ShmReader::Open() noexcept {
  _h = OpenFileMappingA(FILE_MAP_READ, FALSE, SHM_NAME);
  if (!_h) return false;
  _p = (volatile DataBlock*)MapViewOfFile(_h, FILE_MAP_READ, 0, 0, sizeof(DataBlock));
  return _p != nullptr;
}
bool ShmReader::Read(DataBlock& out) noexcept {
  if (!_p) { if (!Open()) return false; }
  for (int i = 0; i < 4; ++i) {
    uint32_t s1 = _p->seq;
    if (s1 & 1u) continue;                     // writer in progress
    memcpy(&out, (const void*)_p, sizeof(DataBlock));
    uint32_t s2 = _p->seq;
    if (s1 == s2 && !(s2 & 1u)) return out.version == SHM_VERSION;
  }
  return false;
}
void ShmReader::Close() noexcept {
  if (_p) { UnmapViewOfFile((void*)_p); _p = nullptr; }
  if (_h) { CloseHandle(_h); _h = nullptr; }
}
