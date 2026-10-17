// Bigger weapon-data buffers. The game carves two 0xE1800-byte (923,648) buffers out of its main arena
// (init function at 0x1F49F0); a weapon .db file larger than that does not load. Known build only.
#pragma once

// After image_map. Allocates 2 x g_cfg.buf_mb MB of contiguous memory (at the top of RAM, so everything else
// keeps its physical address) and points the two buffers there. Returns false if the code does not match.
bool install_bigbuf_patch();
