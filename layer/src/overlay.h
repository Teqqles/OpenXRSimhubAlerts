#pragma once
#include "shm_contract.h"
#include "render/render_backend.h"

// Turns the element list into per-eye triangle lists for one frame. Elements draw
// lowest priority first; nothing draws while telemetry is disconnected.
void BuildOverlay(const DataBlock& b, OverlayGeometry& out);

// Per-eye count of time-critical elements (0 while disconnected). A change
// between frames means one appeared or disappeared.
uint32_t TimeCriticalSignature(const DataBlock& b);
