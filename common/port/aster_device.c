/* SPDX-License-Identifier: MIT */
#include "aster_device.h"
#include <errno.h>
#include <string.h>
static const aster_device_ops_t *active = &aster_device_backend;
static bool sealed;

static bool valid(const aster_device_ops_t *ops)
{
    return ops && ops->size == sizeof(*ops) && ops->version == ASTER_DEVICE_API_VERSION &&
           ops->name;
}

const aster_device_ops_t *aster_device_ops(void)
{
    return active;
}

int aster_device_bind(const aster_device_ops_t *ops)
{
    if (sealed)
        return -EBUSY;
    if (!valid(ops))
        return -EINVAL;
    active = ops;
    return 0;
}

void aster_device_seal(void)
{
    sealed = true;
}

int aster_device_derive(aster_device_ops_t *out, const aster_device_ops_t *base,
                        const aster_device_ops_t *overrides)
{
    if (!out || !valid(base) || !valid(overrides))
        return -EINVAL;
    /* Local copy permits out==base/out==overrides and commits only on success. */
    aster_device_ops_t merged = *base;
    merged.name = overrides->name;
#define ASTER_DEVICE_OP(ret, name, args, call, kind) \
    if (overrides->name) \
        merged.name = overrides->name;
#include "aster_device_ops.def"
#undef ASTER_DEVICE_OP
    *out = merged;
    return 0;
}

bool aster_device_has(aster_device_method_t method)
{
    switch (method) {
#define ASTER_DEVICE_OP(ret, name, args, call, kind) \
    case ASTER_DEVICE_METHOD_##name: \
        return active->name != NULL;
#include "aster_device_ops.def"
#undef ASTER_DEVICE_OP
    default:
        return false;
    }
}

/* Missing operations are explicit unsupported capabilities, never success.
 * Void notifications are no-ops; query outputs must only be consumed after
 * success (or when has(method) is true for void diagnostics). */
#define ASTER_DEVICE_MISSING_ptr return NULL
#define ASTER_DEVICE_MISSING_int return ASTER_ENOTSUP
#define ASTER_DEVICE_MISSING_bool return false
#define ASTER_DEVICE_MISSING_void return
#define ASTER_DEVICE_MISSING_uint32_t return 0
#define ASTER_DEVICE_INVOKE_void(expr) \
    do { \
        expr; \
        return; \
    } while (0)
#define ASTER_DEVICE_INVOKE_int(expr) return (expr)
#define ASTER_DEVICE_INVOKE_bool(expr) return (expr)
#define ASTER_DEVICE_INVOKE_ptr(expr) return (expr)
#define ASTER_DEVICE_INVOKE_uint32_t(expr) return (expr)
#define ASTER_DEVICE_OP(ret, name, args, call, kind) \
    ret aster_device_##name args \
    { \
        if (active->name) { \
            ASTER_DEVICE_INVOKE_##kind(active->name call); \
        } \
        ASTER_DEVICE_MISSING_##kind; \
    }
#include "aster_device_ops.def"
#undef ASTER_DEVICE_OP
