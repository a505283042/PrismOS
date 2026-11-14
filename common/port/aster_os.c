/* SPDX-License-Identifier: MIT */
#include "aster_os.h"
#include <errno.h>
static const aster_os_ops_t *active=&aster_os_backend;
static bool sealed;
static bool valid(const aster_os_ops_t *p){return p&&p->size==sizeof(*p)&&p->version==ASTER_OS_API_VERSION&&p->name;}
const aster_os_ops_t *aster_os_ops(void){return active;}
int aster_os_bind(const aster_os_ops_t *p){if(sealed)return -EBUSY;if(!valid(p))return -EINVAL;active=p;return 0;}
void aster_os_seal(void){sealed=true;}
int aster_os_derive(aster_os_ops_t *out,const aster_os_ops_t *base,const aster_os_ops_t *overrides){
 if(!out||!valid(base)||!valid(overrides))return -EINVAL;
 aster_os_ops_t merged=*base;merged.name=overrides->name;
#define ASTER_OS_OP(ret,name,args,call,kind) if(overrides->name)merged.name=overrides->name;
#include "aster_os_ops.def"
#undef ASTER_OS_OP
 *out=merged;return 0;
}
#define MISSING_status return -ENOTSUP
#define MISSING_ptr return NULL
#define MISSING_void return
#define MISSING_u64 return 0
#define MISSING_u32 return 0
#define INVOKE_void(expr) do{expr;return;}while(0)
#define INVOKE_status(expr) return (expr)
#define INVOKE_ptr(expr) return (expr)
#define INVOKE_u64(expr) return (expr)
#define INVOKE_u32(expr) return (expr)
#define ASTER_OS_OP(ret,name,args,call,kind) ret aster_os_##name args {if(active->name){INVOKE_##kind(active->name call);}MISSING_##kind;}
#include "aster_os_ops.def"
#undef ASTER_OS_OP
