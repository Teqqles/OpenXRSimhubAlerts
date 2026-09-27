#pragma once
#include "shm_contract.h"
#include "render/render_backend.h"

// Horizontal NDC of the head's straight-ahead direction in each eye (ForwardNdcU).
// Zero until the runtime FOV is known.
struct EyeAnchors { float leftU = 0.0f, rightU = 0.0f; };

// Turns the element list into per-eye triangle lists for one frame. Elements draw
// lowest priority first; nothing draws while telemetry is disconnected.
// Forward-anchored elements are shifted by that eye's anchor.
void BuildOverlay(const DataBlock& b, OverlayGeometry& out, const EyeAnchors& anchors = EyeAnchors{});

// Per-eye count of time-critical elements (0 while disconnected). A change
// between frames means one appeared or disappeared.
uint32_t TimeCriticalSignature(const DataBlock& b);
