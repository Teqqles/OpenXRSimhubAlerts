#include "hooks.h"

// Temporary pass-through. Overlay session setup lands in a later task.
XrResult MyCreateSession(XrInstance instance, const XrSessionCreateInfo* i, XrSession* o) {
  return g_dispatch.createSession(instance, i, o);
}
