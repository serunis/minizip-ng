/* mz_os_posix.c -- System functions for posix
   part of the minizip-ng project

   Copyright (C) Nathan Moinvaziri
     https://github.com/zlib-ng/minizip-ng

   This program is distributed under the terms of the same license as zlib.
   See the accompanying LICENSE file for the full text of the license.
*/

#include "mz.h"
#include "mz_config.h"
#include "mz_strm.h"
#include "mz_os.h"

#include <stdio.h> /* rename */
#include <errno.h>
#include <fcntl.h>
#if defined(HAVE_ICONV)
#  include <iconv.h>
#endif
#if defined(HAVE_ICU)
#  include <unicode/ucnv.h>
#endif
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>

#if defined(__APPLE__)
#  include <copyfile.h>
#elif defined(__linux__)
#  include <sys/xattr.h>
#endif

#ifndef _WIN32
#  include <utime.h>
#  include <unistd.h>
#endif
#if defined(__APPLE__)
#  include <mach/clock.h>
#  include <mach/mach.h>
#endif

#if defined(HAVE_GETRANDOM)
#  include <sys/random.h>
#endif
#if defined(HAVE_LIBBSD)
#  include <stdlib.h> /* arc4random_buf */
#endif

#ifndef MZ_PRESERVE_NATIVE_STRUCTURE
#  define MZ_PRESERVE_NATIVE_STRUCTURE 1
#endif

/***************************************************************************/

#if defined(HAVE_ICONV)
char *mz_os_utf8_string_create(const char *string, int32_t encoding) {
    iconv_t cd;
    /// up to CP2147483647
    char string_encoding[13];
    const char *from_encoding = NULL;
    size_t result = 0;
    size_t string_length = 0;
    size_t string_utf8_size = 0;
    char *string_utf8 = NULL;
    char *string_utf8_ptr = NULL;

    if (!string || encoding <= 0)
        return NULL;

    if (encoding == MZ_ENCODING_UTF8)
        from_encoding = "UTF-8";
    else {
        snprintf(string_encoding, sizeof(string_encoding), "CP%03" PRId32, encoding);
        from_encoding = string_encoding;
    }

    cd = iconv_open("UTF-8", from_encoding);
    if (cd == (iconv_t)-1)
        return NULL;

    string_length = strlen(string);
    string_utf8_size = string_length * 2;
    string_utf8 = (char *)calloc((int32_t)(string_utf8_size + 1), sizeof(char));
    string_utf8_ptr = string_utf8;

    if (string_utf8) {
        result = iconv(cd, (char **)&string, &string_length, (char **)&string_utf8_ptr, &string_utf8_size);
    }

    iconv_close(cd);

    if (result == (size_t)-1) {
        free(string_utf8);
        string_utf8 = NULL;
    }

    return string_utf8;
}
#elif defined(HAVE_ICU)
char *mz_os_utf8_string_create(const char *string, int32_t encoding) {
    char string_encoding[16];
    const char *from_encoding = NULL;
    int32_t string_length = 0;
    int32_t string_utf8_size = 0;
    char *string_utf8 = NULL;
    int32_t result = 0;
    UErrorCode status = U_ZERO_ERROR;

    if (!string || encoding <= 0)
        return NULL;

    if (encoding == MZ_ENCODING_UTF8)
        from_encoding = "UTF-8";
    else if (encoding == MZ_ENCODING_CODEPAGE_437)
        from_encoding = "ibm-437";
    else if (encoding == MZ_ENCODING_CODEPAGE_932)
        from_encoding = "windows-932-2000";
    else if (encoding == MZ_ENCODING_CODEPAGE_936)
        from_encoding = "windows-936-2000";
    else if (encoding == MZ_ENCODING_CODEPAGE_950)
        from_encoding = "windows-950-2000";
    else {
        snprintf(string_encoding, sizeof(string_encoding), "windows-%" PRId32 "-2000", encoding);
        from_encoding = string_encoding;
    }

    string_length = (int32_t)strlen(string);
    string_utf8_size = string_length * 4 + 1;
    string_utf8 = (char *)calloc(string_utf8_size, sizeof(char));

    if (!string_utf8)
        return NULL;

    result = ucnv_convert("UTF-8", from_encoding, string_utf8, string_utf8_size, string, string_length, &status);

    if (U_FAILURE(status) || result < 0) {
        free(string_utf8);
        string_utf8 = NULL;
    }

    return string_utf8;
}
#else
char *mz_os_utf8_string_create(const char *string, int32_t encoding) {
    return strdup(string);
}
#endif

