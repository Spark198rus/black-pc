// Standalone HLSL Shader Compiler Helper for Linux x86 (Phase 4)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STDCALL __attribute__((stdcall))

typedef int HRESULT;
typedef unsigned int UINT;
typedef unsigned long ULONG;
typedef size_t SIZE_T;

struct ID3D10BlobVtbl;
typedef struct ID3D10Blob {
    struct ID3D10BlobVtbl *lpVtbl;
} ID3D10Blob;

struct ID3D10BlobVtbl {
    HRESULT (STDCALL *QueryInterface)(ID3D10Blob *This, const void *riid, void **ppvObject);
    ULONG   (STDCALL *AddRef)(ID3D10Blob *This);
    ULONG   (STDCALL *Release)(ID3D10Blob *This);
    void *  (STDCALL *GetBufferPointer)(ID3D10Blob *This);
    SIZE_T  (STDCALL *GetBufferSize)(ID3D10Blob *This);
};

typedef void* HMODULE;
extern HMODULE STDCALL LoadLibraryA(const char *name);
extern void* STDCALL GetProcAddress(HMODULE mod, const char *name);

typedef HRESULT(STDCALL *pD3DCompile)(
    const void *pSrcData, SIZE_T SrcDataSize, const char *pSourceName,
    const void *pDefines, void *pInclude, const char *pEntrypoint,
    const char *pTarget, UINT Flags1, UINT Flags2,
    ID3D10Blob **ppCode, ID3D10Blob **ppErrorMsgs);

int main(int argc, char **argv)
{
    if (argc < 4) {
        fprintf(stderr, "Usage: fxc_linux <target: ps_2_0 | vs_2_0> <hlsl_file> <output_bin>\n");
        return 1;
    }

    const char *target = argv[1];
    const char *hlsl_path = argv[2];
    const char *out_path = argv[3];

    FILE *f = fopen(hlsl_path, "rb");
    if (!f) {
        fprintf(stderr, "fxc_linux: failed to open input %s\n", hlsl_path);
        return 1;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = (char *)malloc(sz + 1);
    if (!buf) {
        fclose(f);
        return 1;
    }
    size_t read_bytes = fread(buf, 1, sz, f);
    buf[read_bytes] = 0;
    fclose(f);

    HMODULE hMod = LoadLibraryA("d3dcompiler_47.dll");
    if (!hMod) {
        fprintf(stderr, "fxc_linux: failed to load d3dcompiler_47.dll\n");
        free(buf);
        return 2;
    }

    pD3DCompile fnCompile = (pD3DCompile)GetProcAddress(hMod, "D3DCompile");
    if (!fnCompile) {
        fprintf(stderr, "fxc_linux: failed to find D3DCompile\n");
        free(buf);
        return 2;
    }

    ID3D10Blob *code = NULL;
    ID3D10Blob *errors = NULL;
    // D3DCOMPILE_OPTIMIZATION_LEVEL2 = 0x0000c000
    HRESULT hr = fnCompile(buf, read_bytes, "shader", NULL, NULL, "main", target, 0x0000c000, 0, &code, &errors);
    if (hr < 0 || !code) {
        if (errors) {
            fprintf(stderr, "fxc_linux compile error: %s\n", (char *)errors->lpVtbl->GetBufferPointer(errors));
            errors->lpVtbl->Release(errors);
        } else {
            fprintf(stderr, "fxc_linux compile failed: 0x%08x\n", (unsigned int)hr);
        }
        free(buf);
        return 3;
    }

    FILE *out = fopen(out_path, "wb");
    if (!out) {
        fprintf(stderr, "fxc_linux: failed to open output file %s\n", out_path);
        code->lpVtbl->Release(code);
        free(buf);
        return 4;
    }
    fwrite(code->lpVtbl->GetBufferPointer(code), 1, code->lpVtbl->GetBufferSize(code), out);
    fclose(out);

    code->lpVtbl->Release(code);
    free(buf);
    return 0;
}
