#include "quad_fit.h"
#include <cmath>

namespace {

// Tangents beyond this (about 79 degrees) mean the runtime sent garbage.
constexpr float kMaxTan = 5.0f;
constexpr float kMinSpan = 0.1f;

Vec3f Cross(const Vec3f& a, const Vec3f& b) {
  return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
}

Vec3f Rotate(const Quatf& q, const Vec3f& v) {
  const Vec3f u{q.x, q.y, q.z};
  const Vec3f c = Cross(u, v);
  const Vec3f t{2 * c.x, 2 * c.y, 2 * c.z};
  const Vec3f ut = Cross(u, t);
  return { v.x + q.w * t.x + ut.x, v.y + q.w * t.y + ut.y, v.z + q.w * t.z + ut.z };
}

bool Plausible(float tan) { return std::isfinite(tan) && std::fabs(tan) <= kMaxTan; }

}  // namespace

bool FitQuadToEye(const EyeView& eye, float distance, QuadPlacement& out) {
  const float left = std::tan(eye.angleLeft), right = std::tan(eye.angleRight);
  const float up = std::tan(eye.angleUp), down = std::tan(eye.angleDown);
  if (!Plausible(left) || !Plausible(right) || !Plausible(up) || !Plausible(down)) return false;
  if (right - left < kMinSpan || up - down < kMinSpan) return false;

  // Centre of the frustum's cross-section at `distance`, in the eye's own axes.
  const Vec3f centre{ distance * (left + right) / 2, distance * (up + down) / 2, -distance };
  const Vec3f offset = Rotate(eye.orientation, centre);

  out.orientation = eye.orientation;
  out.position = { eye.position.x + offset.x, eye.position.y + offset.y, eye.position.z + offset.z };
  out.width  = distance * (right - left);
  out.height = distance * (up - down);
  return true;
}
