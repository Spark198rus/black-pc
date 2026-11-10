// The game's font files (Language\Fonts\Small.bin / Big.bin, "4.1v") and adding icon glyphs to them.
//
// Layout: +4 offset of the texture, +8 five floats (+8 / +0xC: the texture's width / height over the design size),
// +0x1C offset of the default glyph (the last one), +0x20 128 offsets, one per (code & 0x7F): the first glyph of that
// bucket, or the default glyph. From +0x220 the glyphs, 0x28 bytes each, sorted by bucket: u, v, width, height (of the
// texture, 0..1), x / y offset, advance (floats), the code (UTF-16, compared as a word) and two more dwords. The game's
// lookup starts at the bucket's glyph and goes on through the following glyphs until the code matches or it reaches
// the default glyph, so a glyph added just before the default one is always found. The size of the file is not fixed.
// The texture: a D3D resource (0x80 bytes: +4 data offset 0x80, +0xC format word, +0x38 width, +0x3C height), then
// DXT3 data (format 0x0E, one level). The pad buttons are glyphs U+00B0..
//
// Adding icons doubles the texture's height (the original blocks are kept as they are; every glyph's v, height and y
// offset are halved and the height float doubled, so the text is drawn the same) and puts the icons in the new half,
// as glyphs the size of the A button's (U+00B0) with its offsets and advance.
#pragma once

#include "util/image.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace util::font {

constexpr size_t kGlyph = 0x28, kBuckets = 0x20, kGlyphs = 0x220, kTexHeader = 0x80;
constexpr wchar_t kPadA = 0xB0;

struct Patched {
    std::vector<uint8_t> data; // empty on failure
    std::string error;         // why it failed
    int icon_size = 0, width = 0, height = 0; // the icons' pixels; the new texture
};

// The font with one glyph per icon added as first, first + 1, ...
Patched add_icons(const std::vector<uint8_t> &in, const std::vector<GreyAlpha> &icons, wchar_t first);

// The offset of the glyph the game draws for code (as its lookup finds it), or 0 when only the default glyph matches.
size_t find_glyph(const std::vector<uint8_t> &font, wchar_t code);

} // namespace util::font