void mz_os_utf8_string_delete(char **string) {
    if (string) {
        free(*string);
        *string = NULL;
    }
}

/* Gets the system default encoding; returns 0 because there is no system
   ANSI code page concept on posix platforms */
int32_t mz_os_get_default_encoding(void) {
    return 0;
}

/***************************************************************************/

#if defined(HAVE_GETRANDOM)
int32_t mz_os_rand(uint8_t *buf, int32_t size) {
    int32_t left = size;
    int32_t written = 0;

    while (left > 0) {
        written = getrandom(buf, left, 0);
        if (written < 0)
            return MZ_INTERNAL_ERROR;

        buf += written;
        left -= written;
    }
    return size - left;
}
#elif defined(HAVE_ARC4RANDOM_BUF)
int32_t mz_os_rand(uint8_t *buf, int32_t size) {
    if (size < 0)
        return 0;
    arc4random_buf(buf, (uint32_t)size);
    return size;
}
#elif defined(HAVE_ARC4RANDOM)
int32_t mz_os_rand(uint8_t *buf, int32_t size) {
    int32_t left = size;
    for (; left > 2; left -= 3, buf += 3) {
        uint32_t val = arc4random();

        buf[0] = (val) & 0xFF;
        buf[1] = (val >> 8) & 0xFF;
        buf[2] = (val >> 16) & 0xFF;
    }
    for (; left > 0; left--, buf++) {
        *buf = arc4random() & 0xFF;
    }
    return size - left;
}
#else
int32_t mz_os_rand(uint8_t *buf, int32_t size) {
    static unsigned calls = 0;
    int32_t i = 0;

    /* Ensure different random header each time */
    if (++calls == 1) {
#  define PI_SEED 3141592654UL
        srand((unsigned)(time(NULL) ^ PI_SEED));
    }

    while (i < size)
        buf[i++] = (rand() >> 7) & 0xff;

    return size;
}
#endif

int32_t mz_os_rename(const char *source_path, const char *target_path) {
    if (rename(source_path, target_path) == -1)
        return MZ_EXIST_ERROR;

    return MZ_OK;
}

static int32_t mz_os_path_join_alloc(const char *parent_path, const char *name, char **path) {
    size_t parent_size = 0;
    size_t name_size = 0;
    size_t path_size = 0;
    uint8_t append_slash = 0;

    if (!parent_path || !name || !path)
        return MZ_PARAM_ERROR;
    *path = NULL;

    parent_size = strlen(parent_path);
    name_size = strlen(name);
    append_slash = (parent_size > 0 && parent_path[parent_size - 1] != '/');
    if (parent_size > (size_t)-1 - name_size - append_slash - 1)
        return MZ_MEM_ERROR;

    path_size = parent_size + append_slash + name_size + 1;
    *path = (char *)malloc(path_size);
    if (!*path)
        return MZ_MEM_ERROR;
    memcpy(*path, parent_path, parent_size);
    if (append_slash)
        (*path)[parent_size++] = '/';
    memcpy(*path + parent_size, name, name_size + 1);
    return MZ_OK;
}

static int32_t mz_os_path_parent_realpath(const char *path, char **resolved_parent, const char **basename) {
    const char *slash = NULL;
    char *parent_path = NULL;
    size_t parent_size = 0;

    if (!path || !resolved_parent || !basename)
        return MZ_PARAM_ERROR;
    *resolved_parent = NULL;

    slash = strrchr(path, '/');
    if (slash) {
        *basename = slash + 1;
        parent_size = slash == path ? 1 : (size_t)(slash - path);
        parent_path = (char *)malloc(parent_size + 1);
        if (!parent_path)
            return MZ_MEM_ERROR;
        memcpy(parent_path, path, parent_size);
        parent_path[parent_size] = 0;
    } else {
        *basename = path;
        parent_path = (char *)strdup(".");
        if (!parent_path)
            return MZ_MEM_ERROR;
    }

    *resolved_parent = realpath(parent_path, NULL);
    free(parent_path);
    if (!*resolved_parent)
        return errno == ENOMEM ? MZ_MEM_ERROR : MZ_EXIST_ERROR;
    return MZ_OK;
}

