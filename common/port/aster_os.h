/* SPDX-License-Identifier: MIT */
#ifndef ASTER_OS_H
#define ASTER_OS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <aster/time.h>

#define ASTER_OS_API_VERSION 1u
#define ASTER_OS_WAIT_FOREVER UINT32_MAX

enum { ASTER_OS_MEM_EXTERNAL, ASTER_OS_MEM_INTERNAL };

enum {
    ASTER_OS_READ = 1,
    ASTER_OS_WRITE = 2,
    ASTER_OS_CREATE = 4,
    ASTER_OS_TRUNCATE = 8,
    ASTER_OS_APPEND = 16
};

enum { ASTER_OS_SEEK_SET, ASTER_OS_SEEK_CUR, ASTER_OS_SEEK_END };

enum { ASTER_OS_FILE = 1, ASTER_OS_DIRECTORY = 2 };

typedef struct {
    void *handle;
} aster_os_file_t;

typedef struct {
    void *handle;
} aster_os_dir_t;

static inline void aster_os_file_init(aster_os_file_t *file)
{
    file->handle = NULL;
}

static inline void aster_os_dir_init(aster_os_dir_t *dir)
{
    dir->handle = NULL;
}

typedef struct {
    uint64_t size;
    uint32_t type;
    char name[256];
} aster_os_entry_t;

typedef struct {
    uint64_t blocks, free_blocks;
    uint32_t block_size;
} aster_os_volume_t;

typedef struct {
    void *handle;
} aster_os_mutex_t;

typedef struct {
    void *handle;
} aster_os_sem_t;

typedef struct {
    void *handle;
} aster_os_task_t;

typedef void (*aster_os_task_fn)(void *context);

typedef struct {
    const char *name;
    size_t stack_bytes;
    /* 0 is highest priority on all adapters; core=-1 means no affinity. */
    unsigned priority;
    int core;
} aster_os_task_config_t;

/* Negative errno results; sizes/counts are nonnegative. Implementations map
 * opaque handles and the /SD: mount to their own filesystem, not vice versa.
 * File/dir/mutex/sem handles must start zeroed. File/dir close consumes the
 * handle even on an I/O error; mutex/sem destroy clears it on success.
 * An open handle is not copyable and callers serialize access to it.
 * Locks/semaphores are task-context only. Destroy requires no owners/waiters.
 * task_join releases its record only after success; a timed-out join retains
 * ownership and can be retried. No asynchronous kill operation is exposed. */
typedef struct {
    uint32_t size, version;
    const char *name;
#define ASTER_OS_OP(ret, name, args, call, kind) ret(*name) args;
#include "aster_os_ops.def"
#undef ASTER_OS_OP
} aster_os_ops_t;

extern const aster_os_ops_t aster_os_backend;
const aster_os_ops_t *aster_os_ops(void);
static inline uint32_t aster_os_uptime32(void);
int aster_os_bind(const aster_os_ops_t *ops);
void aster_os_seal(void);
int aster_os_derive(aster_os_ops_t *out, const aster_os_ops_t *base,
                    const aster_os_ops_t *overrides);
#define ASTER_OS_OP(ret, name, args, call, kind) ret aster_os_##name args;
#include "aster_os_ops.def"
#undef ASTER_OS_OP

static inline uint32_t aster_os_uptime32(void)
{
    return (uint32_t)aster_os_uptime_ms();
}

static inline uint64_t aster_os_elapsed_us(uint32_t microseconds)
{
    return microseconds;
}

static inline void *aster_os_alloc_external(size_t bytes)
{
    return aster_os_alloc(bytes, ASTER_OS_MEM_EXTERNAL);
}

static inline void aster_os_free_external(void *memory)
{
    aster_os_free(memory, ASTER_OS_MEM_EXTERNAL);
}
#endif
