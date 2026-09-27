#pragma once
#include "shm_contract.h"
#include "render/render_backend.h"
#include "atlas.h"

// Width of the anti-aliasing feather across every shape outline: 1.5 texels of
// the kEyeDim eye texture, in NDC. Half lies inside the outline, half outside,
// so feathering does not change a shape's apparent size.
constexpr float kFeatherNdc = 1.5f * 2.0f / kEyeDim;
// Horizontal NDC of the head's straight-ahead direction in each eye (ForwardNdcU).
// Zero until the runtime FOV is known.
struct EyeAnchors { float leftU = 0.0f, rightU = 0.0f; };

// Turns the element list into per-eye triangle lists for one frame. Elements draw
// lowest priority first; nothing draws while telemetry is disconnected.
// Forward-anchored elements are shifted by that eye's anchor. Text and icon
// elements draw only with an atlas that is ok.
void BuildOverlay(const DataBlock& b, OverlayGeometry& out, const EyeAnchors& anchors = EyeAnchors{},
                  const Atlas* atlas = nullptr);

// Per-eye count of time-critical elements (0 while disconnected). A change
// between frames means one appeared or disappeared.
uint32_t TimeCriticalSignature(const DataBlock& b);
