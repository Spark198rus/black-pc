#include "util/image.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace util {

std::vector<Rgba> scale_area(const GreyAlpha &src, int s)
{
    std::vector<Rgba> out(s * s);
    const int n = src.size;
    double step = (double)n / s;
    for (int oy = 0; oy < s; oy++)
        for (int ox = 0; ox < s; ox++) {
            double ga = 0, a = 0, area = 0;
            for (int y = (int)(oy * step); y < (int)std::ceil((oy + 1) * step) && y < n; y++)
                for (int x = (int)(ox * step); x < (int)std::ceil((ox + 1) * step) && x < n; x++) {
                    double wy = std::min<double>(y + 1, (oy + 1) * step) - std::max<double>(y, oy * step);
                    double wx = std::min<double>(x + 1, (ox + 1) * step) - std::max<double>(x, ox * step);
                    double w = wx * wy, al = src.px[(y * n + x) * 2 + 1];
                    ga += w * al * src.px[(y * n + x) * 2];
                    a += w * al;
                    area += w;
                }
            uint8_t g = a > 0 ? (uint8_t)std::lround(ga / a) : 0;
            out[oy * s + ox] = {g, g, g, (uint8_t)std::lround(area > 0 ? a / area : 0)};
        }
    return out;
}

std::vector<Rgba> outlined(const GreyAlpha &src, int s)
{
    int r = std::max(1, s / 24), inner = s - 2 * r;
    std::vector<Rgba> px = scale_area(src, inner), out(s * s, Rgba{255, 255, 255, 0});
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

namespace {

uint16_t to565(int r, int g, int b) { return (uint16_t)(((r * 31 + 127) / 255) << 11 | ((g * 63 + 127) / 255) << 5 | (b * 31 + 127) / 255); }

void from565(uint16_t c, int *rgb)
{
    rgb[0] = (c >> 11) * 255 / 31;
    rgb[1] = ((c >> 5) & 63) * 255 / 63;
    rgb[2] = (c & 31) * 255 / 31;
}

// The four colours of a block with c0 > c1 (DXT3 always uses the four-colour mode).
void palette(uint16_t c0, uint16_t c1, int pal[4][3])
{
    from565(c0, pal[0]);
    from565(c1, pal[1]);
    for (int k = 0; k < 3; k++) {
        pal[2][k] = (2 * pal[0][k] + pal[1][k]) / 3;
        pal[3][k] = (pal[0][k] + 2 * pal[1][k]) / 3;
    }
}

int luma(const Rgba &p) { return p.r * 299 + p.g * 587 + p.b * 114; }

} // namespace

void dxt3_encode_block(const Rgba *p, uint8_t *out)
{
    uint64_t alpha = 0;
    for (int i = 0; i < 16; i++) alpha |= (uint64_t)((p[i].a * 15 + 127) / 255) << (4 * i);
    memcpy(out, &alpha, 8);
    int lo = -1, hi = -1;
    for (int i = 0; i < 16; i++) {
        if (!p[i].a) continue;
        if (lo < 0 || luma(p[i]) < luma(p[lo])) lo = i;
        if (hi < 0 || luma(p[i]) > luma(p[hi])) hi = i;
    }
    uint16_t c0 = hi < 0 ? 0 : to565(p[hi].r, p[hi].g, p[hi].b), c1 = lo < 0 ? 0 : to565(p[lo].r, p[lo].g, p[lo].b);
    if (c0 < c1) std::swap(c0, c1);
    int pal[4][3];
    palette(c0, c1, pal);
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

void dxt3_decode_block(const uint8_t *in, Rgba *p)
{
    uint64_t alpha;
    uint16_t c0, c1;
    uint32_t bits;
    memcpy(&alpha, in, 8);
    memcpy(&c0, in + 8, 2);
    memcpy(&c1, in + 10, 2);
    memcpy(&bits, in + 12, 4);
    int pal[4][3];
    palette(c0, c1, pal);
    for (int i = 0; i < 16; i++) {
        const int *c = pal[(bits >> (2 * i)) & 3];
        p[i] = {(uint8_t)c[0], (uint8_t)c[1], (uint8_t)c[2], (uint8_t)(((alpha >> (4 * i)) & 15) * 17)};
    }
}

} // namespace util
