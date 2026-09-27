#include "embedded_assets.h"
#include "asset_ids.h"
// RT_RCDATA expands through MAKEINTRESOURCE, which picks the wide overload only
// when UNICODE is defined; this project doesn't define it project-wide, so set
// it locally to keep FindResourceW's LPCWSTR argument well typed.
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>

namespace {

// The module (DLL or test exe) this code is linked into, so resources resolve in
// the right image whichever process loaded it.
HMODULE ThisModule() {
  HMODULE m = nullptr;
  GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                     reinterpret_cast<LPCWSTR>(&ThisModule), &m);
  return m;
}

const uint8_t* Resource(int id, size_t& size) {
  size = 0;
  HMODULE m = ThisModule();
  HRSRC r = m ? FindResourceW(m, MAKEINTRESOURCEW(id), RT_RCDATA) : nullptr;
  HGLOBAL h = r ? LoadResource(m, r) : nullptr;
  const void* p = h ? LockResource(h) : nullptr;
  if (!p) return nullptr;
  size = SizeofResource(m, r);
  return static_cast<const uint8_t*>(p);
}

}  // namespace

AtlasSources EmbeddedAtlasSources() {
  AtlasSources s{};
  s.font = Resource(IDR_FONT, s.fontSize);
  for (int i = 0; i < ICON_COUNT; ++i) s.icons[i] = Resource(IDR_ICON_BASE + i, s.iconSizes[i]);
  return s;
}
