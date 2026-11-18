/* SPDX-License-Identifier: MIT */
#pragma once
#include "aster_power.h"
typedef struct {
 int (*audio)(void *context,bool sleeping);
 int (*network)(void *context,bool suspend);
 int (*storage)(void *context,bool suspend);
 void (*ui)(void *context,const aster_power_request_t *request,unsigned stage,int result);
} aster_power_session_ops_t;
typedef struct {
 const aster_power_session_ops_t *ops;void *context;
 bool admission,network,storage;int recovery_error;
} aster_power_session_t;
/* Matching aster_power_lifecycle_t callbacks, shared by OS adapters. */
int aster_power_session_prepare(void *context,const aster_power_request_t *request);
void aster_power_session_finish(void *context,const aster_power_request_t *request,int result);

int aster_power_session_error(void *context);
