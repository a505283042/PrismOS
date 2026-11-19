/* SPDX-License-Identifier: MIT
 * Adapter shared by Zephyr and IDF's existing VFS compatibility layer.
 * Kernel/VFS structures never cross the public port interface. Included only
 * by the two OS adapters, not compiled as a standalone translation unit.
 */
#include "aster_os.h"
#include "aster_storage.h"
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/fs/fs.h>

static int file_open(aster_os_file_t *f, const char *path, unsigned flags)
{
    if (!f || !path || f->handle || !flags || (flags & ~31u))
        return -EINVAL;
    int mode = 0;
    if (flags & ASTER_OS_READ)
        mode |= FS_O_READ;
    if (flags & ASTER_OS_WRITE)
        mode |= FS_O_WRITE;
    if (flags & ASTER_OS_CREATE)
        mode |= FS_O_CREATE;
    if (flags & ASTER_OS_TRUNCATE)
        mode |= FS_O_TRUNC;
    if (flags & ASTER_OS_APPEND)
        mode |= FS_O_APPEND;
    struct fs_file_t *native = malloc(sizeof(*native));
    if (!native)
        return -ENOMEM;
    fs_file_t_init(native);
    int r = fs_open(native, path, mode);
    if (r) {
        free(native);
        return r;
    }
    f->handle = native;
    return 0;
}

static int file_close(aster_os_file_t *f)
{
    if (!f || !f->handle)
        return -EBADF;
    int r = fs_close(f->handle);
    free(f->handle);
    f->handle = NULL;
    return r;
}

static int file_read(aster_os_file_t *f, void *data, size_t bytes)
{
    if (!f || !f->handle || (!data && bytes) || bytes > INT_MAX)
        return -EINVAL;
    return aster_fs_read_safe(f->handle, data, bytes);
}

static int file_write(aster_os_file_t *f, const void *data, size_t bytes)
{
    if (!f || !f->handle || (!data && bytes) || bytes > INT_MAX)
        return -EINVAL;
    return aster_fs_write_safe(f->handle, data, bytes);
}

static int file_seek(aster_os_file_t *f, int64_t offset, unsigned whence)
{
    if (!f || !f->handle || whence > ASTER_OS_SEEK_END)
        return -EINVAL;
    if ((int64_t)(off_t)offset != offset)
        return -EOVERFLOW;
    int mode = whence == ASTER_OS_SEEK_SET   ? FS_SEEK_SET
               : whence == ASTER_OS_SEEK_CUR ? FS_SEEK_CUR
                                             : FS_SEEK_END;
    return fs_seek(f->handle, (off_t)offset, mode);
}

static int file_sync(aster_os_file_t *f)
{
    return f && f->handle ? fs_sync(f->handle) : -EBADF;
}

static void entry_copy(aster_os_entry_t *out, const struct fs_dirent *in)
{
    memset(out, 0, sizeof(*out));
    out->size = in->size;
    out->type = in->type == FS_DIR_ENTRY_DIR ? ASTER_OS_DIRECTORY : ASTER_OS_FILE;
    size_t n = sizeof(in->name);
    if (n >= sizeof(out->name))
        n = sizeof(out->name) - 1;
    memcpy(out->name, in->name, n);
}

static int port_stat(const char *path, aster_os_entry_t *out)
{
    if (!path || !out)
        return -EINVAL;
    memset(out, 0, sizeof(*out));
    struct fs_dirent native;
    int r = fs_stat(path, &native);
    if (!r)
        entry_copy(out, &native);
    return r;
}

static int volume(const char *path, aster_os_volume_t *out)
{
    if (!path || !out)
        return -EINVAL;
    memset(out, 0, sizeof(*out));
    struct fs_statvfs s;
    int r = fs_statvfs(path, &s);
    if (!r) {
        out->blocks = s.f_blocks;
        out->free_blocks = s.f_bfree;
        out->block_size = s.f_frsize;
    }
    return r;
}

static int dir_open(aster_os_dir_t *d, const char *path)
{
    if (!d || d->handle || !path)
        return -EINVAL;
    struct fs_dir_t *n = malloc(sizeof(*n));
    if (!n)
        return -ENOMEM;
    fs_dir_t_init(n);
    int r = fs_opendir(n, path);
    if (r) {
        free(n);
        return r;
    }
    d->handle = n;
    return 0;
}

static int dir_read(aster_os_dir_t *d, aster_os_entry_t *out)
{
    if (!d || !d->handle || !out)
        return -EINVAL;
    memset(out, 0, sizeof(*out));
    struct fs_dirent n;
    int r = fs_readdir(d->handle, &n);
    if (!r)
        entry_copy(out, &n);
    return r;
}

static int dir_close(aster_os_dir_t *d)
{
    if (!d || !d->handle)
        return -EBADF;
    int r = fs_closedir(d->handle);
    free(d->handle);
    d->handle = NULL;
    return r;
}

/* Included by each backend after defining its own clock/memory/lock hooks. */
#define ASTER_OS_FILE_BINDINGS \
    .file_open = file_open, .file_close = file_close, .file_read = file_read, \
    .file_write = file_write, .file_seek = file_seek, .file_sync = file_sync, .stat = port_stat, \
    .volume = volume, .dir_open = dir_open, .dir_read = dir_read, .dir_close = dir_close, \
    .mkdir = fs_mkdir, .unlink = fs_unlink, .rename = fs_rename
