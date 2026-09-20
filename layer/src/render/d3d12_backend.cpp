// Direct3D 12 overlay backend. Full implementation lands in Task 11; for now a
// stub factory keeps the DLL linking (session.cpp references the symbol) and
// returning nullptr means the overlay stays disabled for D3D12 apps.
#include "render_backend.h"

IRenderBackend* CreateD3D12Backend() { return nullptr; }
