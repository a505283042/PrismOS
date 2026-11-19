/* SPDX-License-Identifier: MIT */
#include "aster_network.h"
#include <errno.h>
static const aster_network_ops_t *active = &aster_network_backend;
static bool sealed;

static bool valid(const aster_network_ops_t *p)
{
    return p && p->size == sizeof(*p) && p->version == ASTER_NETWORK_API_VERSION && p->name;
}

const aster_network_ops_t *aster_network_ops(void)
{
    return active;
}

int aster_network_bind(const aster_network_ops_t *p)
{
    if (sealed)
        return -EBUSY;
    if (!valid(p))
        return -EINVAL;
    active = p;
    return 0;
}

void aster_network_seal(void)
{
    sealed = true;
}

int aster_network_derive(aster_network_ops_t *out, const aster_network_ops_t *base,
                         const aster_network_ops_t *overrides)
{
    if (!out || !valid(base) || !valid(overrides))
        return -EINVAL;
    aster_network_ops_t merged = *base;
    merged.name = overrides->name;
#define ASTER_NETWORK_OP(ret, name, args, call, kind) \
    if (overrides->name) \
        merged.name = overrides->name;
#include "aster_network_ops.def"
#undef ASTER_NETWORK_OP
    *out = merged;
    return 0;
}

#define MISSING_status return -ENOTSUP
#define MISSING_u32 return 0
#define MISSING_boolean return false
#define MISSING_void return
#define INVOKE_void(expr) \
    do { \
        expr; \
        return; \
    } while (0)
#define INVOKE_status(expr) return (expr)
#define INVOKE_u32(expr) return (expr)
#define INVOKE_boolean(expr) return (expr)
#define ASTER_NETWORK_OP(ret, name, args, call, kind) \
    ret aster_network_##name args \
    { \
        if (active->name) { \
            INVOKE_##kind(active->name call); \
        } \
        MISSING_##kind; \
    }
#include "aster_network_ops.def"
#undef ASTER_NETWORK_OP
