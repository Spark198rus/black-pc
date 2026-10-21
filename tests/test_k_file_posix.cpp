// Unit Test: POSIX Virtual Filesystem and Case-Insensitive Resolver
#include "loader/src/platform/k_file_posix.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>

#pragma pack(push, 1)
struct XSTRING {
    uint16_t Length;
    uint16_t MaximumLength;
    char    *Buffer;
};

struct XOBJECT_ATTRIBUTES {
    void    *RootDirectory;
    XSTRING *ObjectName;
    uint32_t Attributes;
};

struct XIO_STATUS_BLOCK {
    int32_t  Status;
    uint32_t Information;
};
#pragma pack(pop)

int main()
{
    printf("==========================================================\n");
    printf("     TESTE FASE 2: SISTEMA DE ARQUIVOS POSIX & MOUNTS     \n");
    printf("==========================================================\n");
    std::string game_dir = "../game";
    std::string savedata_dir = "../savedata";
    if (access(game_dir.c_str(), F_OK) != 0 && access("./game", F_OK) == 0) {
        game_dir = "./game";
        savedata_dir = "./savedata";
    }
    if (access((game_dir + "/GlobData.bin").c_str(), F_OK) != 0) {
        printf("[SKIP] game/GlobData.bin not found (requires retail game dump)\n");
        return 0;
    }
    xposix::file_init(game_dir, savedata_dir);

    // 1. Testar Resolução de Symbolic Links e Mounts
    printf("[+] Testando resolução de caminhos virtuais do Xbox...\n");
    std::string path1 = xposix::resolve_xbox_path("\\??\\D:\\GlobData.bin");
    printf("    '\\??\\D:\\GlobData.bin' -> '%s'\n", path1.c_str());
    assert(strstr(path1.c_str(), "GlobData.bin") != nullptr);

    // 2. Testar Resolução Case-Insensitive (maiúsculas para minúsculas no Linux ext4)
    std::string path2 = xposix::resolve_xbox_path("\\Device\\CdRom0\\LEVELS\\LEVEL_00\\FPGUNS\\BG1_SM5.WDD");
    printf("    '\\Device\\CdRom0\\LEVELS\\LEVEL_00\\FPGUNS\\BG1_SM5.WDD' -> '%s'\n", path2.c_str());
    assert(strstr(path2.c_str(), "bg1_sm5.wdd") != nullptr);
    printf("    [PASS] Resolução case-insensitive validada com sucesso!\n");

    // 3. Testar Abertura e Leitura real de GlobData.bin
    printf("\n[+] Testando NtOpenFile e NtReadFile com GlobData.bin real...\n");
    char name_buf[] = "\\??\\D:\\GlobData.bin";
    XSTRING xstr = { (uint16_t)strlen(name_buf), (uint16_t)sizeof(name_buf), name_buf };
    XOBJECT_ATTRIBUTES oa = { nullptr, &xstr, 0 };
    XIO_STATUS_BLOCK ios{};

    HANDLE h_file = nullptr;
    NTSTATUS status = xposix::NtOpenFile(&h_file, 0x0001, &oa, &ios, 0, 0); // FILE_READ_DATA
    assert(status == 0 && h_file != nullptr && "NtOpenFile deve abrir GlobData.bin");
    printf("    [PASS] Arquivo aberto com sucesso! Handle: %p\n", h_file);

    // Ler os primeiros 16 bytes
    uint8_t buffer[16] = {0};
    status = xposix::NtReadFile(h_file, nullptr, nullptr, nullptr, &ios, buffer, sizeof(buffer), nullptr);
    assert(status == 0 && "NtReadFile deve retornar STATUS_SUCCESS");
    assert(ios.Information == sizeof(buffer) && "Deve ter lido exatamente 16 bytes");

    printf("    Primeiros 16 bytes lidos de GlobData.bin: ");
    for (int i = 0; i < 16; ++i) printf("%02X ", buffer[i]);
    printf("\n");

    // 4. Testar Consulta de Tamanho do Arquivo (FileInfo)
    struct {
        int64_t AllocationSize;
        int64_t EndOfFile;
        uint32_t NumberOfLinks;
        uint8_t  DeletePending;
        uint8_t  Directory;
    } fsi{};
    status = xposix::NtQueryInformationFile(h_file, &ios, &fsi, sizeof(fsi), 5); // FileStandardInformation
    assert(status == 0 && "NtQueryInformationFile deve retornar 0");

    struct stat real_st{};
    int stat_res = stat(path1.c_str(), &real_st);
    assert(stat_res == 0 && "stat no arquivo real do host deve ter sucesso");
    printf("    Tamanho do arquivo: %lld bytes (host stat: %lld bytes)\n",
           (long long)fsi.EndOfFile, (long long)real_st.st_size);
    assert(fsi.EndOfFile == real_st.st_size && "Tamanho retornado por NtQueryInformationFile deve coincidir dinamicamente com st_size do Linux!");
    printf("    [PASS] Informações de arquivo confirmadas dinamicamente com o sistema de arquivos!\n");

    // Fechar handle
    bool closed = xposix::file_close_handle(h_file);
    assert(closed && "Handle deve ser fechado com sucesso");

    // 5. Testar Leitura do Setor da Tabela de Partições em \Device\Harddisk0\partition0
    printf("\n[+] Testando leitura de setor cru em '\\Device\\Harddisk0\\partition0'...\n");
    char p0_buf[] = "\\Device\\Harddisk0\\partition0";
    XSTRING p0_xstr = { (uint16_t)strlen(p0_buf), (uint16_t)sizeof(p0_buf), p0_buf };
    XOBJECT_ATTRIBUTES p0_oa = { nullptr, &p0_xstr, 0 };
    HANDLE h_p0 = nullptr;
    status = xposix::NtOpenFile(&h_p0, 0xC0100000, &p0_oa, &ios, 3, 0x10);
    assert(status == 0 && h_p0 != nullptr && "NtOpenFile deve abrir Partition0.bin");

    uint8_t sector_buf[512] = {0};
    LARGE_INTEGER sector_offset{};
    sector_offset.QuadPart = 0x800; // Sector 4 (Partition Table)
    status = xposix::NtReadFile(h_p0, nullptr, nullptr, nullptr, &ios, sector_buf, sizeof(sector_buf), &sector_offset);
    assert(status == 0 && "NtReadFile deve ler o setor 4 com sucesso");
    assert(ios.Information == 512 && "Deve ler exatamente 512 bytes");

    uint32_t magic = *(uint32_t *)sector_buf;
    printf("    Assinatura lida no offset 0x800 do disco: 0x%08X (esperado: 0x97315286)\n", magic);
    assert(magic == 0x97315286 && "Assinatura do Xbox Partition Table (0x97315286) deve estar íntegra!");
    xposix::file_close_handle(h_p0);
    printf("    [PASS] Dispositivo cru Partition0 verificado com sucesso!\n");

    // 6. Testar NtQueryDirectoryFile em u:\ (savedata/Partition1)
    printf("\n[+] Testando NtQueryDirectoryFile em 'u:\\'...\n");
    char u_buf[] = "u:\\";
    XSTRING u_xstr = { (uint16_t)strlen(u_buf), (uint16_t)sizeof(u_buf), u_buf };
    XOBJECT_ATTRIBUTES u_oa = { nullptr, &u_xstr, 0 };
    HANDLE h_u = nullptr;
    status = xposix::NtOpenFile(&h_u, 0x100001, &u_oa, &ios, 1, 0x20); // FILE_LIST_DIRECTORY
    assert(status == 0 && h_u != nullptr && "NtOpenFile deve abrir diretório u:\\");

    struct {
        uint32_t NextEntryOffset;
        uint32_t FileIndex;
        int64_t  CreationTime;
        int64_t  LastAccessTime;
        int64_t  LastWriteTime;
        int64_t  ChangeTime;
        int64_t  EndOfFile;
        int64_t  AllocationSize;
        uint32_t FileAttributes;
        uint32_t FileNameLength;
        char     FileName[256];
    } fdi{};

    int entries_found = 0;
    status = xposix::NtQueryDirectoryFile(h_u, nullptr, nullptr, nullptr, &ios, &fdi, sizeof(fdi), 1, nullptr, true);
    while (status == 0) {
        entries_found++;
        assert(fdi.FileNameLength > 0);
        assert(strcmp(fdi.FileName, ".") != 0 && "Não deve retornar '.'");
        assert(strcmp(fdi.FileName, "..") != 0 && "Não deve retornar '..'");
        printf("    Encontrada entrada #%d: '%s' (Attr: 0x%02X, Len: %u)\n",
               entries_found, fdi.FileName, fdi.FileAttributes, fdi.FileNameLength);
        status = xposix::NtQueryDirectoryFile(h_u, nullptr, nullptr, nullptr, &ios, &fdi, sizeof(fdi), 1, nullptr, false);
    }
    assert(status == (NTSTATUS)0x80000006L && "Ao término do diretório deve retornar STATUS_NO_MORE_FILES (0x80000006)");
    printf("    [PASS] Enumeração completa de u:\\ concluída com sucesso (%d entradas encontradas)!\n", entries_found);
    xposix::file_close_handle(h_u);

    printf("\n>> TESTE DE ARQUIVOS POSIX PASSOU COM SUCESSO!\n\n");
    return 0;
}
