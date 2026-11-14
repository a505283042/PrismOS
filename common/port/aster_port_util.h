/* SPDX-License-Identifier: MIT */
#ifndef ASTER_PORT_UTIL_H
#define ASTER_PORT_UTIL_H
#include <stdint.h>
#include <stddef.h>
#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#endif
#ifndef MIN
#define MIN(a,b) ((a)<(b)?(a):(b))
#endif
#ifndef MAX
#define MAX(a,b) ((a)>(b)?(a):(b))
#endif
#ifndef CLAMP
#define CLAMP(v,a,b) MIN(MAX(v,a),b)
#endif
#ifndef BIT
#define BIT(n) (1u<<(n))
#endif
#ifndef ARG_UNUSED
#define ARG_UNUSED(v) ((void)(v))
#endif
#ifndef CONTAINER_OF
#define CONTAINER_OF(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
#endif
#ifndef BUILD_ASSERT
#define BUILD_ASSERT(condition,...) _Static_assert(condition,#condition)
#endif
#ifndef IS_ENABLED
#define ASTER_PORT_ENABLED_1 yes,
#define ASTER_PORT_ENABLED_EXPAND(value) ASTER_PORT_ENABLED_SELECT(ASTER_PORT_ENABLED_##value)
#define ASTER_PORT_ENABLED_SELECT(args) ASTER_PORT_ENABLED_RESULT(args 1,0,unused)
#define ASTER_PORT_ENABLED_RESULT(ignore,value,...) value
#define IS_ENABLED(value) ASTER_PORT_ENABLED_EXPAND(value)
#endif
#endif
