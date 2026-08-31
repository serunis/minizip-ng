/* mz_os_win32.c -- System functions for Windows
   part of the minizip-ng project

   Copyright (C) Nathan Moinvaziri
     https://github.com/zlib-ng/minizip-ng

   This program is distributed under the terms of the same license as zlib.
   See the accompanying LICENSE file for the full text of the license.
*/

#include "mz.h"
#include "mz_os.h"
#include "mz_strm_os.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <wchar.h>

#include <windows.h>
#include <winioctl.h>

/***************************************************************************/

#ifndef SYMBOLIC_LINK_FLAG_DIRECTORY
#  define SYMBOLIC_LINK_FLAG_DIRECTORY 0x1
#endif
#ifndef SYMLINK_FLAG_RELATIVE
#  define SYMLINK_FLAG_RELATIVE 0x1
#endif

#ifndef _WIN32_WINNT_WIN8
#  define _WIN32_WINNT_WIN8 0x0602
#endif
#ifndef ERROR_UNABLE_TO_MOVE_REPLACEMENT
#  define ERROR_UNABLE_TO_MOVE_REPLACEMENT 1176L
#endif
#ifndef ERROR_UNABLE_TO_MOVE_REPLACEMENT_2
#  define ERROR_UNABLE_TO_MOVE_REPLACEMENT_2 1177L
#endif
#ifndef FILE_NAME_NORMALIZED
#  define FILE_NAME_NORMALIZED 0x0
#endif
#ifndef VOLUME_NAME_DOS
#  define VOLUME_NAME_DOS 0x0
#endif

typedef struct MZ_REPARSE_DATA_BUFFER_s {
    ULONG ReparseTag;
    USHORT ReparseDataLength;
    USHORT Reserved;
    union {
        struct {
            USHORT SubstituteNameOffset;
            USHORT SubstituteNameLength;
            USHORT PrintNameOffset;
            USHORT PrintNameLength;
            ULONG Flags;
            WCHAR PathBuffer[1];
        } SymbolicLinkReparseBuffer;
        struct {
            USHORT SubstituteNameOffset;
            USHORT SubstituteNameLength;
            USHORT PrintNameOffset;
            USHORT PrintNameLength;
            WCHAR PathBuffer[1];
        } MountPointReparseBuffer;
        struct {
            UCHAR DataBuffer[1];
        } GenericReparseBuffer;
    };
} MZ_REPARSE_DATA_BUFFER;

typedef DWORD(WINAPI *mz_get_final_path_name_by_handle_w)(HANDLE, LPWSTR, DWORD, DWORD);

/***************************************************************************/

typedef struct DIR_int_s {
    void *find_handle;
    WIN32_FIND_DATAW find_data;
    struct dirent entry;
    uint8_t end;
} DIR_int;

/***************************************************************************/

wchar_t *mz_os_unicode_string_create(const char *string, int32_t encoding) {
    wchar_t *string_wide = NULL;
    uint32_t string_wide_size = 0;

    string_wide_size = MultiByteToWideChar(encoding, 0, string, -1, NULL, 0);
    if (string_wide_size == 0)
        return NULL;
    string_wide = (wchar_t *)calloc(string_wide_size + 1, sizeof(wchar_t));
    if (!string_wide)
        return NULL;

    MultiByteToWideChar(encoding, 0, string, -1, string_wide, string_wide_size);
    return string_wide;
}

void mz_os_unicode_string_delete(wchar_t **string) {
    if (string) {
        free(*string);
        *string = NULL;
    }
}

char *mz_os_utf8_string_create(const char *string, int32_t encoding) {
    wchar_t *string_wide = NULL;
    char *string_utf8 = NULL;

    string_wide = mz_os_unicode_string_create(string, encoding);
    if (string_wide) {
        string_utf8 = mz_os_utf8_string_create_from_unicode(string_wide, MZ_ENCODING_UTF8);
        mz_os_unicode_string_delete(&string_wide);
    }

    return string_utf8;
}

char *mz_os_utf8_string_create_from_unicode(const wchar_t *string, int32_t encoding) {
    char *string_utf8 = NULL;
    uint32_t string_utf8_size = 0;

    MZ_UNUSED(encoding);

    string_utf8_size = WideCharToMultiByte(CP_UTF8, 0, string, -1, NULL, 0, NULL, NULL);
    if (string_utf8_size == 0)
        return NULL;
    string_utf8 = (char *)calloc(string_utf8_size, sizeof(char));

    if (string_utf8 && WideCharToMultiByte(CP_UTF8, 0, string, -1, string_utf8, string_utf8_size, NULL, NULL) == 0) {
        free(string_utf8);
        string_utf8 = NULL;
    }

    return string_utf8;
}

void mz_os_utf8_string_delete(char **string) {
    if (string) {
        free(*string);
        *string = NULL;
    }
}

/* Gets the system default ANSI code page used for legacy string conversion */
int32_t mz_os_get_default_encoding(void) {
    return (int32_t)GetACP();
}

/***************************************************************************/

int32_t mz_os_rand(uint8_t *buf, int32_t size) {
    unsigned __int64 pentium_tsc[1];
    int32_t len = 0;

    for (len = 0; len < (int)size; len += 1) {
        if (len % 8 == 0)
            QueryPerformanceCounter((LARGE_INTEGER *)pentium_tsc);
        buf[len] = ((unsigned char *)pentium_tsc)[len % 8];
    }

    return len;
}

