#pragma once
#include "shm_contract.h"
#include "render/render_backend.h"
// Build the stereo overlay geometry (per-eye triangle lists) for one frame.
void BuildOverlay(const DataBlock& b, OverlayGeometry& out);
