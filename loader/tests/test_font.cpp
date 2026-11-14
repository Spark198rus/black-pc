#include "check.h"

#include "util/files.h"
#include "util/font.h"
#include "util/text.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace font = util::font;
using Bytes = std::vector<uint8_t>;
using util::GreyAlpha;
using util::Rgba;

namespace {

uint32_t rd(const Bytes &b, size_t o) { uint32_t v; memcpy(&v, &b[o], 4); return v; }
float rdf(const Bytes &b, size_t o) { float v; memcpy(&v, &b[o], 4); return v; }
void wr(Bytes &b, size_t o, uint32_t v) { memcpy(&b[o], &v, 4); }
void wrf(Bytes &b, size_t o, float v) { memcpy(&b[o], &v, 4); }

struct Glyph {
    wchar_t code;
    float u, v, w, h, xoff, yoff, adv;
};

// A font laid out like the game's: the glyphs sorted by bucket, the default glyph last, then the texture
// (w x h DXT3 with a recognisable byte pattern). The A button glyph is pad x pad pixels.
Bytes make_font(int w = 64, int h = 64, int pad = 16, bool with_pad = true)
{
    std::vector<Glyph> glyphs = {
        {L' ', -1, -1, 0, 4.f / h, 0, 3, 5},                                  // a blank: u = v = -1
        {L'A', 0, 0.25f, 10.f / w, 12.f / h, 1, 2, 10},                       // bucket 0x41
    };
    if (with_pad) glyphs.insert(glyphs.begin() + 1, {font::kPadA, 0.5f, 0, (float)pad / w, (float)pad / h, 0, 4, 18}); // bucket 0x30
    size_t def = font::kGlyphs + glyphs.size() * font::kGlyph;
    size_t tex = (def + font::kGlyph + 0x7F) & ~(size_t)0x7F;
    Bytes f(tex + font::kTexHeader + (size_t)w * h, 0);
    memcpy(f.data(), "4.1v", 4);
    wr(f, 4, (uint32_t)tex);
    wrf(f, 8, w / 15.f);
    wrf(f, 0xC, h / 15.f);
    wr(f, 0x1C, (uint32_t)def);
    for (size_t b = 0; b < 128; b++) wr(f, font::kBuckets + b * 4, (uint32_t)def);
    for (size_t i = 0; i < glyphs.size(); i++) {
        const Glyph &g = glyphs[i];
        size_t o = font::kGlyphs + i * font::kGlyph;
        float v[7] = {g.u, g.v, g.w, g.h, g.xoff, g.yoff, g.adv};
        for (int k = 0; k < 7; k++) wrf(f, o + k * 4, v[k]);
        wr(f, o + 0x1C, g.code);
        wr(f, font::kBuckets + (g.code & 0x7F) * 4, (uint32_t)o);
    }
    wr(f, def + 0x1C, 0xFFFF); // the default glyph
    wr(f, tex, 0x00040001);
    wr(f, tex + 4, (uint32_t)font::kTexHeader);
    int lw = 0, lh = 0;
    while ((1 << lw) < w) lw++;
    while ((1 << lh) < h) lh++;
    wr(f, tex + 0xC, 0x0E << 8 | 1 << 16 | lw << 20 | lh << 24);
    wr(f, tex + 0x38, (uint32_t)w);
    wr(f, tex + 0x3C, (uint32_t)h);
    for (size_t i = 0; i < (size_t)w * h; i++) f[tex + font::kTexHeader + i] = (uint8_t)(i * 7 + 3);
    return f;
}

// Five solid black, opaque icons.
std::vector<GreyAlpha> black_icons()
{
    static std::vector<uint8_t> px = [] {
        std::vector<uint8_t> p(32 * 32 * 2, 0);
        for (size_t i = 1; i < p.size(); i += 2) p[i] = 255;
        return p;
    }();
    return std::vector<GreyAlpha>(5, GreyAlpha{32, px.data()});
}

// One pixel of the patched texture.
Rgba pixel(const Bytes &f, int x, int y)
{
    size_t tex = rd(f, 4);
    int w = (int)rd(f, tex + 0x38);
    const uint8_t *block = &f[tex + font::kTexHeader + ((size_t)(y / 4) * (w / 4) + x / 4) * 16];
    Rgba p[16];
    util::dxt3_decode_block(block, p);
    return p[(y % 4) * 4 + x % 4];
}

} // namespace

