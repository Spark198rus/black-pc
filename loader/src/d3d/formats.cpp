#include "d3d/xd3d.h"

namespace xd3d {

const FormatInfo &format_info(uint32_t xfmt)
{
    static const FormatInfo unknown = {32, false, false, "?"};
    static FormatInfo table[0x100];
    static bool init = false;
    if (!init) {
        for (auto &f : table) f = unknown;
        auto set = [](uint32_t i, uint8_t bpp, bool lin, bool comp, const char *n) { table[i] = {bpp, lin, comp, n}; };
        set(XFMT_L8, 8, false, false, "L8");
        set(XFMT_AL8, 8, false, false, "AL8");
        set(XFMT_A1R5G5B5, 16, false, false, "A1R5G5B5");
        set(XFMT_X1R5G5B5, 16, false, false, "X1R5G5B5");
        set(XFMT_A4R4G4B4, 16, false, false, "A4R4G4B4");
        set(XFMT_R5G6B5, 16, false, false, "R5G6B5");
        set(XFMT_A8R8G8B8, 32, false, false, "A8R8G8B8");
        set(XFMT_X8R8G8B8, 32, false, false, "X8R8G8B8");
        set(XFMT_P8, 8, false, false, "P8");
        set(XFMT_DXT1, 4, false, true, "DXT1");
        set(XFMT_DXT3, 8, false, true, "DXT3");
        set(XFMT_DXT5, 8, false, true, "DXT5");
        set(XFMT_LIN_A1R5G5B5, 16, true, false, "LIN_A1R5G5B5");
        set(XFMT_LIN_R5G6B5, 16, true, false, "LIN_R5G6B5");
        set(XFMT_LIN_A8R8G8B8, 32, true, false, "LIN_A8R8G8B8");
        set(XFMT_LIN_L8, 8, true, false, "LIN_L8");
        set(XFMT_LIN_R8B8, 16, true, false, "LIN_R8B8");
        set(XFMT_LIN_G8B8, 16, true, false, "LIN_G8B8");
        set(XFMT_A8, 8, false, false, "A8");
        set(XFMT_A8L8, 16, false, false, "A8L8");
        set(XFMT_LIN_AL8, 8, true, false, "LIN_AL8");
        set(XFMT_LIN_X1R5G5B5, 16, true, false, "LIN_X1R5G5B5");
        set(XFMT_LIN_A4R4G4B4, 16, true, false, "LIN_A4R4G4B4");
        set(XFMT_LIN_X8R8G8B8, 32, true, false, "LIN_X8R8G8B8");
        set(XFMT_LIN_A8, 8, true, false, "LIN_A8");
        set(XFMT_LIN_A8L8, 16, true, false, "LIN_A8L8");
        set(XFMT_YUY2, 16, true, false, "YUY2");
        set(XFMT_UYVY, 16, true, false, "UYVY");
        set(XFMT_L6V5U5, 16, false, false, "L6V5U5");
        set(XFMT_V8U8, 16, false, false, "V8U8");
        set(XFMT_R8B8, 16, false, false, "R8B8");
        set(XFMT_D24S8, 32, false, false, "D24S8");
        set(XFMT_F24S8, 32, false, false, "F24S8");
        set(XFMT_D16, 16, false, false, "D16");
        set(XFMT_F16, 16, false, false, "F16");
        set(XFMT_LIN_D24S8, 32, true, false, "LIN_D24S8");
        set(XFMT_LIN_F24S8, 32, true, false, "LIN_F24S8");
        set(XFMT_LIN_D16, 16, true, false, "LIN_D16");
        set(XFMT_LIN_F16, 16, true, false, "LIN_F16");
        set(XFMT_L16, 16, false, false, "L16");
        set(XFMT_V16U16, 32, false, false, "V16U16");
        set(XFMT_LIN_L16, 16, true, false, "LIN_L16");
        set(XFMT_LIN_V16U16, 32, true, false, "LIN_V16U16");
        set(XFMT_LIN_L6V5U5, 16, true, false, "LIN_L6V5U5");
        set(XFMT_R6G5B5, 16, false, false, "R6G5B5");
        set(XFMT_G8B8, 16, false, false, "G8B8");
        set(XFMT_A8B8G8R8, 32, false, false, "A8B8G8R8");
        set(XFMT_B8G8R8A8, 32, false, false, "B8G8R8A8");
        set(XFMT_R8G8B8A8, 32, false, false, "R8G8B8A8");
        set(XFMT_LIN_R6G5B5, 16, true, false, "LIN_R6G5B5");
        set(XFMT_LIN_A8B8G8R8, 32, true, false, "LIN_A8B8G8R8");
        set(XFMT_LIN_B8G8R8A8, 32, true, false, "LIN_B8G8R8A8");
        set(XFMT_LIN_R8G8B8A8, 32, true, false, "LIN_R8G8B8A8");
        init = true;
    }
    return xfmt < 0x100 ? table[xfmt] : unknown;
}

} // namespace xd3d
