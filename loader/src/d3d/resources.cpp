// Resource creation, locking and lifetime. Resource headers and data live in Xbox memory exactly as the
// original library lays them out, because game code builds and reads them directly.
#include "d3d/device.h"
#include "d3d/dispatch.h"
#include "d3d/state.h"
#include "kernel/kexports.h"

#include <cstring>

namespace xd3d {
namespace {

uint32_t log2u(uint32_t v)
{
    uint32_t r = 0;
    while ((1u << r) < v) r++;
    return r;
}

void *alloc_data(uint32_t bytes)
{
    void *p = xk::MmAllocateContiguousMemoryEx(bytes ? bytes : 1, 0, 0xFFFFFFFF, 0x1000, PAGE_READWRITE);
    if (!p) logf("[d3d] out of contiguous memory for a 0x%x-byte resource", bytes);
    return p;
}

Surface *new_surface(uint32_t data, uint32_t format, uint32_t size, PixelContainer *parent)
{
    auto *s = (Surface *)xk::ExAllocatePoolWithTag(sizeof(Surface), 'fruS');
    s->Common = 1 | (RTYPE_SURFACE << COMMON_TYPE_SHIFT) | COMMON_D3DCREATED;
    s->Data = data;
    s->Lock = 0;
    s->Format = format;
    s->Size = size;
    s->Parent = parent;
    if (parent) parent->Common++;
    return s;
}

// Format/Size words for a level of a texture, so a surface header describes just that level.
void level_format(const PixelContainer *pc, uint32_t level, uint32_t *format, uint32_t *size)
{
    if (pc->Size) { // linear
        *format = pc->Format;
        *size = pc->Size;
        return;
    }
    uint32_t w, h, d, levels;
    pixel_container_dims(pc, &w, &h, &d, &levels);
    w = w >> level ? w >> level : 1;
    h = h >> level ? h >> level : 1;
    *format = (pc->Format & ~(0xFFFu << FMT_MIPMAP_SHIFT) & ~FMT_CUBEMAP) | (1u << FMT_MIPMAP_SHIFT) |
              (log2u(w) << FMT_USIZE_SHIFT) | (log2u(h) << FMT_VSIZE_SHIFT);
    *size = 0;
}

void lock_rect(PixelContainer *pc, uint32_t face, uint32_t level, D3DLOCKED_RECT *lr, const RECT *rect)
{
    uint32_t pitch, w, h;
    uint32_t off = texture_level_offset(pc, face, level, &pitch, &w, &h);
    uint8_t *bits = (uint8_t *)phys_to_virt(pc->Data) + off;
    if (rect && pc->Size) {
        const FormatInfo &fi = format_info((pc->Format >> FMT_FORMAT_SHIFT) & 0xFF);
        bits += rect->top * pitch + rect->left * fi.bpp / 8;
    }
    lr->Pitch = (INT)pitch;
    lr->pBits = bits;
    texture_mark_dirty(pc);
}

void destroy(Resource *r)
{
    uint32_t type = resource_type(r);
    if (type == RTYPE_TEXTURE || type == RTYPE_SURFACE) texture_forget(r);
    if (!(r->Common & COMMON_D3DCREATED)) return; // header and data belong to the game
    if (type == RTYPE_SURFACE && ((Surface *)r)->Parent) {
        PixelContainer *parent = ((Surface *)r)->Parent;
        xk::ExFreePool(r);
        if ((parent->Common & COMMON_REFCOUNT_MASK) && (--parent->Common & COMMON_REFCOUNT_MASK) == 0) destroy(parent);
        return;
    }
    for (int i = 0; i < 4; i++)
        if (g_st.textures[i] == r) return; // still bound: keep it (released resources are rare)
    if (r->Data) xk::MmFreeContiguousMemory(phys_to_virt(r->Data));
    xk::ExFreePool(r);
}

} // namespace

// ---- creation ----

// Fill Format/Size for a new pixel container and allocate its data. Returns the data size.
static uint32_t init_pixel_container(PixelContainer *t, uint32_t Width, uint32_t Height, uint32_t Depth, uint32_t *Levels,
                                     uint32_t Format, bool cube, bool volume)
{
    const FormatInfo &fi = format_info(Format);
    if (!Depth) Depth = 1;
    t->Lock = 0;
    if (fi.linear) {
        uint32_t pitch = ((Width * fi.bpp / 8) + 63) & ~63u;
        t->Format = 1 | (2u << FMT_DIMENSION_SHIFT) | (Format << FMT_FORMAT_SHIFT) | (1u << FMT_MIPMAP_SHIFT);
        t->Size = (Width - 1) | ((Height - 1) << SIZE_HEIGHT_SHIFT) | ((pitch / 64 - 1) << SIZE_PITCH_SHIFT);
        *Levels = 1;
    } else {
        uint32_t maxl = log2u(Width > Height ? Width : Height) + 1;
        if (!*Levels || *Levels > maxl) *Levels = maxl;
        t->Format = 1 | (cube ? FMT_CUBEMAP : 0) | ((volume ? 3u : 2u) << FMT_DIMENSION_SHIFT) | (Format << FMT_FORMAT_SHIFT) |
                    (*Levels << FMT_MIPMAP_SHIFT) | (log2u(Width) << FMT_USIZE_SHIFT) | (log2u(Height) << FMT_VSIZE_SHIFT) |
                    (log2u(Depth) << FMT_PSIZE_SHIFT);
        t->Size = 0;
    }
    uint32_t pitch, w, h;
    uint32_t last = texture_level_offset(t, cube ? 5 : 0, *Levels - 1, &pitch, &w, &h);
    uint32_t bytes = last + pitch * (fi.compressed ? (h + 3) / 4 : h);
    if (volume) bytes *= Depth;
    t->Data = xk::MmGetPhysicalAddress(alloc_data(bytes));
    return bytes;
}

Resource *__stdcall D3DDevice_CreateTexture2(uint32_t Width, uint32_t Height, uint32_t Depth, uint32_t Levels, uint32_t Usage,
                                             uint32_t Format, uint32_t ResourceType)
{
    auto *t = (PixelContainer *)xk::ExAllocatePoolWithTag(sizeof(PixelContainer), 'xeTX');
    t->Common = 1 | (RTYPE_TEXTURE << COMMON_TYPE_SHIFT) | COMMON_D3DCREATED;
    uint32_t levels = Levels;
    uint32_t bytes = init_pixel_container(t, Width, Height, Depth, &levels, Format, ResourceType == 5, ResourceType == 4);
    DTRACE("%ux%ux%u, levels %u, usage 0x%x, %s, type %u) -> %p (data 0x%x, 0x%x bytes", Width, Height, Depth, Levels, Usage,
           format_info(Format).name, ResourceType, t, t->Data, bytes);
    return t;
}

// FUN_00269b30: create a stand-alone surface (Width, Height, Usage, Format), e.g. render targets.
Surface *__stdcall D3D_CreateSurface2(uint32_t Width, uint32_t Height, uint32_t Usage, uint32_t Format)
{
    auto *s = (Surface *)xk::ExAllocatePoolWithTag(sizeof(Surface), 'fruS');
    s->Common = 0x80000000u | 1 | (RTYPE_SURFACE << COMMON_TYPE_SHIFT) | COMMON_D3DCREATED;
    s->Parent = nullptr;
    uint32_t levels = 1;
    uint32_t bytes = init_pixel_container(s, Width, Height, 1, &levels, Format, false, false);
    DTRACE("%ux%u, usage 0x%x, %s) -> %p (data 0x%x, 0x%x bytes", Width, Height, Usage, format_info(Format).name, s, s->Data, bytes);
    return s;
}

Resource *__stdcall D3DDevice_CreateVertexBuffer2(uint32_t Length)
{
    auto *vb = (Resource *)xk::ExAllocatePoolWithTag(sizeof(Resource), 'BVX');
    vb->Common = 1 | (RTYPE_VERTEXBUFFER << COMMON_TYPE_SHIFT) | COMMON_D3DCREATED;
    vb->Lock = 0;
    vb->Data = xk::MmGetPhysicalAddress(alloc_data(Length));
    DTRACE("%u) -> %p (", Length, vb);
    return vb;
}

void *__stdcall D3DVertexBuffer_Lock2(Resource *pVB, uint32_t Flags)
{
    DTRACE("%p, 0x%x", pVB, Flags);
    return phys_to_virt(pVB->Data);
}

void *__stdcall D3DPalette_Lock2(Resource *pThis, uint32_t Flags)
{
    DTRACE("%p, 0x%x", pThis, Flags);
    return phys_to_virt(pThis->Data);
}

// ---- lifetime ----

uint32_t __stdcall D3DResource_AddRef(Resource *pThis)
{
    DTRACE("%p", pThis);
    pThis->Common++;
    return pThis->Common & COMMON_REFCOUNT_MASK;
}

uint32_t __stdcall D3DResource_Release(Resource *pThis)
{
    DTRACE("%p", pThis);
    uint32_t n = pThis->Common & COMMON_REFCOUNT_MASK;
    if (n == 0) return 0;
    pThis->Common--;
    if (n == 1) destroy(pThis);
    return n - 1;
}

void __stdcall D3D_DestroyResource(Resource *pResource)
{
    DTRACE("%p", pResource);
    destroy(pResource);
}

void __stdcall D3DResource_BlockUntilNotBusy(Resource *pThis) { DTRACE("%p", pThis); }

// ---- surfaces ----

Surface *__stdcall D3DTexture_GetSurfaceLevel2(PixelContainer *pThis, uint32_t Level)
{
    uint32_t pitch, w, h, fmt, size;
    uint32_t off = texture_level_offset(pThis, 0, Level, &pitch, &w, &h);
    level_format(pThis, Level, &fmt, &size);
    Surface *s = new_surface(pThis->Data + off, fmt, size, pThis);
    DTRACE("%p, %u) -> %p (", pThis, Level, s);
    return s;
}

Surface *__stdcall D3DCubeTexture_GetCubeMapSurface2(PixelContainer *pThis, uint32_t Face, uint32_t Level)
{
    uint32_t pitch, w, h, fmt, size;
    uint32_t off = texture_level_offset(pThis, Face, Level, &pitch, &w, &h);
    level_format(pThis, Level, &fmt, &size);
    Surface *s = new_surface(pThis->Data + off, fmt, size, pThis);
    DTRACE("%p, %u, %u) -> %p (", pThis, Face, Level, s);
    return s;
}

// FUN_0026d6d0: describe one level of a pixel container: data address (virtual), pitch, size in bytes,
// and the Format/Size words a surface header for that level needs.
void __stdcall D3D_GetLevelInfo(PixelContainer *pc, uint32_t Face, uint32_t Level, uint32_t *pData, uint32_t *pPitch,
                                uint32_t *pBytes, uint32_t *pFormat, uint32_t *pSize)
{
    uint32_t pitch, w, h, fmt, size;
    uint32_t off = texture_level_offset(pc, Face, Level, &pitch, &w, &h);
    level_format(pc, Level, &fmt, &size);
    const FormatInfo &fi = format_info((pc->Format >> FMT_FORMAT_SHIFT) & 0xFF);
    *pData = (pc->Data | 0x80000000u) + off;
    *pPitch = pitch;
    *pBytes = pitch * (fi.compressed ? (h + 3) / 4 : h);
    *pFormat = fmt;
    *pSize = size;
    DTRACE("%p, %u, %u) -> data 0x%x pitch %u (", pc, Face, Level, *pData, pitch);
}

// FUN_00269ac0: create a surface header for (parent, data); eax = data, edi = parent.
Surface *__stdcall D3D_CreateSurfaceHeader(uint32_t data, PixelContainer *parent, uint32_t format, uint32_t size)
{
    Surface *s = new_surface(data & 0x0FFFFFFF, format, size, parent);
    DTRACE("data 0x%x, parent %p, fmt 0x%x, size 0x%x) -> %p (", data, parent, format, size, s);
    return s;
}

HRESULT __stdcall D3DTexture_LockRect(PixelContainer *pThis, uint32_t Level, D3DLOCKED_RECT *pLockedRect, const RECT *pRect,
                                      uint32_t Flags)
{
    DTRACE("%p, %u, %p, %p, 0x%x", pThis, Level, pLockedRect, pRect, Flags);
    lock_rect(pThis, 0, Level, pLockedRect, pRect);
    return D3D_OK;
}

HRESULT __stdcall D3DCubeTexture_LockRect(PixelContainer *pThis, uint32_t Face, uint32_t Level, D3DLOCKED_RECT *pLockedRect,
                                          const RECT *pRect, uint32_t Flags)
{
    DTRACE("%p, %u, %u, %p, %p, 0x%x", pThis, Face, Level, pLockedRect, pRect, Flags);
    lock_rect(pThis, Face, Level, pLockedRect, pRect);
    return D3D_OK;
}

HRESULT __stdcall D3DSurface_LockRect(Surface *pThis, D3DLOCKED_RECT *pLockedRect, const RECT *pRect, uint32_t Flags)
{
    DTRACE("%p, %p, %p, 0x%x", pThis, pLockedRect, pRect, Flags);
    lock_rect(pThis, 0, 0, pLockedRect, pRect);
    if (pThis->Parent) texture_mark_dirty(pThis->Parent);
    return D3D_OK;
}

// Xbox D3DSURFACE_DESC: Format, Type, Usage, Size, MultiSampleType, Width, Height
struct SurfaceDesc {
    uint32_t Format, Type, Usage, Size, MultiSampleType, Width, Height;
};

static void fill_desc(PixelContainer *pc, uint32_t level, SurfaceDesc *d)
{
    uint32_t pitch, w, h;
    texture_level_offset(pc, 0, level, &pitch, &w, &h);
    const FormatInfo &fi = format_info((pc->Format >> FMT_FORMAT_SHIFT) & 0xFF);
    d->Format = (pc->Format >> FMT_FORMAT_SHIFT) & 0xFF;
    d->Type = 1; // D3DRTYPE_SURFACE
    d->Usage = 0;
    d->Size = pitch * (fi.compressed ? (h + 3) / 4 : h);
    d->MultiSampleType = 0x0011; // D3DMULTISAMPLE_NONE on Xbox
    d->Width = w;
    d->Height = h;
}

// Get2DSurfaceDesc: edi = pixel container, ebx = level, esi = desc
void __stdcall D3D_Get2DSurfaceDesc(PixelContainer *pc, uint32_t Level, SurfaceDesc *pDesc)
{
    DTRACE("%p, %u, %p", pc, Level, pDesc);
    fill_desc(pc, Level, pDesc);
}

void __stdcall D3DSurface_GetDesc(Surface *pThis, SurfaceDesc *pDesc)
{
    DTRACE("%p, %p", pThis, pDesc);
    fill_desc(pThis, 0, pDesc);
}

// ---- table ----

#define H(addr, fn, params, ret) {addr, #fn, (void *)&fn, params, ret}

static const Hle kTable[] = {
    H(0x0026c6d0, D3DDevice_CreateTexture2, "s s s s s s s", true),
    H(0x00269b30, D3D_CreateSurface2, "s s s s", true),
    H(0x0026c1e0, D3DDevice_CreateVertexBuffer2, "s", true),
    H(0x0026c230, D3DVertexBuffer_Lock2, "s s", true),
    H(0x0026c2c0, D3DPalette_Lock2, "s s", true),
    H(0x0026c470, D3DResource_AddRef, "s", true),
    H(0x0026c4b0, D3DResource_Release, "s", true),
    H(0x0026c360, D3D_DestroyResource, "edi", false),
    H(0x0026c610, D3DResource_BlockUntilNotBusy, "s", false),
    H(0x0026c780, D3DTexture_GetSurfaceLevel2, "s s", true),
    H(0x0026c800, D3DCubeTexture_GetCubeMapSurface2, "s s s", true),
    H(0x00269ac0, D3D_CreateSurfaceHeader, "eax edi s s", true),
    H(0x0026d6d0, D3D_GetLevelInfo, "s s s s s s s s", false),
    H(0x0026c7d0, D3DTexture_LockRect, "s s s s s", true),
    H(0x0026c850, D3DCubeTexture_LockRect, "s s s s s s", true),
    H(0x00269be0, D3DSurface_LockRect, "s s s s", true),
    H(0x0026d630, D3D_Get2DSurfaceDesc, "edi ebx esi", false),
    H(0x00269bc0, D3DSurface_GetDesc, "s s", false),
};

const Hle *hle_resource_table(size_t *count)
{
    *count = sizeof kTable / sizeof kTable[0];
    return kTable;
}

} // namespace xd3d
