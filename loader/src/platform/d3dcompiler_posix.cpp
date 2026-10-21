// Linux x86 Implementation of D3DCompile for HLSL Shader Translation (Phase 4)
#include <d3dcompiler.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <atomic>
#include <sys/stat.h>
#include <unistd.h>

namespace {

class SimpleBlob : public ID3DBlob {
    std::vector<uint8_t> data_;
    std::atomic<ULONG> ref_{1};
public:
    SimpleBlob(std::vector<uint8_t> data) : data_(std::move(data)) {}
    virtual ~SimpleBlob() = default;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObject) override {
        (void)riid;
        if (!ppvObject) return 0x80004003; // E_POINTER
        *ppvObject = this;
        AddRef();
        return 0; // S_OK
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++ref_; }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG r = --ref_;
        if (r == 0) delete this;
        return r;
    }
    void * STDMETHODCALLTYPE GetBufferPointer() override { return data_.data(); }
    SIZE_T STDMETHODCALLTYPE GetBufferSize() override { return data_.size(); }
};

} // namespace

HRESULT D3DCompile(
    const void *pSrcData,
    SIZE_T SrcDataSize,
    const char *pSourceName,
    const D3D_SHADER_MACRO *pDefines,
    ID3DInclude *pInclude,
    const char *pEntryPoint,
    const char *pTarget,
    UINT Flags1,
    UINT Flags2,
    ID3DBlob **ppCode,
    ID3DBlob **ppErrorMsgs)
{
    (void)pSourceName;
    (void)pDefines;
    (void)pInclude;
    (void)pEntryPoint;
    (void)Flags1;
    (void)Flags2;

    if (!pSrcData || !SrcDataSize || !pTarget || !ppCode) {
        return 0x80070057; // E_INVALIDARG
    }
    if (ppErrorMsgs) *ppErrorMsgs = nullptr;
    *ppCode = nullptr;

    // Hash source code + target profile
    std::string text((const char *)pSrcData, SrcDataSize);
    size_t h = std::hash<std::string>{}(text + std::string(pTarget));
    char hash_str[32];
    std::snprintf(hash_str, sizeof(hash_str), "%016zx", h);

    // Resolve tools path and cache directory based on working directory
    const char *fxc_bin = "tools/bin/fxc_linux.exe";
    std::string cache_base = "cache/shaders";
    if (access(fxc_bin, X_OK) != 0 && access("../tools/bin/fxc_linux.exe", X_OK) == 0) {
        fxc_bin = "../tools/bin/fxc_linux.exe";
        cache_base = "../cache/shaders";
        mkdir("../cache", 0755);
        mkdir("../cache/shaders", 0755);
    } else {
        mkdir("cache", 0755);
        mkdir("cache/shaders", 0755);
    }
    std::string cache_path = cache_base + "/" + hash_str + ".bin";

    FILE *cf = std::fopen(cache_path.c_str(), "rb");
    if (cf) {
        std::fseek(cf, 0, SEEK_END);
        long sz = std::ftell(cf);
        std::fseek(cf, 0, SEEK_SET);
        if (sz > 0) {
            std::vector<uint8_t> cached_data(sz);
            if (std::fread(cached_data.data(), 1, sz, cf) == (size_t)sz) {
                std::fclose(cf);
                *ppCode = new SimpleBlob(std::move(cached_data));
                return 0; // S_OK
            }
        }
        std::fclose(cf);
    }

    // Write temp HLSL file
    char src_tmp[64], out_tmp[64], err_tmp[64];
    std::snprintf(src_tmp, sizeof(src_tmp), "/tmp/sh_%s.hlsl", hash_str);
    std::snprintf(out_tmp, sizeof(out_tmp), "/tmp/sh_%s.bin", hash_str);
    std::snprintf(err_tmp, sizeof(err_tmp), "/tmp/sh_%s.err", hash_str);

    FILE *sf = std::fopen(src_tmp, "wb");
    if (!sf) return 0x80004005; // E_FAIL
    std::fwrite(pSrcData, 1, SrcDataSize, sf);
    std::fclose(sf);

    // Invoke fxc_linux compiler
    char cmd[512];
    std::snprintf(cmd, sizeof(cmd), "%s %s %s %s 2> %s",
                  fxc_bin, pTarget, src_tmp, out_tmp, err_tmp);
    int ret = std::system(cmd);

    if (ret != 0) {
        if (ppErrorMsgs) {
            FILE *ef = std::fopen(err_tmp, "rb");
            if (ef) {
                std::fseek(ef, 0, SEEK_END);
                long esz = std::ftell(ef);
                std::fseek(ef, 0, SEEK_SET);
                std::vector<uint8_t> edata(esz + 1, 0);
                if (esz > 0) {
                    size_t nr = std::fread(edata.data(), 1, esz, ef);
                    (void)nr;
                }
                std::fclose(ef);
                *ppErrorMsgs = new SimpleBlob(std::move(edata));
            }
        }
        unlink(src_tmp);
        unlink(err_tmp);
        return 0x80004005; // E_FAIL
    }

    FILE *of = std::fopen(out_tmp, "rb");
    if (!of) {
        unlink(src_tmp);
        unlink(err_tmp);
        return 0x80004005;
    }
    std::fseek(of, 0, SEEK_END);
    long osz = std::ftell(of);
    std::fseek(of, 0, SEEK_SET);
    std::vector<uint8_t> bytecode(osz);
    size_t nr = std::fread(bytecode.data(), 1, osz, of);
    std::fclose(of);
    if ((long)nr != osz) {
        unlink(src_tmp);
        unlink(out_tmp);
        unlink(err_tmp);
        return 0x80004005;
    }

    // Persist compiled bytecode to cache
    FILE *scf = std::fopen(cache_path.c_str(), "wb");
    if (scf) {
        std::fwrite(bytecode.data(), 1, osz, scf);
        std::fclose(scf);
    }

    unlink(src_tmp);
    unlink(out_tmp);
    unlink(err_tmp);

    *ppCode = new SimpleBlob(std::move(bytecode));
    return 0; // S_OK
}
