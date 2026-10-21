// Test 2: POSIX XBE Memory Mapping verification at 0x00010000
#include "loader/src/platform/xbe_loader.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

int main(int argc, char **argv)
{
    const char *xbe_path = (argc > 1) ? argv[1] : "game/default.xbe";
    if (access(xbe_path, F_OK) != 0 && access("../game/default.xbe", F_OK) == 0) {
        xbe_path = "../game/default.xbe";
    }
    if (access(xbe_path, F_OK) != 0) {
        printf("[SKIP] %s not found (requires retail game dump)\n", xbe_path);
        return 0;
    }

    printf("==========================================================\n");
    printf("     TESTE 2: MAPEAMENTO DE MEMÓRIA DO XBE EM 0x10000    \n");
    printf("==========================================================\n");

    XbeLoader xbe;
    std::string err;
    if (!xbe.load(xbe_path, err)) {
        fprintf(stderr, "FALHA ao carregar XBE: %s\n", err.c_str());
        return 1;
    }

    printf("Mapeando XBE na faixa 0x%08X - 0x%08X (span: %u bytes)...\n",
           xbe.base, xbe.base + xbe.size_of_image, xbe.size_of_image);

    if (!xbe.map_image(err)) {
        fprintf(stderr, "FALHA no map_image: %s\n", err.c_str());
        return 1;
    }

    printf("[+] Mapeamento concluído com sucesso em 0x%08X!\n", xbe.base);

    // 1. Validar cabeçalho mapeado na memória virtual
    const char *mapped_magic = (const char *)(uintptr_t)xbe.base;
    printf("[+] Verificando magic no endereço base 0x%08X: '%.4s'\n", xbe.base, mapped_magic);
    assert(memcmp(mapped_magic, "XBEH", 4) == 0 && "Cabeçalho na memória deve ter magic XBEH");

    // 2. Validar seções na memória virtual
    for (const auto &s : xbe.sections) {
        if (s.flags & XBE_SEC_PRELOAD) {
            const uint8_t *mem = (const uint8_t *)(uintptr_t)s.vaddr;
            const uint8_t *disk = xbe.data.data() + s.raw_addr;
            int diff = memcmp(mem, disk, s.raw_size);
            printf("[+] Seção %-10s em 0x%08X (%u bytes raw): %s\n",
                   s.name.c_str(), s.vaddr, s.raw_size, (diff == 0 ? "INTEGRIDADE CONFIRMADA" : "CORRUPÇÃO!"));
            assert(diff == 0 && "Dados na memória devem coincidir bit a bit com o arquivo");

            // Verificar BSS zeroed
            if (s.vsize > s.raw_size) {
                const uint8_t *bss = mem + s.raw_size;
                size_t bss_size = s.vsize - s.raw_size;
                bool all_zero = true;
                for (size_t i = 0; i < bss_size && i < 1024; ++i) {
                    if (bss[i] != 0) { all_zero = false; break; }
                }
                assert(all_zero && "Cauda BSS deve estar zerada");
            }
        }
    }

    // 3. Inspecionar opcodes no Ponto de Entrada (Entry Point)
    uint8_t *entry_bytes = (uint8_t *)(uintptr_t)xbe.entry;
    printf("\n[+] Opcodes x86 das primeiras 16 instruções no Entry Point 0x%08X:\n    ", xbe.entry);
    for (int i = 0; i < 16; ++i) {
        printf("%02X ", entry_bytes[i]);
    }
    printf("\n");

    // No ponto de entrada 0x00025A3F da CRT MSVC do Xbox:
    // O código típico inicia com configuração de stack frame ou salto para WinMainCRTStartup
    assert((entry_bytes[0] != 0x00 || entry_bytes[1] != 0x00) && "Entry point não pode ser memória vazia");

    printf("\n>> TESTE 2 (XBE LOADER MAP) PASSOU COM SUCESSO!\n\n");
    return 0;
}
