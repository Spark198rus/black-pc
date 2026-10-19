// Interpreter for NV2A methods that game code writes straight into the push buffer (inlined XDK code):
// immediate-mode vertices (SET_BEGIN_END + SET_VERTEX_DATA*), inline arrays (BeginPush/EndPush). Other
// state goes through HLE functions, so only these methods are expected; anything else is logged once.
#pragma once

#include "common.h"

namespace xd3d {

void pb_init(uint32_t *base);
void pb_flush();  // interpret everything between the last flush and the device's PUT pointer
void pb_reset();  // rewind PUT to the start of the buffer (after a flush)
void pb_end_primitive(); // D3DDevice_End: same as SET_BEGIN_END(0)

} // namespace xd3d
