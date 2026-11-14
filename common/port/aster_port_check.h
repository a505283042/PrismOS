/* SPDX-License-Identifier: MIT */
#ifndef ASTER_PORT_CHECK_H
#define ASTER_PORT_CHECK_H
#include <stdint.h>
typedef struct {uint32_t passed;int error;const char *stage;} aster_port_check_t;
/* Explicit diagnostic only. Creates and removes its own unique SD test file;
 * never formats storage, changes profiles, or touches installed applications.
 * Run from a command/service task with no APP executing. */
int aster_port_check(aster_port_check_t *result);
#endif
