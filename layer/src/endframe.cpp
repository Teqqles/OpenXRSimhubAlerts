#include "hooks.h"

// Temporary pass-through. Overlay compositing lands in a later task.
XrResult MyEndFrame(XrSession s, const XrFrameEndInfo* i) {
  return g_dispatch.endFrame(s, i);
}