TEST(font_lookup_finds_the_glyphs)
{
    Bytes f = make_font();
    CHECK(font::find_glyph(f, L'A') != 0);
    CHECK(font::find_glyph(f, L' ') != 0);
    CHECK(font::find_glyph(f, font::kPadA) != 0);
    CHECK_EQ(font::find_glyph(f, L'B'), 0u);
    CHECK_EQ(font::find_glyph(f, 0xC1), 0u); // same bucket as 'A', other code
}

TEST(font_icons_texture_and_header)
{
    Bytes in = make_font();
    font::Patched r = font::add_icons(in, black_icons(), 0xE000);
    REQUIRE(!r.data.empty());
    const Bytes &f = r.data;
    CHECK(r.error.empty());
    CHECK_EQ(r.icon_size, 16);
    CHECK_EQ(r.width, 64);
    CHECK_EQ(r.height, 128);
    size_t tex = rd(f, 4), tex_in = rd(in, 4);
    CHECK_EQ(tex % 0x80, 0u);
    CHECK_EQ(rd(f, 0x1C), rd(in, 0x1C) + 5 * font::kGlyph);
    CHECK_EQ(rdf(f, 8), rdf(in, 8));
    CHECK_EQ(rdf(f, 0xC), rdf(in, 0xC) * 2);
    CHECK_EQ(rd(f, tex + 0x3C), 128u);
    CHECK_EQ((rd(f, tex + 0xC) >> 24) & 15, 7u);
    CHECK_EQ(rd(f, tex + 0xC) & 0x00FFFFFF, rd(in, tex_in + 0xC) & 0x00FFFFFF);
    // the original blocks are the top half, byte for byte
    CHECK(memcmp(&f[tex + font::kTexHeader], &in[tex_in + font::kTexHeader], 64 * 64) == 0);
    CHECK_EQ(f.size(), tex + font::kTexHeader + 64u * 128);
}

TEST(font_existing_glyphs_draw_the_same)
{
    Bytes in = make_font();
    font::Patched r = font::add_icons(in, black_icons(), 0xE000);
    REQUIRE(!r.data.empty());
    for (wchar_t c : {L'A', L' ', (wchar_t)font::kPadA}) {
        size_t a = font::find_glyph(in, c), b = font::find_glyph(r.data, c);
        REQUIRE(a && b);
        bool blank = rdf(in, a) < 0;
        CHECK_EQ(rdf(r.data, b), rdf(in, a));                                    // u
        CHECK_EQ(rdf(r.data, b + 4), blank ? rdf(in, a + 4) : rdf(in, a + 4) / 2); // v: same texels, texture twice as tall
        CHECK_EQ(rdf(r.data, b + 8), rdf(in, a + 8));                            // width
        CHECK_EQ(rdf(r.data, b + 12), rdf(in, a + 12) / 2);                      // height
        CHECK_EQ(rdf(r.data, b + 0x10), rdf(in, a + 0x10));                      // x offset
        CHECK_EQ(rdf(r.data, b + 0x14), rdf(in, a + 0x14) / 2);                  // y offset: the units of the height
        CHECK_EQ(rdf(r.data, b + 0x18), rdf(in, a + 0x18));                      // advance
        // drawn size in design units = height * texture height / 15: unchanged
        CHECK_EQ(rdf(r.data, b + 12) * rdf(r.data, 0xC), rdf(in, a + 12) * rdf(in, 0xC));
    }
}

TEST(font_icon_glyphs)
{
    Bytes in = make_font();
    font::Patched r = font::add_icons(in, black_icons(), 0xE000);
    REQUIRE(!r.data.empty());
    const Bytes &f = r.data;
    size_t pad = font::find_glyph(in, font::kPadA);
    for (int n = 0; n < 5; n++) {
        size_t g = font::find_glyph(f, (wchar_t)(0xE000 + n));
        REQUIRE(g != 0);
        CHECK_EQ(rdf(f, g + 8), 16.f / 64);
        CHECK_EQ(rdf(f, g + 12), 16.f / 128);
        CHECK(rdf(f, g + 4) >= 0.5f); // in the new half
        CHECK_EQ(rdf(f, g + 0x10), rdf(in, pad + 0x10));
        CHECK_EQ(rdf(f, g + 0x14), rdf(in, pad + 0x14) / 2);
        CHECK_EQ(rdf(f, g + 0x18), rdf(in, pad + 0x18));
        // the texels it points at: icon in the middle, outline on the edge, nothing just outside
        int x0 = (int)std::lround(rdf(f, g) * 64), y0 = (int)std::lround(rdf(f, g + 4) * 128);
        Rgba mid = pixel(f, x0 + 8, y0 + 8), edge = pixel(f, x0, y0 + 8), out = pixel(f, x0 - 2, y0 + 8);
        CHECK(mid.a == 255 && mid.g < 40);
        CHECK(edge.a == 255 && edge.g > 215);
        CHECK_EQ(out.a, 0);
    }
}

