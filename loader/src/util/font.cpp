#include "util/font.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace util::font {

namespace {

using Bytes = std::vector<uint8_t>;

uint32_t rd(const Bytes &b, size_t o) { uint32_t v; memcpy(&v, &b[o], 4); return v; }
void wr(Bytes &b, size_t o, uint32_t v) { memcpy(&b[o], &v, 4); }
float rdf(const Bytes &b, size_t o) { float v; memcpy(&v, &b[o], 4); return v; }
void wrf(Bytes &b, size_t o, float v) { memcpy(&b[o], &v, 4); }

} // namespace

size_t find_glyph(const Bytes &font, wchar_t code)
{
    if (font.size() < kGlyphs || memcmp(font.data(), "4.1v", 4) != 0) return 0;
    size_t def = rd(font, 0x1C);
    if (def + kGlyph > font.size()) return 0;
    for (size_t g = rd(font, kBuckets + (code & 0x7F) * 4); g >= kGlyphs && g < def; g += kGlyph)
        if ((rd(font, g + 0x1C) & 0xFFFF) == code) return g;
    return 0;
}

Patched add_icons(const Bytes &in, const std::vector<GreyAlpha> &icons, wchar_t first)
{
    Patched r;
    auto fail = [&](const char *why) {
        r.error = why;
        return r;
    };
    const int count = (int)icons.size();
    if (in.size() < kGlyphs || memcmp(in.data(), "4.1v", 4) != 0) return fail("not a font file");
    size_t tex = rd(in, 4), def = rd(in, 0x1C);
    if (def < kGlyphs || (def - kGlyphs) % kGlyph || def + kGlyph > tex || tex + kTexHeader > in.size()) return fail("unexpected layout");
    uint32_t fmt = rd(in, tex + 0xC);
    int w = 1 << ((fmt >> 20) & 15), h = 1 << ((fmt >> 24) & 15);
    if (rd(in, tex) != 0x00040001 || rd(in, tex + 4) != kTexHeader || ((fmt >> 8) & 0xFF) != 0x0E || ((fmt >> 16) & 15) != 1 ||
        rd(in, tex + 0x38) != (uint32_t)w || rd(in, tex + 0x3C) != (uint32_t)h || tex + kTexHeader + (size_t)w * h > in.size() || h >= 1 << 14)
        return fail("not a one-level DXT3 texture");
    // the A button's glyph: the icons' size and metrics
    size_t pad = 0;
    for (size_t g = kGlyphs; g <= def; g += kGlyph)
        if ((rd(in, g + 0x1C) & 0xFFFF) == kPadA) pad = g;
    if (!pad) return fail("no pad button glyph");
    int s = (int)std::lround(std::min(rdf(in, pad + 8) * w, rdf(in, pad + 12) * h));
    int per_row = (w - 4) / (s + 4);
    if (s < 8 || per_row < 1 || ((count + per_row - 1) / per_row) * (s + 4) + 4 > h) return fail("no room for the icons");

    // glyph table: the icons go just before the default glyph; everything from there on moves down
    const size_t added = count * kGlyph;
    size_t tex2 = (def + added + (tex - def) + 0x7F) & ~(size_t)0x7F, h2 = (size_t)h * 2;
    Bytes out(tex2 + kTexHeader + (size_t)w * h2, 0);
    memcpy(out.data(), in.data(), def);
    memcpy(&out[def + added], &in[def], tex - def);
    memcpy(&out[tex2], &in[tex], kTexHeader + (size_t)w * h);
    wr(out, 4, (uint32_t)tex2);
    wr(out, 0x1C, (uint32_t)(def + added));
    wrf(out, 0xC, rdf(in, 0xC) * 2);
    for (size_t b = 0; b < 128; b++) {
        uint32_t o = rd(in, kBuckets + b * 4);
        if (o >= def) wr(out, kBuckets + b * 4, o + (uint32_t)added);
    }
    for (size_t g = kGlyphs; g + kGlyph <= def + added + (tex - def); g += kGlyph) {
        if (g >= def && g < def + added) continue;
        if (rdf(out, g) >= 0) wrf(out, g + 4, rdf(out, g + 4) / 2); // not a blank (u = v = -1)
        wrf(out, g + 12, rdf(out, g + 12) / 2);
        wrf(out, g + 0x14, rdf(out, g + 0x14) / 2); // the y offset is in the same units as the height
    }
    // the texture: twice as tall, the icons in the new half
    wr(out, tex2 + 0xC, (fmt & ~(15u << 24)) | (uint32_t)((((fmt >> 24) & 15) + 1) << 24));
    wr(out, tex2 + 0x3C, (uint32_t)h2);
    std::vector<Rgba> half((size_t)w * h, Rgba{0, 0, 0, 0});
    for (int n = 0; n < count; n++) {
        int x0 = 4 + (n % per_row) * (s + 4), y0 = 4 + (n / per_row) * (s + 4);
        std::vector<Rgba> px = outlined(icons[n], s);
        for (int y = 0; y < s; y++)
            for (int x = 0; x < s; x++) half[(size_t)(y0 + y) * w + x0 + x] = px[y * s + x];
        size_t g = def + n * kGlyph;
        memcpy(&out[g], &in[pad], kGlyph);
        wrf(out, g, (float)x0 / w);
        wrf(out, g + 4, (float)(h + y0) / h2);
        wrf(out, g + 8, (float)s / w);
        wrf(out, g + 12, (float)s / h2);
        wrf(out, g + 0x14, rdf(in, pad + 0x14) / 2);
        wr(out, g + 0x1C, (uint32_t)(first + n));
        size_t b = kBuckets + ((first + n) & 0x7F) * 4;
        if (rd(out, b) == def + added) wr(out, b, (uint32_t)g); // an empty bucket starts at the icon
    }
    uint8_t *blocks = &out[tex2 + kTexHeader + (size_t)w * h];
    for (int by = 0; by < h; by += 4)
        for (int bx = 0; bx < w; bx += 4) {
            Rgba p[16];
            for (int i = 0; i < 16; i++) p[i] = half[(size_t)(by + i / 4) * w + bx + i % 4];
            dxt3_encode_block(p, blocks);
            blocks += 16;
        }
    r.data = std::move(out);
    r.icon_size = s;
    r.width = w;
    r.height = (int)h2;
    return r;
}

} // namespace util::font
