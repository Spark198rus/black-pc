// The mouse button icons (by fez, loader/res/mouse, tools/gen_mouse_icons.py) added to the game's two fonts, so the
// prompts can show them the way they show the pad buttons (input/prompts.cpp).
//
// Language\Fonts\Small.bin / Big.bin ("4.1v"): +4 offset of the texture, +8 five floats (+8 / +0xC: the texture's
// width / height over the design size: 128/15, 256/15 in Small), +0x1C offset of the default glyph (the last one),
// +0x20 128 offsets, one per (code & 0x7F): the first glyph of that bucket, or the default glyph. From +0x220 the
// glyphs, 0x28 bytes each, sorted by bucket: u, v, width, height (of the texture, 0..1), x / y offset, advance (floats),
// the code (UTF-16, compared as a word) and two more dwords. The lookup (FUN_000C3770 and the text code) starts at
// the bucket's glyph and goes on through the following glyphs until the code matches or it reaches the default glyph,
// so a glyph added just before the default one is always found. The font loader's callback (FUN_001FF510) relocates
// these offsets and registers the texture (FUN_000B2940); the size of the file is not fixed.
// The texture: a D3D resource (0x80 bytes: +4 data offset 0x80, +0xC format word, +0x34 format, +0x38 width, +0x3C
// height), then DXT3 data (format 0x0E, one level, 128x256 in Small, 256x256 in Big). The pad buttons are glyphs
// U+00B0.. (25x25 pixels in Small, 48x48 in Big).
//
// The copy doubles the texture's height (the original blocks are kept as they are; every glyph's v, height and y
// offset are halved and the height float doubled, so the text is drawn the same) and puts the icons in the new half, as glyphs
// U+E000.. the size of the pad buttons with their offsets and advance.
#include "fe/fonts.h"

#include "common.h"
#include "input/bindings.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <vector>

namespace {

#include "fe/mouse_icons.inc"

using Bytes = std::vector<uint8_t>;

constexpr wchar_t kFirstIcon = 0xE000; // left, right, middle, wheel up, wheel down (the .inc's order)
constexpr size_t kGlyph = 0x28, kBuckets = 0x20, kGlyphs = 0x220, kTexHeader = 0x80;
bool g_icons;

uint32_t rd(const Bytes &b, size_t o) { uint32_t v; memcpy(&v, &b[o], 4); return v; }
void wr(Bytes &b, size_t o, uint32_t v) { memcpy(&b[o], &v, 4); }
float rdf(const Bytes &b, size_t o) { float v; memcpy(&v, &b[o], 4); return v; }
void wrf(Bytes &b, size_t o, float v) { memcpy(&b[o], &v, 4); }

bool read_file(const std::wstring &path, Bytes &out)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), {});
    return true;
}

bool write_file(const std::wstring &path, const Bytes &data)
{
    for (size_t i = path.find(L'\\', 3); i != std::wstring::npos; i = path.find(L'\\', i + 1))
        CreateDirectoryW(path.substr(0, i).c_str(), nullptr);
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f.write((const char *)data.data(), (std::streamsize)data.size());
    f.close();
    return (bool)f;
}

struct Rgba {
    uint8_t r, g, b, a;
};

// One icon scaled to s x s pixels (area average, weighted by alpha).
std::vector<Rgba> scaled(int n, int s)
{
    std::vector<Rgba> out(s * s);
    const unsigned char *px = kMouseIconPixels[n];
    double step = (double)kMouseIconSize / s;
    for (int oy = 0; oy < s; oy++)
        for (int ox = 0; ox < s; ox++) {
            double ga = 0, a = 0, area = 0;
            for (int y = (int)(oy * step); y < (int)std::ceil((oy + 1) * step) && y < kMouseIconSize; y++)
                for (int x = (int)(ox * step); x < (int)std::ceil((ox + 1) * step) && x < kMouseIconSize; x++) {
                    double wy = std::min<double>(y + 1, (oy + 1) * step) - std::max<double>(y, oy * step);
                    double wx = std::min<double>(x + 1, (ox + 1) * step) - std::max<double>(x, ox * step);
                    double w = wx * wy, al = px[(y * kMouseIconSize + x) * 2 + 1];
                    ga += w * al * px[(y * kMouseIconSize + x) * 2];
                    a += w * al;
                    area += w;
                }
            uint8_t g = a > 0 ? (uint8_t)std::lround(ga / a) : 0;
            out[oy * s + ox] = {g, g, g, (uint8_t)std::lround(area > 0 ? a / area : 0)};
        }
    return out;
}