TEST(font_icon_in_a_bucket_that_has_glyphs)
{
    // U+E041 shares the bucket of 'A' (0x41): the lookup goes on from 'A' to the icon
    Bytes in = make_font();
    font::Patched r = font::add_icons(in, black_icons(), 0xE041);
    REQUIRE(!r.data.empty());
    CHECK(font::find_glyph(r.data, L'A') != 0);
    for (int n = 0; n < 5; n++) CHECK(font::find_glyph(r.data, (wchar_t)(0xE041 + n)) != 0);
}

TEST(font_rejects_what_it_does_not_know)
{
    Bytes good = make_font();
    Bytes bad = good;
    memcpy(bad.data(), "4.2v", 4);
    CHECK(font::add_icons(bad, black_icons(), 0xE000).error == "not a font file");
    CHECK(font::add_icons(Bytes(good.begin(), good.begin() + 0x100), black_icons(), 0xE000).error == "not a font file");
    bad = good;
    wr(bad, 4, (uint32_t)bad.size()); // texture past the end
    CHECK(font::add_icons(bad, black_icons(), 0xE000).error == "unexpected layout");
    bad = good;
    bad.resize(bad.size() - 1); // texture cut short
    CHECK(font::add_icons(bad, black_icons(), 0xE000).error == "not a one-level DXT3 texture");
    bad = good;
    wr(bad, rd(bad, 4) + 0xC, rd(bad, rd(bad, 4) + 0xC) ^ (0x0E << 8) ^ (0x0C << 8)); // DXT1
    CHECK(font::add_icons(bad, black_icons(), 0xE000).error == "not a one-level DXT3 texture");
    CHECK(font::add_icons(make_font(64, 64, 16, false), black_icons(), 0xE000).error == "no pad button glyph");
    CHECK(font::add_icons(make_font(16, 16, 16), black_icons(), 0xE000).error == "no room for the icons");
    CHECK(font::add_icons(make_font(64, 64, 6), black_icons(), 0xE000).error == "no room for the icons"); // too small
}

TEST(font_real_files_when_given)
{
    // set BLACK_GAME_DIR to the extracted game folder (the one with default.xbe) to run this on the real fonts
    const char *dir = std::getenv("BLACK_GAME_DIR");
    if (!dir || !*dir) {
        std::printf("  (skipped: BLACK_GAME_DIR not set)\n");
        return;
    }
    for (const wchar_t *file : {L"Small.bin", L"Big.bin"}) {
        Bytes in;
        REQUIRE(util::read_file(widen(dir) + L"\\language\\fonts\\" + file, in));
        font::Patched r = font::add_icons(in, black_icons(), 0xE000);
        REQUIRE(!r.data.empty());
        for (int n = 0; n < 5; n++) CHECK(font::find_glyph(r.data, (wchar_t)(0xE000 + n)) != 0);
        // every glyph the game had is still found, at the same place in the texture
        size_t def = rd(in, 0x1C), checked = 0;
        for (size_t g = font::kGlyphs; g < def; g += font::kGlyph) {
            wchar_t c = (wchar_t)(rd(in, g + 0x1C) & 0xFFFF);
            if (font::find_glyph(in, c) != g) continue; // a duplicate the game never reaches
            size_t h = font::find_glyph(r.data, c);
            CHECK(h != 0);
            if (h) CHECK_EQ(rdf(r.data, h), rdf(in, g));
            checked++;
        }
        std::printf("  %s: %zu glyphs, icons %dpx\n", narrow(file).c_str(), checked, r.icon_size);
    }
}
