#include "shm_reader.h"
#include <atomic>
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
    // Seqlock ordering: the pre-fence keeps the payload copy from being
    // hoisted above the s1 read; the post-fence keeps the s2 read from being
    // sunk below the copy. Explicit acquire fences rather than relying on x86
    // TSO, so the protocol is correct regardless of target memory model.
    std::atomic_thread_fence(std::memory_order_acquire);
    memcpy(&out, (const void*)_p, sizeof(DataBlock));
    std::atomic_thread_fence(std::memory_order_acquire);
    uint32_t s2 = _p->seq;
    if (s1 == s2 && !(s2 & 1u)) return out.version == SHM_VERSION;
  }
  return false;
}
void ShmReader::Close() noexcept {
  if (_p) { UnmapViewOfFile((void*)_p); _p = nullptr; }
  if (_h) { CloseHandle(_h); _h = nullptr; }
}
