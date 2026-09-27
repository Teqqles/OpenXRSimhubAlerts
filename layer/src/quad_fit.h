#pragma once

// Fits the overlay quad to one eye's view frustum, so overlay NDC -1..+1 spans
// exactly what that eye renders, even for asymmetric or canted FOVs. Pure maths
// with plain types (fields match XrQuaternionf, XrVector3f and XrFovf) so it
// is testable without OpenXR.

struct Vec3f { float x, y, z; };
struct Quatf { float x, y, z, w; };

// One eye from xrLocateViews in view space. Angles in radians; left and down
// are negative.
struct EyeView {
  Quatf orientation;
  Vec3f position;
  float angleLeft, angleRight, angleUp, angleDown;
};

// Quad pose (view space) and full size in metres.
struct QuadPlacement {
  Quatf orientation;
  Vec3f position;
  float width, height;
};

// Places the quad `distance` metres along the eye's view. Returns false for a
// degenerate or implausible FOV, leaving `out` unchanged.
bool FitQuadToEye(const EyeView& eye, float distance, QuadPlacement& out);
