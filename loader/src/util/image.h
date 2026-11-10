// Small image helpers for the mouse icons (fe/fonts.cpp): scaling, the outline, DXT3 blocks.
#pragma once

#include <cstdint>
#include <vector>

namespace util {

struct Rgba {
    uint8_t r, g, b, a;
};

// A square grey + alpha image: size x size pixels, 2 bytes each.
struct GreyAlpha {
    int size;
    const uint8_t *px;
};

// The image scaled to s x s pixels (area average, the grey weighted by alpha).
std::vector<Rgba> scale_area(const GreyAlpha &src, int s);
// The image for an s x s cell: a little smaller, over a white outline max(1, s / 24) pixels wide around it.
std::vector<Rgba> outlined(const GreyAlpha &src, int s);

// One DXT3 block (16 bytes) for 4x4 pixels in row order: explicit 4-bit alpha, the colours between the darkest and
// the brightest opaque pixel.
void dxt3_encode_block(const Rgba *p, uint8_t *out);
void dxt3_decode_block(const uint8_t *in, Rgba *p);

} // namespace util
