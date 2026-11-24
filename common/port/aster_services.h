/* SPDX-License-Identifier: MIT */
#ifndef ASTER_SERVICES_H
#define ASTER_SERVICES_H
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <aster/system.h>
#define ASTER_SERVICES_API_VERSION 1u
#define ASTER_SERVICE_MAX_APPS 64
#define ASTER_SERVICE_DESCRIPTION_MAX 128
/* Owned snapshot: valid after subsequent registry changes, with no borrowed
 * native record or lock leaking to the UI. */
typedef struct {
 char id[64],name[40],description[ASTER_SERVICE_DESCRIPTION_MAX],version[24],category[16];
 uint32_t state;
 bool has_icon,has_aot;
} aster_service_app_t;
typedef struct {
 uint32_t size,version;
 const char *name;
#define ASTER_SERVICE_OP(ret,name,args,call,kind) ret (*name) args;
#include "aster_services_ops.def"
#undef ASTER_SERVICE_OP
} aster_services_ops_t;
extern const aster_services_ops_t aster_services_backend;
const aster_services_ops_t *aster_services_ops(void);
int aster_services_bind(const aster_services_ops_t *ops);
void aster_services_seal(void);
void aster_service_logf(unsigned level,const char *format,...);
int aster_services_derive(aster_services_ops_t *out,const aster_services_ops_t *base,const aster_services_ops_t *overrides);
#define ASTER_SERVICE_OP(ret,name,args,call,kind) ret aster_service_##name args;
#include "aster_services_ops.def"
#undef ASTER_SERVICE_OP
#endif
