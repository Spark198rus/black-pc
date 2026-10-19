// Xbox Direct3D 8 (XDK 5849) types as the game sees them: resource headers in game memory, the device
// object fields the game reads directly, and enum values (the Xbox uses NV2A/OpenGL-style values for
// many of them). Field layouts are the publicly documented Xbox D3D ABI; written for this project.
#pragma once

#include "common.h"

namespace xd3d {

// ---- resources ----

// D3DResource::Common
constexpr uint32_t COMMON_REFCOUNT_MASK = 0x0000FFFF;
constexpr uint32_t COMMON_TYPE_MASK = 0x00070000;
constexpr uint32_t COMMON_TYPE_SHIFT = 16;
constexpr uint32_t COMMON_INTREFCOUNT_MASK = 0x00780000;
constexpr uint32_t COMMON_VIDEOMEMORY = 0x00800000;
constexpr uint32_t COMMON_D3DCREATED = 0x01000000;

enum ResourceType : uint32_t {
    RTYPE_VERTEXBUFFER = 0,
    RTYPE_INDEXBUFFER = 1,
    RTYPE_PUSHBUFFER = 2,
    RTYPE_PALETTE = 3,
    RTYPE_TEXTURE = 4,
    RTYPE_SURFACE = 5,
    RTYPE_FIXUP = 6,
};

struct Resource {
    uint32_t Common;
    uint32_t Data; // physical address (contiguous memory) of the data
    uint32_t Lock;
};

struct PixelContainer : Resource {
    uint32_t Format;
    uint32_t Size;
};

struct Surface : PixelContainer {
    PixelContainer *Parent;
};

struct Palette : Resource {};
struct VertexBuffer : Resource {};

// PixelContainer::Format
constexpr uint32_t FMT_DMA_MASK = 0x00000003;
constexpr uint32_t FMT_CUBEMAP = 0x00000004;
constexpr uint32_t FMT_BORDERSOURCE_COLOR = 0x00000008;
constexpr uint32_t FMT_DIMENSION_SHIFT = 4;
constexpr uint32_t FMT_FORMAT_SHIFT = 8;
constexpr uint32_t FMT_MIPMAP_SHIFT = 16;
constexpr uint32_t FMT_USIZE_SHIFT = 20;
constexpr uint32_t FMT_VSIZE_SHIFT = 24;
constexpr uint32_t FMT_PSIZE_SHIFT = 28;

// PixelContainer::Size (linear formats only; 0 for swizzled power-of-two)
constexpr uint32_t SIZE_WIDTH_MASK = 0x00000FFF;
constexpr uint32_t SIZE_HEIGHT_SHIFT = 12;
constexpr uint32_t SIZE_PITCH_SHIFT = 24;

inline void *phys_to_virt(uint32_t data) { return (void *)(uintptr_t)(data | 0x80000000u); }
inline uint32_t resource_type(const Resource *r) { return (r->Common & COMMON_TYPE_MASK) >> COMMON_TYPE_SHIFT; }

// Xbox D3DFORMAT values (field FMT_FORMAT of PixelContainer::Format)
enum XFormat : uint32_t {
    XFMT_L8 = 0x00, XFMT_AL8 = 0x01, XFMT_A1R5G5B5 = 0x02, XFMT_X1R5G5B5 = 0x03, XFMT_A4R4G4B4 = 0x04,
    XFMT_R5G6B5 = 0x05, XFMT_A8R8G8B8 = 0x06, XFMT_X8R8G8B8 = 0x07, XFMT_P8 = 0x0B, XFMT_DXT1 = 0x0C,
    XFMT_DXT3 = 0x0E, XFMT_DXT5 = 0x0F, XFMT_LIN_A1R5G5B5 = 0x10, XFMT_LIN_R5G6B5 = 0x11,
    XFMT_LIN_A8R8G8B8 = 0x12, XFMT_LIN_L8 = 0x13, XFMT_LIN_R8B8 = 0x16, XFMT_LIN_G8B8 = 0x17,
    XFMT_A8 = 0x19, XFMT_A8L8 = 0x1A, XFMT_LIN_AL8 = 0x1B, XFMT_LIN_X1R5G5B5 = 0x1C,
    XFMT_LIN_A4R4G4B4 = 0x1D, XFMT_LIN_X8R8G8B8 = 0x1E, XFMT_LIN_A8 = 0x1F, XFMT_LIN_A8L8 = 0x20,
    XFMT_YUY2 = 0x24, XFMT_UYVY = 0x25, XFMT_L6V5U5 = 0x27, XFMT_V8U8 = 0x28, XFMT_R8B8 = 0x29,
    XFMT_D24S8 = 0x2A, XFMT_F24S8 = 0x2B, XFMT_D16 = 0x2C, XFMT_F16 = 0x2D, XFMT_LIN_D24S8 = 0x2E,
    XFMT_LIN_F24S8 = 0x2F, XFMT_LIN_D16 = 0x30, XFMT_LIN_F16 = 0x31, XFMT_L16 = 0x32, XFMT_V16U16 = 0x33,
    XFMT_LIN_L16 = 0x35, XFMT_LIN_V16U16 = 0x36, XFMT_LIN_L6V5U5 = 0x37, XFMT_R6G5B5 = 0x38,
    XFMT_G8B8 = 0x39, XFMT_A8B8G8R8 = 0x3A, XFMT_B8G8R8A8 = 0x3B, XFMT_R8G8B8A8 = 0x3C,
    XFMT_LIN_R6G5B5 = 0x3D, XFMT_LIN_A8B8G8R8 = 0x3E, XFMT_LIN_B8G8R8A8 = 0x3F, XFMT_LIN_R8G8B8A8 = 0x40,
    XFMT_VERTEXDATA = 0x64, XFMT_INDEX16 = 0x65,
};

struct FormatInfo {
    uint8_t bpp;      // bits per pixel (4 for DXT1, 8 for DXT3/5)
    bool linear;      // LIN_ formats: pitch-linear, sized by the Size field
    bool compressed;  // DXT
    const char *name;
};
const FormatInfo &format_info(uint32_t xfmt);

// ---- device creation / modes ----

struct DisplayMode { // Xbox D3DDISPLAYMODE
    uint32_t Width, Height, RefreshRate, Flags, Format;
};

struct PresentParameters { // Xbox D3DPRESENT_PARAMETERS (0x44 bytes)
    uint32_t BackBufferWidth;
    uint32_t BackBufferHeight;
    uint32_t BackBufferFormat;
    uint32_t BackBufferCount;
    uint32_t MultiSampleType;
    uint32_t SwapEffect;
    uint32_t hDeviceWindow;
    uint32_t Windowed;
    uint32_t EnableAutoDepthStencil;
    uint32_t AutoDepthStencilFormat;
    uint32_t Flags;
    uint32_t FullScreen_RefreshRateInHz;
    uint32_t FullScreen_PresentationInterval;
    Surface *BufferSurfaces[3];
    Surface *DepthStencilSurface;
};
static_assert(sizeof(PresentParameters) == 0x44, "");

struct Viewport { // D3DVIEWPORT8
    uint32_t X, Y, Width, Height;
    float MinZ, MaxZ;
};

// ---- device object fields read or written directly by game code (see NOTES.md) ----
constexpr uint32_t DEV_PUT = 0x0000;          // push buffer write pointer
constexpr uint32_t DEV_THRESHOLD = 0x0004;    // MakeRequestedSpace when PUT reaches this
constexpr uint32_t DEV_STATEFLAGS = 0x0008;
constexpr uint32_t DEV_GPUTIME = 0x002C;      // fence / GPU time
constexpr uint32_t DEV_SCREENOFFSET_X = 0x0EF8;
constexpr uint32_t DEV_SCREENOFFSET_Y = 0x0EFC;
constexpr uint32_t DEV_TEXTURES = 0x0F98;     // Resource* [4]
constexpr uint32_t DEV_VERTEXSHADER = 0x0794;  // m_VertexShader (XbSymbolDatabase)
constexpr uint32_t DEV_RENDERTARGET = 0x1A04; // Surface*
constexpr uint32_t DEV_ZBUFFER = 0x1A08;      // Surface*
constexpr uint32_t DEV_SWAPCALLBACK = 0x1DB4;  // m_SwapCallback
constexpr uint32_t DEV_VBLANKCALLBACK = 0x1DB8; // m_VBlankCallback (set by game code)
constexpr uint32_t DEV_VBLANKEVENT = 0x1DBC;   // m_VerticalBlankEvent
constexpr uint32_t DEV_SWAPCOUNT = 0x2478;
// Gamma ramp (D3DGAMMARAMP: red[256], green[256], blue[256] bytes), double-buffered: the game's ramp builder
// FUN_001762B0 writes buffer (swap count & 1) at +0x1E04 + i*0x300, makes it current (+0x240C) and sets its
// dirty flag (+0x2404 + i*4) for the vblank upload to the DAC palette.
constexpr uint32_t DEV_GAMMA_RAMP = 0x1E04;
constexpr uint32_t DEV_GAMMA_DIRTY = 0x2404;
constexpr uint32_t DEV_GAMMA_INDEX = 0x240C;
constexpr uint32_t ADDR_DEVICE_OBJECT = 0x00279B50; // static g_Device
constexpr uint32_t DEVICE_SIZE = 0x24A8;            // up to D3D_g_pDevice (0x27BFF8)

// Xbox data in the D3D section that game code reads or writes
constexpr uint32_t ADDR_G_PDEVICE = 0x0027BFF8;    // D3D_g_pDevice
constexpr uint32_t ADDR_PUSH_SEGMENT = 0x0027CEB0; // size the game passes to MakeRequestedSpace
constexpr uint32_t ADDR_RENDERSTATE = 0x0027C008;  // D3D_g_RenderState[166]
constexpr uint32_t ADDR_TEXTURESTATE = 0x00279948; // D3D_g_DeferredTextureState[4][32]
constexpr uint32_t ADDR_STREAMS = 0x00279758;      // D3D_g_Stream
constexpr uint32_t ADDR_DIRTYFLAGS = 0x0027C000;

inline uint32_t *render_state() { return (uint32_t *)(uintptr_t)ADDR_RENDERSTATE; }
inline uint32_t *texture_state(int stage) { return (uint32_t *)(uintptr_t)ADDR_TEXTURESTATE + stage * 32; }

// Xbox D3DRENDERSTATETYPE for XDK 5849 (verified against the XbSymbolDatabase variable addresses).
enum XRS : uint32_t {
    XRS_PSALPHAINPUTS0 = 0, XRS_PSFINALCOMBINERINPUTSABCD = 8, XRS_PSFINALCOMBINERINPUTSEFG = 9,
    XRS_PSCONSTANT0_0 = 10, XRS_PSCONSTANT1_0 = 18, XRS_PSALPHAOUTPUTS0 = 26, XRS_PSRGBINPUTS0 = 34,
    XRS_PSCOMPAREMODE = 42, XRS_PSFINALCOMBINERCONSTANT0 = 43, XRS_PSFINALCOMBINERCONSTANT1 = 44,
    XRS_PSRGBOUTPUTS0 = 45, XRS_PSCOMBINERCOUNT = 53,
    // simple
    XRS_ZFUNC = 57, XRS_ALPHAFUNC, XRS_ALPHABLENDENABLE, XRS_ALPHATESTENABLE, XRS_ALPHAREF, XRS_SRCBLEND,
    XRS_DESTBLEND, XRS_ZWRITEENABLE, XRS_DITHERENABLE, XRS_SHADEMODE, XRS_COLORWRITEENABLE, XRS_STENCILZFAIL,
    XRS_STENCILPASS, XRS_STENCILFUNC, XRS_STENCILREF, XRS_STENCILMASK, XRS_STENCILWRITEMASK, XRS_BLENDOP,
    XRS_BLENDCOLOR, XRS_SWATHWIDTH, XRS_POLYGONOFFSETZSLOPESCALE, XRS_POLYGONOFFSETZOFFSET, XRS_POINTOFFSETENABLE,
    XRS_WIREFRAMEOFFSETENABLE, XRS_SOLIDOFFSETENABLE, XRS_DEPTHCLIPCONTROL, XRS_STIPPLEENABLE, // 83
    // deferred
    XRS_FOGENABLE = 92, XRS_FOGTABLEMODE, XRS_FOGSTART, XRS_FOGEND, XRS_FOGDENSITY, XRS_RANGEFOGENABLE,
    XRS_WRAP0, XRS_WRAP1, XRS_WRAP2, XRS_WRAP3, XRS_LIGHTING, XRS_SPECULARENABLE, XRS_LOCALVIEWER, XRS_COLORVERTEX,
    XRS_BACKSPECULARMATERIALSOURCE, XRS_BACKDIFFUSEMATERIALSOURCE, XRS_BACKAMBIENTMATERIALSOURCE,
    XRS_BACKEMISSIVEMATERIALSOURCE, XRS_SPECULARMATERIALSOURCE, XRS_DIFFUSEMATERIALSOURCE, XRS_AMBIENTMATERIALSOURCE,
    XRS_EMISSIVEMATERIALSOURCE, XRS_BACKAMBIENT, XRS_AMBIENT, XRS_POINTSIZE, XRS_POINTSIZE_MIN, XRS_POINTSPRITEENABLE,
    XRS_POINTSCALEENABLE, XRS_POINTSCALE_A, XRS_POINTSCALE_B, XRS_POINTSCALE_C, XRS_POINTSIZE_MAX, XRS_PATCHEDGESTYLE,
    XRS_PATCHSEGMENTS, XRS_SWAPFILTER, XRS_PRESENTATIONINTERVAL, // 127
    // complex
    XRS_PSTEXTUREMODES = 136, XRS_VERTEXBLEND, XRS_FOGCOLOR, XRS_FILLMODE, XRS_BACKFILLMODE, XRS_TWOSIDEDLIGHTING,
    XRS_NORMALIZENORMALS, XRS_ZENABLE, XRS_STENCILENABLE, XRS_STENCILFAIL, XRS_FRONTFACE, XRS_CULLMODE,
    XRS_TEXTUREFACTOR, XRS_ZBIAS, XRS_LOGICOP, XRS_EDGEANTIALIAS, XRS_MULTISAMPLEANTIALIAS, XRS_MULTISAMPLEMASK,
    XRS_MULTISAMPLEMODE, XRS_MULTISAMPLERENDERTARGETMODE, XRS_SHADOWFUNC, XRS_LINEWIDTH, XRS_SAMPLEALPHA,
    XRS_DXT1NOISEENABLE, XRS_YUVENABLE, XRS_OCCLUSIONCULLENABLE, XRS_STENCILCULLENABLE, XRS_ROPZCMPALWAYSREAD,
    XRS_ROPZREAD, XRS_DONOTCULLUNCOMPRESSED, // 165
    XRS_MAX = 166,
};

// ---- enums ----

enum : uint32_t { // D3DCLEAR (Xbox)
    XCLEAR_ZBUFFER = 0x01,
    XCLEAR_STENCIL = 0x02,
    XCLEAR_TARGET = 0xF0,
};

enum : uint32_t { // D3DPRIMITIVETYPE (Xbox) = NV097_SET_BEGIN_END values
    XPT_POINTLIST = 1, XPT_LINELIST = 2, XPT_LINELOOP = 3, XPT_LINESTRIP = 4, XPT_TRIANGLELIST = 5,
    XPT_TRIANGLESTRIP = 6, XPT_TRIANGLEFAN = 7, XPT_QUADLIST = 8, XPT_QUADSTRIP = 9, XPT_POLYGON = 10,
};

} // namespace xd3d
