// Vulkan overlay backend. Full implementation lands in Task 12; for now a stub
// factory keeps the DLL linking (session.cpp references the symbol) and
// returning nullptr means the overlay stays disabled for Vulkan apps.
#include "render_backend.h"

IRenderBackend* CreateVulkanBackend() { return nullptr; }