static int32_t mz_os_read_symlink_alloc(const char *path, const struct stat *path_stat, char **link_path) {
    size_t link_size = 0;
    ssize_t link_length = 0;

    if (!path || !path_stat || !link_path)
        return MZ_PARAM_ERROR;
    *link_path = NULL;

    link_size = path_stat->st_size > 0 ? (size_t)path_stat->st_size + 1 : 256;
    while (1) {
        *link_path = (char *)malloc(link_size);
        if (!*link_path)
            return MZ_MEM_ERROR;
        link_length = readlink(path, *link_path, link_size - 1);
        if (link_length < 0) {
            free(*link_path);
            *link_path = NULL;
            return MZ_EXIST_ERROR;
        }
        if ((size_t)link_length < link_size - 1)
            break;
        free(*link_path);
        *link_path = NULL;
        if (link_size > (size_t)-1 / 2)
            return MZ_MEM_ERROR;
        link_size *= 2;
    }
    (*link_path)[link_length] = 0;
    return MZ_OK;
}

int32_t mz_os_get_replace_path(const char *target_path, char **replace_path) {
    const char *basename = NULL;
    struct stat path_stat;
    char *current_path = NULL;
    char *link_path = NULL;
    char *next_path = NULL;
    char *resolved_parent = NULL;
    uint32_t link_count = 0;
    int32_t err = MZ_OK;

    if (!target_path || !replace_path)
        return MZ_PARAM_ERROR;
    *replace_path = NULL;

    current_path = (char *)strdup(target_path);
    if (!current_path)
        return MZ_MEM_ERROR;

    while (link_count < 64) {
        if (lstat(current_path, &path_stat) == 0) {
            if (!S_ISLNK(path_stat.st_mode)) {
                *replace_path = realpath(current_path, NULL);
                if (!*replace_path)
                    err = errno == ENOMEM ? MZ_MEM_ERROR : MZ_EXIST_ERROR;
                goto cleanup;
            }

            err = mz_os_read_symlink_alloc(current_path, &path_stat, &link_path);
            if (err != MZ_OK)
                goto cleanup;
            if (link_path[0] == '/') {
                next_path = link_path;
                link_path = NULL;
            } else {
                err = mz_os_path_parent_realpath(current_path, &resolved_parent, &basename);
                if (err == MZ_OK)
                    err = mz_os_path_join_alloc(resolved_parent, link_path, &next_path);
                if (err != MZ_OK)
                    goto cleanup;
            }

            free(current_path);
            current_path = next_path;
            next_path = NULL;
            free(link_path);
            link_path = NULL;
            free(resolved_parent);
            resolved_parent = NULL;
            link_count += 1;
            continue;
        }

        if (errno != ENOENT && errno != ENOTDIR) {
            err = MZ_EXIST_ERROR;
            goto cleanup;
        }
        err = mz_os_path_parent_realpath(current_path, &resolved_parent, &basename);
        if (err == MZ_OK)
            err = mz_os_path_join_alloc(resolved_parent, basename, replace_path);
        goto cleanup;
    }

    err = MZ_SYMLINK_ERROR;

cleanup:
    free(resolved_parent);
    free(next_path);
    free(link_path);
    free(current_path);
    return err;
}

#if defined(__linux__)
static uint8_t mz_os_xattr_list_contains(const char *names, ssize_t names_size, const char *match) {
    const char *name = names;

    while (name && name < names + names_size) {
        if (strcmp(name, match) == 0)
            return 1;
        name += strlen(name) + 1;
    }
    return 0;
}

