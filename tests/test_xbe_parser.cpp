// Test 1: POSIX XBE Parser verification against real game/default.xbe
#include "loader/src/platform/xbe_loader.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
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
    printf("     TESTE 1: XBE PARSER & VALIDAÇÃO DE CABEÇALHO        \n");
    printf("==========================================================\n");
    printf("Carregando XBE: %s\n", xbe_path);

    XbeLoader xbe;
    std::string err;
    if (!xbe.load(xbe_path, err)) {
        fprintf(stderr, "FALHA ao carregar XBE: %s\n", err.c_str());
        return 1;
    }

    printf("\n[+] XBE carregado com sucesso! Tamanho do arquivo: %zu bytes\n", xbe.data.size());
    printf("[+] Build kind:      %s\n", xbe.kind.c_str());
    assert(xbe.kind == "retail" && "Build deve ser retail!");

    printf("[+] Image Base:      0x%08X (esperado: 0x00010000)\n", xbe.base);
    assert(xbe.base == 0x00010000 && "Base address deve ser 0x00010000");

    printf("[+] Entry Point:     0x%08X (esperado: 0x00025A3F)\n", xbe.entry);
    assert(xbe.entry == 0x00025A3F && "Entry point deve ser 0x00025A3F");

    printf("[+] Image Size:      0x%08X (%u bytes)\n", xbe.size_of_image, xbe.size_of_image);
    printf("[+] Header Size:     0x%08X (%u bytes)\n", xbe.size_of_headers, xbe.size_of_headers);
    printf("[+] Thunk Address:   0x%08X\n", xbe.thunk_addr);
    printf("[+] Title ID:        0x%08X\n", xbe.title_id);
    printf("[+] Title Name:      '%s'\n", xbe.title.c_str());

    printf("\n[+] Verificando Seções (%zu seções encontradas):\n", xbe.sections.size());
    assert(xbe.sections.size() >= 4 && "XBE deve conter pelo menos 4 seções");

    for (const auto &s : xbe.sections) {
        printf("    - Seção #%-2u %-10s vaddr=0x%08X vsize=0x%08X raw=0x%08X rawsize=0x%08X flags=0x%02X\n",
               s.index, s.name.c_str(), s.vaddr, s.vsize, s.raw_addr, s.raw_size, s.flags);
    }

    const XbeSection *sec_text = xbe.section_named(".text");
    assert(sec_text != nullptr && "Seção .text é obrigatória");
    assert(sec_text->flags & XBE_SEC_EXECUTABLE && "Seção .text deve ser executável");

    auto imports = xbe.kernel_imports();
    printf("\n[+] Verificando Importações do Kernel do Xbox (%zu ordinais):\n", imports.size());
    printf("    Primeiros 10 ordinais: ");
    for (size_t i = 0; i < imports.size() && i < 10; ++i) {
        printf("%u ", imports[i]);
    }
    printf("...\n");
    assert(imports.size() == 115 && "default.xbe do BLACK deve importar exatamente 115 ordinais de kernel");

    printf("\n[+] Verificando Bibliotecas Estáticas vinculadas (%zu bibliotecas):\n", xbe.libraries.size());
    for (const auto &lib : xbe.libraries) {
        printf("    - %-10s v%u.%u.%u\n", lib.name.c_str(), lib.major, lib.minor, lib.build);
    }

    printf("\n>> TESTE 1 (XBE PARSER) PASSOU COM SUCESSO!\n\n");
    return 0;
}