// One icon for an s x s cell: the icon a little smaller, over a white outline r pixels wide around it. The game
// tints text (green hints), and the outline keeps the black mouse visible on dark scenes.
std::vector<Rgba> icon(int n, int s)
{
    int r = std::max(1, s / 24), inner = s - 2 * r;
    std::vector<Rgba> px = scaled(n, inner), out(s * s, Rgba{255, 255, 255, 0});
    for (int y = 0; y < s; y++)
        for (int x = 0; x < s; x++) {
            int edge = 0; // the outline: the most opaque icon pixel within r
            for (int dy = -r; dy <= r; dy++)
                for (int dx = -r; dx <= r; dx++) {
                    int ix = x - r + dx, iy = y - r + dy;
                    if (dx * dx + dy * dy <= r * r + r && ix >= 0 && iy >= 0 && ix < inner && iy < inner)
                        edge = std::max<int>(edge, px[iy * inner + ix].a);
                }
            int ix = x - r, iy = y - r;
            Rgba p = ix >= 0 && iy >= 0 && ix < inner && iy < inner ? px[iy * inner + ix] : Rgba{0, 0, 0, 0};
            int a = p.a + edge * (255 - p.a) / 255; // the icon over the outline
            uint8_t g = a ? (uint8_t)((p.r * p.a + 255 * edge * (255 - p.a) / 255) / a) : 255;
            out[y * s + x] = {g, g, g, (uint8_t)a};
        }
    return out;
}

uint16_t to565(int r, int g, int b) { return (uint16_t)(((r * 31 + 127) / 255) << 11 | ((g * 63 + 127) / 255) << 5 | (b * 31 + 127) / 255); }
void from565(uint16_t c, int *rgb)
{
    rgb[0] = (c >> 11) * 255 / 31;
    rgb[1] = ((c >> 5) & 63) * 255 / 63;
    rgb[2] = (c & 31) * 255 / 31;
}

// One DXT3 block (16 bytes) for 4x4 pixels: explicit 4-bit alpha, the colours between the darkest and the brightest.
void dxt3_block(const Rgba *p, uint8_t *out)
{
    uint64_t alpha = 0;
    for (int i = 0; i < 16; i++) alpha |= (uint64_t)((p[i].a * 15 + 127) / 255) << (4 * i);
    memcpy(out, &alpha, 8);
    int lo = -1, hi = -1;
    for (int i = 0; i < 16; i++) {
        if (!p[i].a) continue;
        int l = p[i].r * 299 + p[i].g * 587 + p[i].b * 114;
        if (lo < 0 || l < p[lo].r * 299 + p[lo].g * 587 + p[lo].b * 114) lo = i;
        if (hi < 0 || l > p[hi].r * 299 + p[hi].g * 587 + p[hi].b * 114) hi = i;
    }
    uint16_t c0 = hi < 0 ? 0 : to565(p[hi].r, p[hi].g, p[hi].b), c1 = lo < 0 ? 0 : to565(p[lo].r, p[lo].g, p[lo].b);
    if (c0 < c1) std::swap(c0, c1);
    int pal[4][3];
    from565(c0, pal[0]);
    from565(c1, pal[1]);
    for (int k = 0; k < 3; k++) {
        pal[2][k] = (2 * pal[0][k] + pal[1][k]) / 3;
        pal[3][k] = (pal[0][k] + 2 * pal[1][k]) / 3;
    }
    uint32_t bits = 0;
    for (int i = 0; i < 16 && c0 != c1; i++) {
        int best = 0, bd = 1 << 30;
        for (int j = 0; j < 4; j++) {
            int dr = p[i].r - pal[j][0], dg = p[i].g - pal[j][1], db = p[i].b - pal[j][2];
            int d = dr * dr + dg * dg + db * db;
            if (d < bd) bd = d, best = j;
        }
        bits |= (uint32_t)best << (2 * i);
    }
    memcpy(out + 8, &c0, 2);
    memcpy(out + 10, &c1, 2);
    memcpy(out + 12, &bits, 4);
}