static int32_t mz_os_sync_xattrs(int source_fd, int target_fd) {
    char *name = NULL;
    char *source_names = NULL;
    char *names = NULL;
    void *source_value = NULL;
    void *value = NULL;
    ssize_t source_names_size = 0;
    ssize_t names_size = 0;
    ssize_t source_value_size = 0;
    ssize_t value_size = 0;
    int32_t err = MZ_OK;

    source_names_size = flistxattr(source_fd, NULL, 0);
    if (source_names_size < 0) {
        if (errno == ENOTSUP || errno == EOPNOTSUPP)
            return MZ_OK;
        return MZ_READ_ERROR;
    }
    if (source_names_size > 0) {
        source_names = (char *)malloc((size_t)source_names_size);
        if (!source_names)
            return MZ_MEM_ERROR;
        source_names_size = flistxattr(source_fd, source_names, (size_t)source_names_size);
        if (source_names_size < 0) {
            err = MZ_READ_ERROR;
            goto cleanup;
        }
    }

    names_size = flistxattr(target_fd, NULL, 0);
    if (names_size < 0) {
        if (errno == ENOTSUP || errno == EOPNOTSUPP)
            goto cleanup;
        err = MZ_READ_ERROR;
        goto cleanup;
    }
    if (names_size > 0) {
        names = (char *)malloc((size_t)names_size);
        if (!names) {
            err = MZ_MEM_ERROR;
            goto cleanup;
        }
        names_size = flistxattr(target_fd, names, (size_t)names_size);
        if (names_size < 0) {
            err = MZ_READ_ERROR;
            goto cleanup;
        }
    }

    name = source_names;
    while (name && name < source_names + source_names_size && err == MZ_OK) {
        if (!mz_os_xattr_list_contains(names, names_size, name) && fremovexattr(source_fd, name) != 0 &&
            errno != ENODATA)
            err = MZ_WRITE_ERROR;
        name += strlen(name) + 1;
    }

    name = names;
    while (name && name < names + names_size && err == MZ_OK) {
        value_size = fgetxattr(target_fd, name, NULL, 0);
        if (value_size < 0) {
            err = MZ_READ_ERROR;
            break;
        }
        value = malloc(value_size > 0 ? (size_t)value_size : 1);
        if (!value) {
            err = MZ_MEM_ERROR;
            break;
        }
        value_size = fgetxattr(target_fd, name, value, (size_t)value_size);
        if (value_size < 0)
            err = MZ_READ_ERROR;
        else if (fsetxattr(source_fd, name, value, (size_t)value_size, 0) != 0) {
            source_value_size = fgetxattr(source_fd, name, NULL, 0);
            if (source_value_size != value_size) {
                err = MZ_WRITE_ERROR;
            } else {
                source_value = malloc(value_size > 0 ? (size_t)value_size : 1);
                if (!source_value) {
                    err = MZ_MEM_ERROR;
                } else {
                    source_value_size = fgetxattr(source_fd, name, source_value, (size_t)value_size);
                    if (source_value_size != value_size || memcmp(source_value, value, (size_t)value_size) != 0)
                        err = MZ_WRITE_ERROR;
                }
            }
            free(source_value);
            source_value = NULL;
        }
        free(value);
        value = NULL;
        name += strlen(name) + 1;
    }

cleanup:
    free(source_value);
    free(value);
    free(source_names);
    free(names);
    return err;
}
#endif

static int32_t mz_os_copy_file_contents(int source_fd, int target_fd) {
    uint8_t buf[UINT16_MAX];
    ssize_t bytes_read = 0;
    ssize_t bytes_written = 0;
    ssize_t total_written = 0;

    if (lseek(source_fd, 0, SEEK_SET) < 0 || ftruncate(target_fd, 0) != 0)
        return MZ_INTERNAL_ERROR;

    do {
        bytes_read = read(source_fd, buf, sizeof(buf));
        if (bytes_read < 0 && errno == EINTR)
            continue;
        if (bytes_read < 0)
            return MZ_INTERNAL_ERROR;

        total_written = 0;
        while (total_written < bytes_read) {
            bytes_written = write(target_fd, buf + total_written, (size_t)(bytes_read - total_written));
            if (bytes_written < 0 && errno == EINTR)
                continue;
            if (bytes_written <= 0)
                return MZ_INTERNAL_ERROR;
            total_written += bytes_written;
        }
    } while (bytes_read > 0);

    if (fsync(target_fd) != 0)
        return MZ_INTERNAL_ERROR;
    return MZ_OK;
}

