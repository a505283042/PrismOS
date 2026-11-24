/* SPDX-License-Identifier: MIT */
#ifndef ASTER_FIRMWARE_NETWORK_PORT_H
#define ASTER_FIRMWARE_NETWORK_PORT_H
#include "aster_net.h"
#include "aster_http.h"
#include "aster_mqtt.h"
#include "aster_ws.h"
#include "aster_net_event.h"
#define ASTER_NETWORK_API_VERSION 1u
/* Asynchronous, copied-input, owner-scoped transport contract. Read the
 * protocol headers for EAGAIN/EOF, partial writes and close-owner lifetimes.
 * TLS certificate verification remains the backend's responsibility; a
 * missing HTTPS-capable backend must fail instead of downgrading to HTTP. */
typedef struct {
 uint32_t size,version;
 const char *name;
#define ASTER_NETWORK_OP(ret,name,args,call,kind) ret (*name) args;
#include "aster_network_ops.def"
#undef ASTER_NETWORK_OP
} aster_network_ops_t;
extern const aster_network_ops_t aster_network_backend;
const aster_network_ops_t *aster_network_ops(void);
int aster_network_bind(const aster_network_ops_t *ops);
void aster_network_seal(void);
int aster_network_derive(aster_network_ops_t *out,const aster_network_ops_t *base,const aster_network_ops_t *overrides);
#define ASTER_NETWORK_OP(ret,name,args,call,kind) ret aster_network_##name args;
#include "aster_network_ops.def"
#undef ASTER_NETWORK_OP
#endif
