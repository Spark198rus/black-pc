// POSIX Virtual Filesystem and Device Mounts for Xbox kernel
#pragma once

#include "loader/src/platform/windefs.h"

#include <cstdint>
#include <string>

namespace xposix {

void file_init(const std::string &game_dir = "./game", const std::string &savedata_dir = "./savedata");
void set_cache_override(const std::string &cache_dir);

// Symbolic Links
NTSTATUS IoCreateSymbolicLink(const char *symlink_name, const char *device_name);
NTSTATUS IoDeleteSymbolicLink(const char *symlink_name);

// Path Resolution
std::string resolve_xbox_path(const std::string &xbox_path);

// File Operations
NTSTATUS NtCreateFile(HANDLE *out_handle, ACCESS_MASK desired_access, void *obj_attr, void *io_status,
                      const LARGE_INTEGER *alloc_size, uint32_t file_attr, uint32_t share_access,
                      uint32_t create_disp, uint32_t create_options);

NTSTATUS NtOpenFile(HANDLE *out_handle, ACCESS_MASK desired_access, void *obj_attr, void *io_status,
                    uint32_t share_access, uint32_t open_options);

NTSTATUS NtReadFile(HANDLE handle, HANDLE event, void *apc_routine, void *apc_context, void *io_status,
                    void *buffer, uint32_t length, const LARGE_INTEGER *byte_offset);

NTSTATUS NtWriteFile(HANDLE handle, HANDLE event, void *apc_routine, void *apc_context, void *io_status,
                     const void *buffer, uint32_t length, const LARGE_INTEGER *byte_offset);

NTSTATUS NtQueryInformationFile(HANDLE handle, void *io_status, void *file_info, uint32_t length, uint32_t info_class);
NTSTATUS NtSetInformationFile(HANDLE handle, void *io_status, void *file_info, uint32_t length, uint32_t info_class);
NTSTATUS NtQueryVolumeInformationFile(HANDLE handle, void *io_status, void *fs_info, uint32_t length, uint32_t fs_info_class);
NTSTATUS NtQueryFullAttributesFile(void *obj_attr, void *file_info);
NTSTATUS NtQueryDirectoryFile(HANDLE handle, HANDLE event, void *apc_routine, void *apc_context, void *io_status,
                             void *file_info, uint32_t length, uint32_t info_class,
                             void *file_name, bool restart_scan);
NTSTATUS NtOpenSymbolicLinkObject(HANDLE *link_handle, void *obj_attr);
NTSTATUS NtQuerySymbolicLinkObject(HANDLE link_handle, void *link_target, uint32_t *returned_length);

bool file_close_handle(HANDLE handle);

} // namespace xposix