int32_t mz_os_replace_resolved(const char *source_path, const char *replace_path) {
    struct stat source_stat;
    struct stat target_stat;
    struct stat target_link_stat;
    struct stat target_write_stat;
    int source_fd = -1;
    int target_fd = -1;
    int target_write_fd = -1;
    int target_result = 0;
    int32_t err = MZ_OK;
    uint8_t replace_in_place = 0;

    if (!source_path || !replace_path)
        return MZ_PARAM_ERROR;

    target_result = lstat(replace_path, &target_link_stat);
    if (target_result == 0 && !S_ISLNK(target_link_stat.st_mode)) {
#if defined(O_NOFOLLOW)
        target_fd = open(replace_path, O_RDONLY | O_NOFOLLOW);
        source_fd = open(source_path, O_RDONLY | O_NOFOLLOW);
#else
        target_fd = open(replace_path, O_RDONLY);
        source_fd = open(source_path, O_RDONLY);
#endif
        if (target_fd < 0 || source_fd < 0)
            err = MZ_OPEN_ERROR;
        if (err == MZ_OK && fstat(target_fd, &target_stat) != 0)
            err = MZ_READ_ERROR;
        if (err == MZ_OK && fstat(source_fd, &source_stat) != 0)
            err = MZ_READ_ERROR;
        if (err == MZ_OK && target_stat.st_nlink > 1) {
#if defined(O_NOFOLLOW)
            target_write_fd = open(replace_path, O_WRONLY | O_NOFOLLOW);
#else
            target_write_fd = open(replace_path, O_WRONLY);
#endif
            if (target_write_fd < 0)
                err = MZ_OPEN_ERROR;
            if (err == MZ_OK && fstat(target_write_fd, &target_write_stat) != 0)
                err = MZ_READ_ERROR;
            if (err == MZ_OK && (target_write_stat.st_dev != target_stat.st_dev ||
                                 target_write_stat.st_ino != target_stat.st_ino))
                err = MZ_EXIST_ERROR;
            if (err == MZ_OK) {
                err = mz_os_copy_file_contents(source_fd, target_write_fd);
                replace_in_place = 1;
            }
        } else {
            if (err == MZ_OK &&
                (source_stat.st_uid != target_stat.st_uid || source_stat.st_gid != target_stat.st_gid) &&
                fchown(source_fd, target_stat.st_uid, target_stat.st_gid) != 0)
                err = MZ_WRITE_ERROR;
            if (err == MZ_OK && fchmod(source_fd, target_stat.st_mode) != 0)
                err = MZ_WRITE_ERROR;
#if defined(__APPLE__)
            if (err == MZ_OK && fcopyfile(target_fd, source_fd, NULL, COPYFILE_ACL | COPYFILE_XATTR) != 0)
                err = MZ_WRITE_ERROR;
#elif defined(__linux__)
            if (err == MZ_OK)
                err = mz_os_sync_xattrs(source_fd, target_fd);
#endif
        }
    } else if (target_result != 0 && errno != ENOENT) {
        err = MZ_EXIST_ERROR;
    }

    if (target_write_fd >= 0 && close(target_write_fd) != 0 && err == MZ_OK)
        err = MZ_INTERNAL_ERROR;
    if (source_fd >= 0)
        close(source_fd);
    if (target_fd >= 0)
        close(target_fd);
    if (err == MZ_OK && replace_in_place)
        err = mz_os_unlink(source_path);
    else if (err == MZ_OK)
        err = mz_os_rename(source_path, replace_path);

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

int32_t mz_os_path_same_fs(const char *path_a, const char *path_b) {
    struct stat sa, sb;
    if (!path_a || !path_b)
        return MZ_PARAM_ERROR;
    if (stat(path_a, &sa) != 0 || stat(path_b, &sb) != 0)
        return MZ_EXIST_ERROR;
    return (sa.st_dev == sb.st_dev) ? MZ_OK : MZ_EXIST_ERROR;
}

int32_t mz_os_path_same_file(const char *path_a, const char *path_b) {
    struct stat sa, sb;

    if (!path_a || !path_b)
        return MZ_PARAM_ERROR;
    if (stat(path_a, &sa) != 0 || stat(path_b, &sb) != 0)
        return MZ_EXIST_ERROR;
    return (sa.st_dev == sb.st_dev && sa.st_ino == sb.st_ino) ? MZ_OK : MZ_EXIST_ERROR;
}

int32_t mz_os_unlink(const char *path) {
    if (unlink(path) == -1)
        return MZ_EXIST_ERROR;

    return MZ_OK;
}

int32_t mz_os_file_exists(const char *path) {
    struct stat path_stat;

    memset(&path_stat, 0, sizeof(path_stat));
    if (stat(path, &path_stat) == 0)
        return MZ_OK;
    return MZ_EXIST_ERROR;
}

int64_t mz_os_get_file_size(const char *path) {
    struct stat path_stat;

    memset(&path_stat, 0, sizeof(path_stat));
    if (stat(path, &path_stat) == 0) {
        /* Stat returns size taken up by directory entry, so return 0 */
        if (S_ISDIR(path_stat.st_mode))
            return 0;

        return path_stat.st_size;
    }

    return 0;
}

int32_t mz_os_get_file_date(const char *path, time_t *modified_date, time_t *accessed_date, time_t *creation_date) {
    struct stat path_stat;
    char *name = NULL;
    int32_t err = MZ_INTERNAL_ERROR;

    memset(&path_stat, 0, sizeof(path_stat));

    if (strcmp(path, "-") != 0) {
        /* Not all systems allow stat'ing a file with / appended */
        name = strdup(path);
        mz_path_remove_slash(name);

        if (stat(name, &path_stat) == 0) {
            if (modified_date)
                *modified_date = path_stat.st_mtime;
            if (accessed_date)
                *accessed_date = path_stat.st_atime;
            /* Creation date not supported */
            if (creation_date)
                *creation_date = 0;

            err = MZ_OK;
        }

        free(name);
    }

    return err;
}

int32_t mz_os_set_file_date(const char *path, time_t modified_date, time_t accessed_date, time_t creation_date) {
    struct utimbuf ut;

    ut.actime = accessed_date;
    ut.modtime = modified_date;

    /* Creation date not supported */
    MZ_UNUSED(creation_date);

    if (utime(path, &ut) != 0)
        return MZ_INTERNAL_ERROR;

    return MZ_OK;
}

int32_t mz_os_get_file_attribs(const char *path, uint32_t *attributes) {
    struct stat path_stat;
    int32_t err = MZ_OK;

    memset(&path_stat, 0, sizeof(path_stat));
    if (stat(path, &path_stat) == -1)
        err = MZ_INTERNAL_ERROR;
    *attributes = path_stat.st_mode;
    return err;
}

int32_t mz_os_set_file_attribs(const char *path, uint32_t attributes) {
    int32_t err = MZ_OK;

    if (chmod(path, (mode_t)attributes) == -1)
        err = MZ_INTERNAL_ERROR;

    return err;
}

int32_t mz_os_make_dir(const char *path) {
    int32_t err = 0;

    err = mkdir(path, 0755);

    if (err != 0 && errno != EEXIST)
        return MZ_INTERNAL_ERROR;

    return MZ_OK;
}

DIR *mz_os_open_dir(const char *path) {
    return opendir(path);
}

struct dirent *mz_os_read_dir(DIR *dir) {
    if (!dir)
        return NULL;
    return readdir(dir);
}

int32_t mz_os_close_dir(DIR *dir) {
    if (!dir)
        return MZ_PARAM_ERROR;
    if (closedir(dir) == -1)
        return MZ_INTERNAL_ERROR;
    return MZ_OK;
}

int32_t mz_os_is_dir_separator(char c) {
#if MZ_PRESERVE_NATIVE_STRUCTURE
    // While not strictly adhering to 4.4.17.1,
    // this preserves UNIX filesystem structure.
    return c == '/';
#else
    // While strictly adhering to 4.4.17.1,
    // this corrupts UNIX filesystem structure (a filename with a '\\' will become a folder + a file).
    return c == '\\' || c == '/';
#endif
}

int32_t mz_os_is_dir(const char *path) {
    struct stat path_stat;

    memset(&path_stat, 0, sizeof(path_stat));
    stat(path, &path_stat);
    if (S_ISDIR(path_stat.st_mode))
        return MZ_OK;

    return MZ_EXIST_ERROR;
}

int32_t mz_os_is_symlink(const char *path) {
    struct stat path_stat;

    memset(&path_stat, 0, sizeof(path_stat));
    lstat(path, &path_stat);
    if (S_ISLNK(path_stat.st_mode))
        return MZ_OK;

    return MZ_EXIST_ERROR;
}

int32_t mz_os_get_link_attribs(const char *path, uint32_t *attributes) {
    struct stat path_stat;
    int32_t err = MZ_OK;

    memset(&path_stat, 0, sizeof(path_stat));
    if (lstat(path, &path_stat) == -1)
        err = MZ_INTERNAL_ERROR;
    *attributes = path_stat.st_mode;
    return err;
}

int32_t mz_os_make_symlink(const char *path, const char *target_path) {
#if !HAVE_SYMLINK
    return MZ_SUPPORT_ERROR;
#else
    if (symlink(target_path, path) != 0)
        return MZ_INTERNAL_ERROR;
    return MZ_OK;
#endif
}

int32_t mz_os_read_symlink(const char *path, char *target_path, int32_t max_target_path) {
#if !HAVE_READLINK
    return MZ_SUPPORT_ERROR;
#else
    size_t length = 0;

    length = (size_t)readlink(path, target_path, max_target_path - 1);
    if (length == (size_t)-1)
        return MZ_EXIST_ERROR;
    if (length >= (size_t)(max_target_path - 1))
        return MZ_BUF_ERROR;

    target_path[length] = 0;
    return MZ_OK;
#endif
}

int32_t mz_os_get_temp_path(char *path, int32_t max_path, const char *prefix) {
    const char *tmp_dir = NULL;
    char *temp_path;
    int32_t result = 0;

    if (!path || max_path <= 0)
        return MZ_PARAM_ERROR;

    tmp_dir = getenv("TMPDIR");
    if (!tmp_dir)
        tmp_dir = getenv("TMP");
    if (!tmp_dir)
        tmp_dir = getenv("TEMP");
    if (!tmp_dir)
        tmp_dir = "/tmp";

    /* Construct path for mkdtemp in the form <tmp_dir>/<prefix>XXXXXX */
    temp_path = (char *)calloc(max_path, sizeof(char));
    if (!temp_path)
        return MZ_MEM_ERROR;

    /* mkdtemp replaces XXXXXX with unique characters */
    result = snprintf(temp_path, max_path, "%s/%sXXXXXX", tmp_dir, prefix ? prefix : "");
    if (result < 0 || result >= max_path) {
        free(temp_path);
        return MZ_BUF_ERROR;
    }

    /* Create a temporary directory. */
    if (!mkdtemp(temp_path)) {
        free(temp_path);
        return MZ_INTERNAL_ERROR;
    }

    /* Create a filename inside the temporary directory using current time */
    result = snprintf(path, max_path, "%s/%" PRIdMAX "x", temp_path, (intmax_t)time(NULL));
    if (result < 0 || result >= max_path) {
        rmdir(temp_path);
        free(temp_path);
        return MZ_BUF_ERROR;
    }

    free(temp_path);
    return MZ_OK;
}

uint64_t mz_os_ms_time(void) {
    struct timespec ts;

#if defined(__APPLE__)
    clock_serv_t cclock;
    mach_timespec_t mts;

    host_get_clock_service(mach_host_self(), CALENDAR_CLOCK, &cclock);
    clock_get_time(cclock, &mts);
    mach_port_deallocate(mach_task_self(), cclock);

    ts.tv_sec = mts.tv_sec;
    ts.tv_nsec = mts.tv_nsec;
#elif !defined(_POSIX_MONOTONIC_CLOCK) || _POSIX_MONOTONIC_CLOCK < 0
    clock_gettime(CLOCK_REALTIME, &ts);
#elif _POSIX_MONOTONIC_CLOCK > 0
    clock_gettime(CLOCK_MONOTONIC, &ts);
#else
    if (sysconf(_SC_MONOTONIC_CLOCK) > 0)
        clock_gettime(CLOCK_MONOTONIC, &ts);
    else
        clock_gettime(CLOCK_REALTIME, &ts);
#endif

    return ((uint64_t)ts.tv_sec * 1000) + ((uint64_t)ts.tv_nsec / 1000000);
}
