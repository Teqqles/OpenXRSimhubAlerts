#pragma once
#include "shm_contract.h"
#include "render/render_backend.h"
#include <vector>
void BuildOverlay(const DataBlock& b, std::vector<OverlayQuad>& out);