// The font with the icons, or an empty result (logged) when the file is not laid out as expected.
Bytes add_icons(const Bytes &in, const char *name)
{
    auto fail = [&](const char *why) {
        logf("fonts: %s: %s; no mouse icons", name, why);
        return Bytes();
    };
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
        if ((rd(in, g + 0x1C) & 0xFFFF) == 0xB0) pad = g;
    if (!pad) return fail("no pad button glyph");
    int s = (int)std::lround(std::min(rdf(in, pad + 8) * w, rdf(in, pad + 12) * h));
    int per_row = (w - 4) / (s + 4);
    if (s < 8 || per_row < 1 || ((kMouseIconCount + per_row - 1) / per_row) * (s + 4) + 4 > h) return fail("no room for the icons");

    // glyph table: the icons go just before the default glyph; everything from there on moves down
    const size_t added = kMouseIconCount * kGlyph;
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
    for (int n = 0; n < kMouseIconCount; n++) {
        int x0 = 4 + (n % per_row) * (s + 4), y0 = 4 + (n / per_row) * (s + 4);
        std::vector<Rgba> px = icon(n, s);
        for (int y = 0; y < s; y++)
            for (int x = 0; x < s; x++) half[(size_t)(y0 + y) * w + x0 + x] = px[y * s + x];
        size_t g = def + n * kGlyph;
        memcpy(&out[g], &in[pad], kGlyph);
        wrf(out, g, (float)x0 / w);
        wrf(out, g + 4, (float)(h + y0) / h2);
        wrf(out, g + 8, (float)s / w);
        wrf(out, g + 12, (float)s / h2);
        wrf(out, g + 0x14, rdf(in, pad + 0x14) / 2);
        wr(out, g + 0x1C, kFirstIcon + n);
        size_t b = kBuckets + ((kFirstIcon + n) & 0x7F) * 4;
        if (rd(out, b) == def + added) wr(out, b, (uint32_t)g); // an empty bucket starts at the icon
    }
    uint8_t *blocks = &out[tex2 + kTexHeader + (size_t)w * h];
    for (int by = 0; by < h; by += 4)
        for (int bx = 0; bx < w; bx += 4) {
            Rgba p[16];
            for (int i = 0; i < 16; i++) p[i] = half[(size_t)(by + i / 4) * w + bx + i % 4];
            dxt3_block(p, blocks);
            blocks += 16;
        }
    logf("fonts: %s: %d mouse icons (%dx%d) added as U+%04X.., texture %dx%d", name, kMouseIconCount, s, s,
         kFirstIcon, w, (int)h2);
    return out;
}

} // namespace

bool fe_build_fonts(const std::wstring &game_dir, const std::wstring &out_root)
{
    bool ok = true;
    for (const wchar_t *file : {L"Small.bin", L"Big.bin"}) {
        std::wstring rel = std::wstring(L"\\language\\fonts\\") + file, dst = out_root + rel;
        Bytes in, out;
        if (!read_file(game_dir + rel, in)) {
            logf("fonts: %s not found; no mouse icons", narrow(game_dir + rel).c_str());
            ok = false;
        } else if ((out = add_icons(in, narrow(file).c_str())).empty() || !write_file(dst, out)) {
            ok = false;
        }
        if (!ok) break;
    }
    if (!ok) // the game's own fonts for both, so the prompts never ask for a glyph one of them lacks
        for (const wchar_t *file : {L"Small.bin", L"Big.bin"}) DeleteFileW((out_root + L"\\language\\fonts\\" + file).c_str());
    g_icons = ok;
    return ok;
}

wchar_t fe_mouse_icon(uint16_t key)
{
    if (!g_icons) return 0;
    switch (key) {
    case VK_LBUTTON: return kFirstIcon;
    case VK_RBUTTON: return kFirstIcon + 1;
    case VK_MBUTTON: return kFirstIcon + 2;
    case bindings::KEY_WHEEL_UP: return kFirstIcon + 3;
    case bindings::KEY_WHEEL_DOWN: return kFirstIcon + 4;
    }
    return 0;
}
