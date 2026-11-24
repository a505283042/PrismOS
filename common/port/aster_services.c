/* SPDX-License-Identifier: MIT */
#include "aster_services.h"
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
static const aster_services_ops_t *active = &aster_services_backend;
static bool sealed;

static bool valid(const aster_services_ops_t *p)
{
    return p && p->size == sizeof(*p) && p->version == ASTER_SERVICES_API_VERSION && p->name;
}

const aster_services_ops_t *aster_services_ops(void)
{
    return active;
}

int aster_services_bind(const aster_services_ops_t *p)
{
    if (sealed)
        return -EBUSY;
    if (!valid(p))
        return -EINVAL;
    active = p;
    return 0;
}

void aster_services_seal(void)
{
    sealed = true;
}

int aster_services_derive(aster_services_ops_t *out, const aster_services_ops_t *base,
                          const aster_services_ops_t *overrides)
{
    if (!out || !valid(base) || !valid(overrides))
        return -EINVAL;
    aster_services_ops_t merged = *base;
    merged.name = overrides->name;
#define ASTER_SERVICE_OP(ret, name, args, call, kind) \
    if (overrides->name) \
        merged.name = overrides->name;
#include "aster_services_ops.def"
#undef ASTER_SERVICE_OP
    *out = merged;
    return 0;
}

void aster_service_logf(unsigned level, const char *format, ...)
{
    if (!active->log)
        return;
    char message[384];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    active->log(level, message);
}

#define MISSING_status return -ENOTSUP
#define MISSING_boolean return false
#define MISSING_void return
#define INVOKE_void(expr) \
    do { \
        expr; \
        return; \
    } while (0)
#define INVOKE_status(expr) return (expr)
#define INVOKE_boolean(expr) return (expr)
#define ASTER_SERVICE_OP(ret, name, args, call, kind) \
    ret aster_service_##name args \
    { \
        if (active->name) { \
            INVOKE_##kind(active->name call); \
        } \
        MISSING_##kind; \
    }
#include "aster_services_ops.def"
#undef ASTER_SERVICE_OP
