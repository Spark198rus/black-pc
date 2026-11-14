#include "check.h"

#include "util/image.h"

#include <cstdlib>
#include <vector>

using util::Rgba;

namespace {

std::vector<uint8_t> grey_alpha(int n, uint8_t (*pixel)(int x, int y, int channel))
{
    std::vector<uint8_t> px(n * n * 2);
    for (int y = 0; y < n; y++)
        for (int x = 0; x < n; x++) {
            px[(y * n + x) * 2] = pixel(x, y, 0);
            px[(y * n + x) * 2 + 1] = pixel(x, y, 1);
        }
    return px;
}

std::vector<Rgba> round_trip(const Rgba *in)
{
    uint8_t block[16];
    util::dxt3_encode_block(in, block);
    std::vector<Rgba> out(16);
    util::dxt3_decode_block(block, out.data());
    return out;
}

} // namespace

TEST(dxt3_four_greys_are_exact)
{
    const uint8_t levels[4] = {0, 85, 170, 255};
    Rgba in[16];
    for (int i = 0; i < 16; i++) in[i] = {levels[i % 4], levels[i % 4], levels[i % 4], 255};
    std::vector<Rgba> out = round_trip(in);
    for (int i = 0; i < 16; i++) {
        CHECK_EQ(out[i].r, in[i].r);
        CHECK_EQ(out[i].g, in[i].g);
        CHECK_EQ(out[i].b, in[i].b);
        CHECK_EQ(out[i].a, 255);
    }
}

TEST(dxt3_alpha_is_4_bit)
{
    Rgba in[16];
    for (int i = 0; i < 16; i++) in[i] = {255, 255, 255, (uint8_t)(i * 17)};
    std::vector<Rgba> out = round_trip(in);
    for (int i = 0; i < 16; i++) CHECK_EQ(out[i].a, i * 17); // the 16 levels a 4-bit alpha holds come back as they were
    Rgba any[16];
    for (int i = 0; i < 16; i++) any[i] = {255, 255, 255, (uint8_t)(i * 16 + 3)};
    out = round_trip(any);
    for (int i = 0; i < 16; i++) CHECK(std::abs(out[i].a - any[i].a) <= 8);
}

TEST(dxt3_one_colour_and_transparent_blocks)
{
    Rgba red[16], clear[16];
    for (int i = 0; i < 16; i++) red[i] = {255, 0, 0, 255}, clear[i] = {0, 0, 0, 0};
    std::vector<Rgba> out = round_trip(red);
    for (const Rgba &p : out) CHECK(p.r == 255 && p.g == 0 && p.b == 0 && p.a == 255);
    out = round_trip(clear);
    for (const Rgba &p : out) CHECK_EQ(p.a, 0);
}

TEST(dxt3_ignores_the_colour_of_transparent_pixels)
{
    // a transparent white pixel must not pull the palette: the opaque black and grey stay exact
    Rgba in[16];
    for (int i = 0; i < 16; i++) in[i] = i == 5 ? Rgba{255, 255, 255, 0} : i % 2 ? Rgba{0, 0, 0, 255} : Rgba{170, 170, 170, 255};
    std::vector<Rgba> out = round_trip(in);
    for (int i = 0; i < 16; i++)
        if (in[i].a) CHECK_EQ(out[i].g, in[i].g);
}

TEST(scale_uniform_image)
{
    std::vector<uint8_t> px = grey_alpha(64, [](int, int, int c) -> uint8_t { return c ? 255 : 100; });
    for (int s : {16, 25, 48, 64}) {
        std::vector<Rgba> out = util::scale_area({64, px.data()}, s);
        REQUIRE(out.size() == (size_t)s * s);
        for (const Rgba &p : out) CHECK(p.g == 100 && p.a == 255);
    }
}

TEST(scale_keeps_coverage)
{
    // left half opaque, right half transparent: the same split after scaling by an exact factor
    std::vector<uint8_t> px = grey_alpha(64, [](int x, int, int c) -> uint8_t { return c ? (x < 32 ? 255 : 0) : 0; });
    std::vector<Rgba> out = util::scale_area({64, px.data()}, 16);
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++) CHECK_EQ(out[y * 16 + x].a, x < 8 ? 255 : 0);
    // and the total alpha about the same at an uneven factor
    out = util::scale_area({64, px.data()}, 25);
    double sum = 0;
    for (const Rgba &p : out) sum += p.a;
    CHECK(std::abs(sum / (25 * 25) - 127.5) < 2);
}

TEST(scale_weights_grey_by_alpha)
{
    // light opaque pixels next to black transparent ones: the result is light and half transparent, not darker
    std::vector<uint8_t> px = grey_alpha(64, [](int x, int y, int c) -> uint8_t {
        bool on = (x + y) % 2 == 0;
        return c ? (on ? 255 : 0) : (on ? 200 : 0);
    });
    std::vector<Rgba> out = util::scale_area({64, px.data()}, 32);
    for (const Rgba &p : out) CHECK(p.g == 200 && std::abs(p.a - 128) <= 1);
}

TEST(outline_surrounds_the_icon)
{
    std::vector<uint8_t> px = grey_alpha(64, [](int, int, int c) -> uint8_t { return c ? 255 : 0; }); // black, opaque
    const int s = 48, r = 2; // r = max(1, s / 24)
    std::vector<Rgba> out = util::outlined({64, px.data()}, s);
    REQUIRE(out.size() == (size_t)s * s);
    auto at = [&](int x, int y) { return out[y * s + x]; };
    CHECK(at(s / 2, s / 2).g == 0 && at(s / 2, s / 2).a == 255);   // the icon
    CHECK(at(0, s / 2).g == 255 && at(0, s / 2).a == 255);         // the outline left of it
    CHECK(at(s / 2, s - 1).g == 255 && at(s / 2, s - 1).a == 255); // and below it
    CHECK_EQ(at(0, 0).a, 0);                                       // rounded corners
    CHECK_EQ(at(r, s / 2).g, 0);                                   // the icon starts r pixels in
}

TEST(outline_is_at_least_one_pixel)
{
    std::vector<uint8_t> px = grey_alpha(64, [](int, int, int c) -> uint8_t { return c ? 255 : 0; });
    std::vector<Rgba> out = util::outlined({64, px.data()}, 16); // 16 / 24 = 0, so r = 1
    CHECK(out[8 * 16 + 0].g == 255 && out[8 * 16 + 0].a == 255);
    CHECK_EQ(out[8 * 16 + 1].g, 0);
}