int32_t mz_os_path_same_fs(const char *path_a, const char *path_b) {
    wchar_t *path_a_wide = NULL;
    wchar_t *path_b_wide = NULL;
    struct _stati64 sa;
    struct _stati64 sb;
    int32_t err = MZ_OK;

    if (!path_a || !path_b)
        return MZ_PARAM_ERROR;

    path_a_wide = mz_os_unicode_string_create(path_a, MZ_ENCODING_UTF8);
    if (!path_a_wide)
        return MZ_PARAM_ERROR;
    path_b_wide = mz_os_unicode_string_create(path_b, MZ_ENCODING_UTF8);
    if (!path_b_wide) {
        mz_os_unicode_string_delete(&path_a_wide);
        return MZ_PARAM_ERROR;
    }

    if (_wstati64(path_a_wide, &sa) != 0 || _wstati64(path_b_wide, &sb) != 0)
        err = MZ_EXIST_ERROR;
    else if (sa.st_dev != sb.st_dev)
        err = MZ_EXIST_ERROR;

    mz_os_unicode_string_delete(&path_a_wide);
    mz_os_unicode_string_delete(&path_b_wide);
    return err;
}

int32_t mz_os_path_same_file(const char *path_a, const char *path_b) {
    BY_HANDLE_FILE_INFORMATION info_a;
    BY_HANDLE_FILE_INFORMATION info_b;
    wchar_t *path_a_wide = NULL;
    wchar_t *path_b_wide = NULL;
    HANDLE handle_a = INVALID_HANDLE_VALUE;
    HANDLE handle_b = INVALID_HANDLE_VALUE;
    int32_t err = MZ_EXIST_ERROR;

    if (!path_a || !path_b)
        return MZ_PARAM_ERROR;

    path_a_wide = mz_os_unicode_string_create(path_a, MZ_ENCODING_UTF8);
    path_b_wide = mz_os_unicode_string_create(path_b, MZ_ENCODING_UTF8);
    if (!path_a_wide || !path_b_wide) {
        err = MZ_MEM_ERROR;
        goto cleanup;
    }

    handle_a = CreateFileW(path_a_wide, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    handle_b = CreateFileW(path_b_wide, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (handle_a == INVALID_HANDLE_VALUE || handle_b == INVALID_HANDLE_VALUE)
        goto cleanup;

    if (!GetFileInformationByHandle(handle_a, &info_a) || !GetFileInformationByHandle(handle_b, &info_b))
        goto cleanup;

    if (info_a.dwVolumeSerialNumber == info_b.dwVolumeSerialNumber && info_a.nFileIndexHigh == info_b.nFileIndexHigh &&
        info_a.nFileIndexLow == info_b.nFileIndexLow)
        err = MZ_OK;

cleanup:
    if (handle_a != INVALID_HANDLE_VALUE)
        CloseHandle(handle_a);
    if (handle_b != INVALID_HANDLE_VALUE)
        CloseHandle(handle_b);
    if (path_a_wide)
        mz_os_unicode_string_delete(&path_a_wide);
    if (path_b_wide)
        mz_os_unicode_string_delete(&path_b_wide);
    return err;
}

int32_t mz_os_rename(const char *source_path, const char *target_path) {
    wchar_t *source_path_wide = NULL;
    wchar_t *target_path_wide = NULL;
    int32_t result = 0;
    int32_t err = MZ_OK;

    if (!source_path || !target_path)
        return MZ_PARAM_ERROR;

    source_path_wide = mz_os_unicode_string_create(source_path, MZ_ENCODING_UTF8);
    if (!source_path_wide) {
        err = MZ_PARAM_ERROR;
    } else {
        target_path_wide = mz_os_unicode_string_create(target_path, MZ_ENCODING_UTF8);
        if (!target_path_wide)
            err = MZ_PARAM_ERROR;
    }

    if (err == MZ_OK) {
#if _WIN32_WINNT >= _WIN32_WINNT_WINXP
        result = MoveFileExW(source_path_wide, target_path_wide, MOVEFILE_WRITE_THROUGH | MOVEFILE_COPY_ALLOWED);
#else
        result = MoveFileW(source_path_wide, target_path_wide);
#endif
        if (result == 0)
            err = MZ_EXIST_ERROR;
    }

    if (target_path_wide)
        mz_os_unicode_string_delete(&target_path_wide);
    if (source_path_wide)
        mz_os_unicode_string_delete(&source_path_wide);

    return err;
}

static wchar_t *mz_os_wide_string_duplicate(const wchar_t *string) {
    wchar_t *duplicate = NULL;
    size_t string_length = 0;

    if (!string)
        return NULL;
    string_length = wcslen(string);
    duplicate = (wchar_t *)malloc((string_length + 1) * sizeof(wchar_t));
    if (duplicate)
        memcpy(duplicate, string, (string_length + 1) * sizeof(wchar_t));
    return duplicate;
}

static int32_t mz_os_get_full_path_wide(const wchar_t *path, wchar_t **full_path) {
    wchar_t *path_buffer = NULL;
    DWORD path_length = 0;
    DWORD result = 0;

    if (!path || !full_path)
        return MZ_PARAM_ERROR;
    *full_path = NULL;

    path_length = GetFullPathNameW(path, 0, NULL, NULL);
    if (path_length == 0)
        return MZ_EXIST_ERROR;
    path_buffer = (wchar_t *)malloc(((size_t)path_length + 1) * sizeof(wchar_t));
    if (!path_buffer)
        return MZ_MEM_ERROR;
    result = GetFullPathNameW(path, path_length + 1, path_buffer, NULL);
    if (result == 0 || result > path_length) {
        free(path_buffer);
        return MZ_EXIST_ERROR;
    }

    *full_path = path_buffer;
    return MZ_OK;
}

static int32_t mz_os_get_final_path_wide(HANDLE handle, const wchar_t *fallback_path, wchar_t **final_path) {
    HMODULE kernel32_mod = NULL;
    mz_get_final_path_name_by_handle_w get_final_path = NULL;
    wchar_t *path_buffer = NULL;
    DWORD path_length = 0;
    DWORD result = 0;

    if (handle == INVALID_HANDLE_VALUE || !fallback_path || !final_path)
        return MZ_PARAM_ERROR;
    *final_path = NULL;

    kernel32_mod = GetModuleHandleW(L"kernel32.dll");
    if (kernel32_mod)
        get_final_path = (mz_get_final_path_name_by_handle_w)GetProcAddress(kernel32_mod, "GetFinalPathNameByHandleW");
    if (!get_final_path) {
        *final_path = mz_os_wide_string_duplicate(fallback_path);
        return *final_path ? MZ_OK : MZ_MEM_ERROR;
    }

    path_length = get_final_path(handle, NULL, 0, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (path_length == 0)
        return MZ_EXIST_ERROR;
    path_buffer = (wchar_t *)malloc(((size_t)path_length + 1) * sizeof(wchar_t));
    if (!path_buffer)
        return MZ_MEM_ERROR;
    result = get_final_path(handle, path_buffer, path_length + 1, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (result == 0 || result > path_length) {
        free(path_buffer);
        return MZ_EXIST_ERROR;
    }

    *final_path = path_buffer;
    return MZ_OK;
}

static int32_t mz_os_get_parent_path_wide(const wchar_t *path, wchar_t **parent_path, const wchar_t **basename) {
    const wchar_t *slash = NULL;
    const wchar_t *slash_forward = NULL;
    size_t parent_length = 0;

    if (!path || !parent_path || !basename)
        return MZ_PARAM_ERROR;
    *parent_path = NULL;
    *basename = NULL;

    slash = wcsrchr(path, L'\\');
    slash_forward = wcsrchr(path, L'/');
    if (!slash || (slash_forward && slash_forward > slash))
        slash = slash_forward;
    if (!slash)
        return MZ_EXIST_ERROR;

    *basename = slash + 1;
    parent_length = (size_t)(slash - path);
    if (parent_length == 2 && path[1] == L':')
        parent_length += 1;
    else if (parent_length == 0)
        parent_length = 1;

    *parent_path = (wchar_t *)malloc((parent_length + 1) * sizeof(wchar_t));
    if (!*parent_path)
        return MZ_MEM_ERROR;
    memcpy(*parent_path, path, parent_length * sizeof(wchar_t));
    (*parent_path)[parent_length] = 0;
    return MZ_OK;
}

static int32_t mz_os_join_path_wide(const wchar_t *parent_path, const wchar_t *name, wchar_t **path) {
    size_t parent_length = 0;
    size_t name_length = 0;
    size_t path_length = 0;
    uint8_t append_slash = 0;

    if (!parent_path || !name || !path)
        return MZ_PARAM_ERROR;
    *path = NULL;

    parent_length = wcslen(parent_path);
    name_length = wcslen(name);
    append_slash = parent_length > 0 && parent_path[parent_length - 1] != L'\\' &&
                   parent_path[parent_length - 1] != L'/';
    if (parent_length > (size_t)-1 - name_length - append_slash - 1)
        return MZ_MEM_ERROR;
    path_length = parent_length + append_slash + name_length + 1;
    *path = (wchar_t *)malloc(path_length * sizeof(wchar_t));
    if (!*path)
        return MZ_MEM_ERROR;

    memcpy(*path, parent_path, parent_length * sizeof(wchar_t));
    if (append_slash)
        (*path)[parent_length++] = L'\\';
    memcpy(*path + parent_length, name, (name_length + 1) * sizeof(wchar_t));
    return MZ_OK;
}

static int32_t mz_os_get_reparse_target_wide(const wchar_t *path, wchar_t **target_path, uint8_t *relative) {
    MZ_REPARSE_DATA_BUFFER *reparse_data = NULL;
    const wchar_t *path_buffer = NULL;
    wchar_t *result_path = NULL;
    DWORD length = 0;
    HANDLE handle = INVALID_HANDLE_VALUE;
    USHORT name_length = 0;
    USHORT name_offset = 0;
    uint8_t buffer[MAXIMUM_REPARSE_DATA_BUFFER_SIZE];
    uint8_t substitute_name = 0;
    size_t buffer_offset = 0;
    size_t result_length = 0;

    if (!path || !target_path || !relative)
        return MZ_PARAM_ERROR;
    *target_path = NULL;
    *relative = 0;

    handle = CreateFileW(path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                         FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    if (handle == INVALID_HANDLE_VALUE)
        return MZ_EXIST_ERROR;
    if (!DeviceIoControl(handle, FSCTL_GET_REPARSE_POINT, NULL, 0, buffer, sizeof(buffer), &length, NULL)) {
        CloseHandle(handle);
        return MZ_SYMLINK_ERROR;
    }
    CloseHandle(handle);

    reparse_data = (MZ_REPARSE_DATA_BUFFER *)buffer;
    if (reparse_data->ReparseTag == IO_REPARSE_TAG_SYMLINK) {
        *relative = (uint8_t)((reparse_data->SymbolicLinkReparseBuffer.Flags & SYMLINK_FLAG_RELATIVE) != 0);
        path_buffer = reparse_data->SymbolicLinkReparseBuffer.PathBuffer;
        name_length = reparse_data->SymbolicLinkReparseBuffer.PrintNameLength;
        name_offset = reparse_data->SymbolicLinkReparseBuffer.PrintNameOffset;
        if (name_length == 0) {
            name_length = reparse_data->SymbolicLinkReparseBuffer.SubstituteNameLength;
            name_offset = reparse_data->SymbolicLinkReparseBuffer.SubstituteNameOffset;
            substitute_name = 1;
        }
    } else if (reparse_data->ReparseTag == IO_REPARSE_TAG_MOUNT_POINT) {
        path_buffer = reparse_data->MountPointReparseBuffer.PathBuffer;
        name_length = reparse_data->MountPointReparseBuffer.PrintNameLength;
        name_offset = reparse_data->MountPointReparseBuffer.PrintNameOffset;
        if (name_length == 0) {
            name_length = reparse_data->MountPointReparseBuffer.SubstituteNameLength;
            name_offset = reparse_data->MountPointReparseBuffer.SubstituteNameOffset;
            substitute_name = 1;
        }
    } else {
        return MZ_SYMLINK_ERROR;
    }

    buffer_offset = (size_t)((const uint8_t *)path_buffer - buffer);
    if ((name_length % sizeof(wchar_t)) != 0 || buffer_offset > length || name_offset > length - buffer_offset ||
        name_length > length - buffer_offset - name_offset || name_length == 0)
        return MZ_SYMLINK_ERROR;
    path_buffer += name_offset / sizeof(wchar_t);
    result_length = name_length / sizeof(wchar_t);
    result_path = (wchar_t *)malloc((result_length + 1) * sizeof(wchar_t));
    if (!result_path)
        return MZ_MEM_ERROR;
    memcpy(result_path, path_buffer, result_length * sizeof(wchar_t));
    result_path[result_length] = 0;

    if (substitute_name && result_length >= 4 && wcsncmp(result_path, L"\\??\\", 4) == 0) {
        result_path[0] = L'\\';
        result_path[1] = L'\\';
        result_path[2] = L'?';
        result_path[3] = L'\\';
    }

    *target_path = result_path;
    return MZ_OK;
}

int32_t mz_os_get_replace_path(const char *target_path, char **replace_path) {
    const wchar_t *basename = NULL;
    wchar_t *current_path = NULL;
    wchar_t *final_parent = NULL;
    wchar_t *final_path = NULL;
    wchar_t *full_path = NULL;
    wchar_t *link_path = NULL;
    wchar_t *next_path = NULL;
    wchar_t *parent_path = NULL;
    wchar_t *target_path_wide = NULL;
    HANDLE handle = INVALID_HANDLE_VALUE;
    uint32_t link_count = 0;
    uint8_t relative = 0;
    int32_t err = MZ_OK;

    if (!target_path || !replace_path)
        return MZ_PARAM_ERROR;
    *replace_path = NULL;

    target_path_wide = mz_os_unicode_string_create(target_path, MZ_ENCODING_UTF8);
    if (!target_path_wide)
        return MZ_MEM_ERROR;
    err = mz_os_get_full_path_wide(target_path_wide, &current_path);
    if (err != MZ_OK)
        goto cleanup;

    while (link_count < 64) {
        handle = CreateFileW(current_path, FILE_READ_ATTRIBUTES,
                             FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                             FILE_FLAG_BACKUP_SEMANTICS, NULL);
        if (handle != INVALID_HANDLE_VALUE) {
            err = mz_os_get_final_path_wide(handle, current_path, &final_path);
            CloseHandle(handle);
            handle = INVALID_HANDLE_VALUE;
            if (err == MZ_OK) {
                *replace_path = mz_os_utf8_string_create_from_unicode(final_path, MZ_ENCODING_UTF8);
                if (!*replace_path)
                    err = MZ_MEM_ERROR;
            }
            goto cleanup;
        }

        err = mz_os_get_reparse_target_wide(current_path, &link_path, &relative);
        if (err == MZ_OK) {
            if (relative) {
                err = mz_os_get_parent_path_wide(current_path, &parent_path, &basename);
                if (err == MZ_OK)
                    err = mz_os_join_path_wide(parent_path, link_path, &next_path);
            } else {
                next_path = mz_os_wide_string_duplicate(link_path);
                if (!next_path)
                    err = MZ_MEM_ERROR;
            }
            if (err == MZ_OK)
                err = mz_os_get_full_path_wide(next_path, &full_path);
            if (err != MZ_OK)
                goto cleanup;

            free(current_path);
            current_path = full_path;
            full_path = NULL;
            free(link_path);
            link_path = NULL;
            free(next_path);
            next_path = NULL;
            free(parent_path);
            parent_path = NULL;
            link_count += 1;
            continue;
        }
        if (err != MZ_EXIST_ERROR)
            goto cleanup;

        err = mz_os_get_parent_path_wide(current_path, &parent_path, &basename);
        if (err != MZ_OK || !*basename)
            goto cleanup;
        handle = CreateFileW(parent_path, FILE_READ_ATTRIBUTES,
                             FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                             FILE_FLAG_BACKUP_SEMANTICS, NULL);
        if (handle == INVALID_HANDLE_VALUE) {
            err = MZ_EXIST_ERROR;
            goto cleanup;
        }
        err = mz_os_get_final_path_wide(handle, parent_path, &final_parent);
        CloseHandle(handle);
        handle = INVALID_HANDLE_VALUE;
        if (err == MZ_OK)
            err = mz_os_join_path_wide(final_parent, basename, &final_path);
        if (err == MZ_OK) {
            *replace_path = mz_os_utf8_string_create_from_unicode(final_path, MZ_ENCODING_UTF8);
            if (!*replace_path)
                err = MZ_MEM_ERROR;
        }
        goto cleanup;
    }

    err = MZ_SYMLINK_ERROR;

cleanup:
    if (handle != INVALID_HANDLE_VALUE)
        CloseHandle(handle);
    free(target_path_wide);
    free(parent_path);
    free(next_path);
    free(link_path);
    free(full_path);
    free(final_path);
    free(final_parent);
    free(current_path);
    return err;
}

static int32_t mz_os_copy_hard_link_target(const wchar_t *source_path, const wchar_t *replace_path,
                                            const BY_HANDLE_FILE_INFORMATION *expected_info) {
    BY_HANDLE_FILE_INFORMATION target_info;
    LARGE_INTEGER zero;
    HANDLE source_handle = INVALID_HANDLE_VALUE;
    HANDLE target_handle = INVALID_HANDLE_VALUE;
    DWORD bytes_read = 0;
    DWORD bytes_written = 0;
    DWORD total_written = 0;
    uint8_t buffer[UINT16_MAX];
    int32_t err = MZ_OK;

    source_handle = CreateFileW(source_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL, NULL);
    target_handle = CreateFileW(replace_path, GENERIC_WRITE | FILE_READ_ATTRIBUTES,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL, NULL);
    if (source_handle == INVALID_HANDLE_VALUE || target_handle == INVALID_HANDLE_VALUE) {
        err = MZ_OPEN_ERROR;
        goto cleanup;
    }
    if (!GetFileInformationByHandle(target_handle, &target_info) ||
        target_info.dwVolumeSerialNumber != expected_info->dwVolumeSerialNumber ||
        target_info.nFileIndexHigh != expected_info->nFileIndexHigh ||
        target_info.nFileIndexLow != expected_info->nFileIndexLow) {
        err = MZ_EXIST_ERROR;
        goto cleanup;
    }

    zero.QuadPart = 0;
    if (!SetFilePointerEx(target_handle, zero, NULL, FILE_BEGIN) || !SetEndOfFile(target_handle)) {
        err = MZ_INTERNAL_ERROR;
        goto cleanup;
    }

    do {
        if (!ReadFile(source_handle, buffer, sizeof(buffer), &bytes_read, NULL)) {
            err = MZ_INTERNAL_ERROR;
            break;
        }
        total_written = 0;
        while (total_written < bytes_read) {
            if (!WriteFile(target_handle, buffer + total_written, bytes_read - total_written, &bytes_written, NULL) ||
                bytes_written == 0) {
                err = MZ_INTERNAL_ERROR;
                break;
            }
            total_written += bytes_written;
        }
    } while (err == MZ_OK && bytes_read > 0);

    if (err == MZ_OK && !FlushFileBuffers(target_handle))
        err = MZ_INTERNAL_ERROR;

cleanup:
    if (source_handle != INVALID_HANDLE_VALUE)
        CloseHandle(source_handle);
    if (target_handle != INVALID_HANDLE_VALUE)
        CloseHandle(target_handle);
    if (err == MZ_OK && !DeleteFileW(source_path))
        err = MZ_EXIST_ERROR;
    return err;
}

int32_t mz_os_replace_resolved(const char *source_path, const char *replace_path) {
    BY_HANDLE_FILE_INFORMATION replace_info;
    wchar_t *source_path_wide = NULL;
    wchar_t *replace_path_wide = NULL;
    HANDLE replace_handle = INVALID_HANDLE_VALUE;
    DWORD replace_error = ERROR_SUCCESS;
    int32_t result = 0;
    int32_t err = MZ_OK;

    if (!source_path || !replace_path)
        return MZ_PARAM_ERROR;

    source_path_wide = mz_os_unicode_string_create(source_path, MZ_ENCODING_UTF8);
    replace_path_wide = mz_os_unicode_string_create(replace_path, MZ_ENCODING_UTF8);
    if (!source_path_wide || !replace_path_wide) {
        err = MZ_MEM_ERROR;
        goto cleanup;
    }

    replace_handle = CreateFileW(replace_path_wide, FILE_READ_ATTRIBUTES,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                                 FILE_ATTRIBUTE_NORMAL, NULL);
    if (replace_handle != INVALID_HANDLE_VALUE && GetFileInformationByHandle(replace_handle, &replace_info) &&
        replace_info.nNumberOfLinks > 1) {
        err = mz_os_copy_hard_link_target(source_path_wide, replace_path_wide, &replace_info);
        goto cleanup;
    }

    if (mz_os_file_exists(replace_path) == MZ_OK)
        result = ReplaceFileW(replace_path_wide, source_path_wide, NULL, 0, NULL, NULL);
    else
        result = MoveFileExW(source_path_wide, replace_path_wide, MOVEFILE_WRITE_THROUGH);

    if (result == 0) {
        replace_error = GetLastError();
        if (replace_error == ERROR_UNABLE_TO_MOVE_REPLACEMENT || replace_error == ERROR_UNABLE_TO_MOVE_REPLACEMENT_2)
            err = MZ_INTERNAL_ERROR;
        else
            err = MZ_EXIST_ERROR;
    }

cleanup:
    if (replace_handle != INVALID_HANDLE_VALUE)
        CloseHandle(replace_handle);
    if (source_path_wide)
        mz_os_unicode_string_delete(&source_path_wide);
    if (replace_path_wide)
        mz_os_unicode_string_delete(&replace_path_wide);
    return err;
}

int32_t mz_os_replace(const char *source_path, const char *target_path) {
    char *replace_path = NULL;
    int32_t err = MZ_OK;

    if (!source_path || !target_path)
        return MZ_PARAM_ERROR;

    err = mz_os_get_replace_path(target_path, &replace_path);
    if (err == MZ_OK)
        err = mz_os_replace_resolved(source_path, replace_path);

    free(replace_path);
    return err;
}

int32_t mz_os_unlink(const char *path) {
    wchar_t *path_wide = NULL;
    int32_t result = 0;

    if (!path)
        return MZ_PARAM_ERROR;
    path_wide = mz_os_unicode_string_create(path, MZ_ENCODING_UTF8);
    if (!path_wide)
        return MZ_PARAM_ERROR;

    if (mz_os_is_dir(path) == MZ_OK)
        result = RemoveDirectoryW(path_wide);
    else
        result = DeleteFileW(path_wide);

    mz_os_unicode_string_delete(&path_wide);

    if (result == 0)
        return MZ_EXIST_ERROR;

    return MZ_OK;
}

int32_t mz_os_file_exists(const char *path) {
    wchar_t *path_wide = NULL;
    DWORD attribs = 0;

    if (!path)
        return MZ_PARAM_ERROR;
    path_wide = mz_os_unicode_string_create(path, MZ_ENCODING_UTF8);
    if (!path_wide)
        return MZ_PARAM_ERROR;

    attribs = GetFileAttributesW(path_wide);
    mz_os_unicode_string_delete(&path_wide);

    if (attribs == 0xFFFFFFFF)
        return MZ_EXIST_ERROR;

    return MZ_OK;
}

int64_t mz_os_get_file_size(const char *path) {
    HANDLE handle = NULL;
    LARGE_INTEGER large_size;
    wchar_t *path_wide = NULL;

    if (!path)
        return MZ_PARAM_ERROR;
    path_wide = mz_os_unicode_string_create(path, MZ_ENCODING_UTF8);
    if (!path_wide)
        return MZ_PARAM_ERROR;
#if _WIN32_WINNT >= _WIN32_WINNT_WIN8
    handle = CreateFile2(path_wide, GENERIC_READ, 0, OPEN_EXISTING, NULL);
#else
    handle = CreateFileW(path_wide, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
#endif
    mz_os_unicode_string_delete(&path_wide);

    large_size.QuadPart = 0;

    if (handle != INVALID_HANDLE_VALUE) {
        GetFileSizeEx(handle, &large_size);
        CloseHandle(handle);
    }

    return large_size.QuadPart;
}

static void mz_os_file_to_unix_time(FILETIME file_time, time_t *unix_time) {
    uint64_t quad_file_time = 0;
    quad_file_time = file_time.dwLowDateTime;
    quad_file_time |= ((uint64_t)file_time.dwHighDateTime << 32);
    *unix_time = (time_t)((quad_file_time - 116444736000000000LL) / 10000000);
}

static void mz_os_unix_to_file_time(time_t unix_time, FILETIME *file_time) {
    uint64_t quad_file_time = 0;
    quad_file_time = ((uint64_t)unix_time * 10000000) + 116444736000000000LL;
    file_time->dwHighDateTime = (quad_file_time >> 32);
    file_time->dwLowDateTime = (uint32_t)(quad_file_time);
}

int32_t mz_os_get_file_date(const char *path, time_t *modified_date, time_t *accessed_date, time_t *creation_date) {
    WIN32_FILE_ATTRIBUTE_DATA wfad;
    wchar_t *path_wide = NULL;
    int32_t err = MZ_INTERNAL_ERROR;

    if (!path)
        return MZ_PARAM_ERROR;
    path_wide = mz_os_unicode_string_create(path, MZ_ENCODING_UTF8);
    if (!path_wide)
        return MZ_PARAM_ERROR;

    if (GetFileAttributesExW(path_wide, GetFileExInfoStandard, &wfad)) {
        if (modified_date)
            mz_os_file_to_unix_time(wfad.ftLastWriteTime, modified_date);
        if (accessed_date)
            mz_os_file_to_unix_time(wfad.ftLastAccessTime, accessed_date);
        if (creation_date)
            mz_os_file_to_unix_time(wfad.ftCreationTime, creation_date);

        err = MZ_OK;
    }

    free(path_wide);

    return err;
}

int32_t mz_os_set_file_date(const char *path, time_t modified_date, time_t accessed_date, time_t creation_date) {
    HANDLE handle = NULL;
    FILETIME ftm_creation, ftm_accessed, ftm_modified;
    wchar_t *path_wide = NULL;
    int32_t err = MZ_OK;

    if (!path)
        return MZ_PARAM_ERROR;
    path_wide = mz_os_unicode_string_create(path, MZ_ENCODING_UTF8);
    if (!path_wide)
        return MZ_PARAM_ERROR;

#if _WIN32_WINNT >= _WIN32_WINNT_WIN8
    handle = CreateFile2(path_wide, GENERIC_READ | GENERIC_WRITE, 0, OPEN_EXISTING, NULL);
#else
    handle = CreateFileW(path_wide, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
#endif
    mz_os_unicode_string_delete(&path_wide);

    if (handle != INVALID_HANDLE_VALUE) {
        GetFileTime(handle, &ftm_creation, &ftm_accessed, &ftm_modified);

        if (modified_date != 0)
            mz_os_unix_to_file_time(modified_date, &ftm_modified);
        if (accessed_date != 0)
            mz_os_unix_to_file_time(accessed_date, &ftm_accessed);
        if (creation_date != 0)
            mz_os_unix_to_file_time(creation_date, &ftm_creation);

        if (SetFileTime(handle, &ftm_creation, &ftm_accessed, &ftm_modified) == 0)
            err = MZ_INTERNAL_ERROR;

        CloseHandle(handle);
    }

    return err;
}

int32_t mz_os_get_file_attribs(const char *path, uint32_t *attributes) {
    wchar_t *path_wide = NULL;
    int32_t err = MZ_OK;

    if (!path || !attributes)
        return MZ_PARAM_ERROR;

    path_wide = mz_os_unicode_string_create(path, MZ_ENCODING_UTF8);
    if (!path_wide)
        return MZ_PARAM_ERROR;

    *attributes = GetFileAttributesW(path_wide);

    /* If target is a reparse point, open with default flags to get attributes */
    if (*attributes != INVALID_FILE_ATTRIBUTES && (*attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
        HANDLE handle = INVALID_HANDLE_VALUE;
        BY_HANDLE_FILE_INFORMATION info;

#if _WIN32_WINNT >= _WIN32_WINNT_WIN8
        CREATEFILE2_EXTENDED_PARAMETERS extended_params;

        memset(&extended_params, 0, sizeof(extended_params));
        extended_params.dwSize = sizeof(extended_params);
        extended_params.dwFileFlags = FILE_FLAG_BACKUP_SEMANTICS;

        handle = CreateFile2(path_wide, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, OPEN_EXISTING,
                             &extended_params);
#else
        handle = CreateFileW(path_wide, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                             FILE_FLAG_BACKUP_SEMANTICS, NULL);
#endif

        if (handle != INVALID_HANDLE_VALUE) {
            if (GetFileInformationByHandle(handle, &info))
                *attributes = info.dwFileAttributes;
            CloseHandle(handle);
        }
    }

    mz_os_unicode_string_delete(&path_wide);

    if (*attributes == INVALID_FILE_ATTRIBUTES)
        err = MZ_INTERNAL_ERROR;

    return err;
}

int32_t mz_os_set_file_attribs(const char *path, uint32_t attributes) {
    wchar_t *path_wide = NULL;
    int32_t err = MZ_OK;

    if (!path)
        return MZ_PARAM_ERROR;
    path_wide = mz_os_unicode_string_create(path, MZ_ENCODING_UTF8);
    if (!path_wide)
        return MZ_PARAM_ERROR;

    if (SetFileAttributesW(path_wide, attributes) == 0)
        err = MZ_INTERNAL_ERROR;
    mz_os_unicode_string_delete(&path_wide);

    return err;
}

int32_t mz_os_make_dir(const char *path) {
    wchar_t *path_wide = NULL;
    int32_t err = MZ_OK;

    if (!path)
        return MZ_PARAM_ERROR;

    /* Don't try to create a drive letter */
    if ((path[0] != 0) && (strlen(path) <= 3) && (path[1] == ':'))
        return mz_os_is_dir(path);

    path_wide = mz_os_unicode_string_create(path, MZ_ENCODING_UTF8);
    if (!path_wide)
        return MZ_PARAM_ERROR;

    if (CreateDirectoryW(path_wide, NULL) == 0) {
        if (GetLastError() != ERROR_ALREADY_EXISTS)
            err = MZ_INTERNAL_ERROR;
    }

    mz_os_unicode_string_delete(&path_wide);

    return err;
}

DIR *mz_os_open_dir(const char *path) {
    WIN32_FIND_DATAW find_data;
    DIR_int *dir_int = NULL;
    wchar_t *path_wide = NULL;
    char fixed_path[320];
    void *handle = NULL;

    if (!path)
        return NULL;

    strncpy(fixed_path, path, sizeof(fixed_path) - 1);
    fixed_path[sizeof(fixed_path) - 1] = 0;

    mz_path_append_slash(fixed_path, sizeof(fixed_path), MZ_PATH_SLASH_PLATFORM);
    mz_path_combine(fixed_path, "*", sizeof(fixed_path));

    path_wide = mz_os_unicode_string_create(fixed_path, MZ_ENCODING_UTF8);
    if (!path_wide)
        return NULL;

    handle = FindFirstFileW(path_wide, &find_data);
    mz_os_unicode_string_delete(&path_wide);

    if (handle == INVALID_HANDLE_VALUE)
        return NULL;

    dir_int = (DIR_int *)malloc(sizeof(DIR_int));
    if (!dir_int) {
        FindClose(handle);
        return NULL;
    }
    dir_int->find_handle = handle;
    dir_int->end = 0;

    memcpy(&dir_int->find_data, &find_data, sizeof(dir_int->find_data));

    return (DIR *)dir_int;
}

struct dirent *mz_os_read_dir(DIR *dir) {
    DIR_int *dir_int;

    if (!dir)
        return NULL;

    dir_int = (DIR_int *)dir;
    if (dir_int->end)
        return NULL;

    WideCharToMultiByte(CP_UTF8, 0, dir_int->find_data.cFileName, -1, dir_int->entry.d_name,
                        sizeof(dir_int->entry.d_name), NULL, NULL);

    if (FindNextFileW(dir_int->find_handle, &dir_int->find_data) == 0) {
        if (GetLastError() != ERROR_NO_MORE_FILES)
            return NULL;

        dir_int->end = 1;
    }

    return &dir_int->entry;
}

int32_t mz_os_close_dir(DIR *dir) {
    DIR_int *dir_int;

    if (!dir)
        return MZ_PARAM_ERROR;

    dir_int = (DIR_int *)dir;
    if (dir_int->find_handle != INVALID_HANDLE_VALUE)
        FindClose(dir_int->find_handle);
    free(dir_int);
    return MZ_OK;
}

int32_t mz_os_is_dir_separator(char c) {
    return c == '\\' || c == '/';
}

int32_t mz_os_is_dir(const char *path) {
    wchar_t *path_wide = NULL;
    uint32_t attribs = 0;

    if (!path)
        return MZ_PARAM_ERROR;
    path_wide = mz_os_unicode_string_create(path, MZ_ENCODING_UTF8);
    if (!path_wide)
        return MZ_PARAM_ERROR;

    attribs = GetFileAttributesW(path_wide);
    mz_os_unicode_string_delete(&path_wide);

    if (attribs != 0xFFFFFFFF) {
        if (attribs & FILE_ATTRIBUTE_DIRECTORY)
            return MZ_OK;
    }

    return MZ_EXIST_ERROR;
}

int32_t mz_os_is_symlink(const char *path) {
    wchar_t *path_wide = NULL;
    uint32_t attribs = 0;

    if (!path)
        return MZ_PARAM_ERROR;
    path_wide = mz_os_unicode_string_create(path, MZ_ENCODING_UTF8);
    if (!path_wide)
        return MZ_PARAM_ERROR;

    attribs = GetFileAttributesW(path_wide);
    mz_os_unicode_string_delete(&path_wide);

    if (attribs != 0xFFFFFFFF) {
        if (attribs & FILE_ATTRIBUTE_REPARSE_POINT)
            return MZ_OK;
    }

    return MZ_EXIST_ERROR;
}

int32_t mz_os_get_link_attribs(const char *path, uint32_t *attributes) {
    wchar_t *path_wide = NULL;
    int32_t err = MZ_OK;

    if (!path || !attributes)
        return MZ_PARAM_ERROR;

    path_wide = mz_os_unicode_string_create(path, MZ_ENCODING_UTF8);
    if (!path_wide)
        return MZ_PARAM_ERROR;

    *attributes = GetFileAttributesW(path_wide);
    mz_os_unicode_string_delete(&path_wide);

    if (*attributes == INVALID_FILE_ATTRIBUTES)
        err = MZ_INTERNAL_ERROR;

    return err;
}

int32_t mz_os_make_symlink(const char *path, const char *target_path) {
    typedef BOOLEAN(WINAPI * LPCREATESYMBOLICLINKW)(LPCWSTR, LPCWSTR, DWORD);
    MEMORY_BASIC_INFORMATION mbi;
    LPCREATESYMBOLICLINKW create_symbolic_link_w = NULL;
    HMODULE kernel32_mod = NULL;
    wchar_t *path_wide = NULL;
    wchar_t *target_path_wide = NULL;
    int32_t err = MZ_OK;
    int32_t flags = 0;

    if (!path)
        return MZ_PARAM_ERROR;

    /* Use VirtualQuery instead of GetModuleHandleW for UWP */
    memset(&mbi, 0, sizeof(mbi));
    VirtualQuery(VirtualQuery, &mbi, sizeof(mbi));
    kernel32_mod = (HMODULE)mbi.AllocationBase;

    if (!kernel32_mod)
        return MZ_SUPPORT_ERROR;

    create_symbolic_link_w = (LPCREATESYMBOLICLINKW)GetProcAddress(kernel32_mod, "CreateSymbolicLinkW");
    if (!create_symbolic_link_w) {
        return MZ_SUPPORT_ERROR;
    }

    path_wide = mz_os_unicode_string_create(path, MZ_ENCODING_UTF8);
    if (!path_wide) {
        return MZ_PARAM_ERROR;
    }

    target_path_wide = mz_os_unicode_string_create(target_path, MZ_ENCODING_UTF8);
    if (target_path_wide) {
        if (mz_path_has_slash(target_path) == MZ_OK)
            flags |= SYMBOLIC_LINK_FLAG_DIRECTORY;

        if (create_symbolic_link_w(path_wide, target_path_wide, flags) == FALSE)
            err = MZ_SYMLINK_ERROR;

        mz_os_unicode_string_delete(&target_path_wide);
    } else {
        err = MZ_PARAM_ERROR;
    }

    mz_os_unicode_string_delete(&path_wide);

    return err;
}

int32_t mz_os_read_symlink(const char *path, char *target_path, int32_t max_target_path) {
    wchar_t *path_wide = NULL;
    wchar_t *target_path_wide = NULL;
    int32_t err = MZ_OK;
    char *target_path_utf8 = NULL;
    uint8_t relative = 0;

    if (!path || !target_path || max_target_path <= 0)
        return MZ_PARAM_ERROR;
    path_wide = mz_os_unicode_string_create(path, MZ_ENCODING_UTF8);
    if (!path_wide)
        return MZ_PARAM_ERROR;

    err = mz_os_get_reparse_target_wide(path_wide, &target_path_wide, &relative);
    if (err == MZ_EXIST_ERROR)
        err = MZ_OPEN_ERROR;
    if (err == MZ_OK) {
        target_path_utf8 = mz_os_utf8_string_create_from_unicode(target_path_wide, MZ_ENCODING_UTF8);
        if (!target_path_utf8) {
            err = MZ_MEM_ERROR;
        } else {
            strncpy(target_path, target_path_utf8, max_target_path - 1);
            target_path[max_target_path - 1] = 0;
            /* Ensure directories have slash at the end so we can recreate them later */
            if (mz_os_is_dir(target_path_utf8) == MZ_OK)
                mz_path_append_slash(target_path, max_target_path, MZ_PATH_SLASH_PLATFORM);
        }
    }

    mz_os_unicode_string_delete(&path_wide);
    free(target_path_wide);
    mz_os_utf8_string_delete(&target_path_utf8);
    return err;
}

int32_t mz_os_get_temp_path(char *path, int32_t max_path, const char *prefix) {
    wchar_t *prefix_wide = NULL;
    wchar_t tmp_dir_wide[MAX_PATH];
    wchar_t tmp_path_wide[MAX_PATH];
    int32_t path_size = 0;

    if (!path || max_path <= 0)
        return MZ_PARAM_ERROR;

    if (GetTempPathW(MAX_PATH, tmp_dir_wide) == 0)
        return MZ_INTERNAL_ERROR;

    if (prefix) {
        prefix_wide = mz_os_unicode_string_create(prefix, MZ_ENCODING_UTF8);
        if (!prefix_wide)
            return MZ_MEM_ERROR;
    }

    if (GetTempFileNameW(tmp_dir_wide, prefix_wide ? prefix_wide : L"", 0, tmp_path_wide) == 0) {
        mz_os_unicode_string_delete(&prefix_wide);
        return MZ_INTERNAL_ERROR;
    }

    mz_os_unicode_string_delete(&prefix_wide);

    path_size = WideCharToMultiByte(CP_UTF8, 0, tmp_path_wide, -1, path, max_path, NULL, NULL);
    if (path_size == 0)
        return MZ_INTERNAL_ERROR;

    return MZ_OK;
}

uint64_t mz_os_ms_time(void) {
    SYSTEMTIME system_time;
    FILETIME file_time;
    uint64_t quad_file_time = 0;

    GetSystemTime(&system_time);
    SystemTimeToFileTime(&system_time, &file_time);

    quad_file_time = file_time.dwLowDateTime;
    quad_file_time |= ((uint64_t)file_time.dwHighDateTime << 32);

    return quad_file_time / 10000 - 11644473600000LL;
}
