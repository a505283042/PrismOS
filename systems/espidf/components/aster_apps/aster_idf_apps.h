/* SPDX-License-Identifier: MIT */
#pragma once
int aster_idf_apps_start(void);
int aster_idf_apps_command(int argc,char **argv);
int aster_idf_apps_launch(const char *id);

#include <stdbool.h>
#include "aster_power.h"
extern const aster_power_ops_t aster_idf_power_base;
void aster_idf_power_register(void);
bool aster_idf_apps_busy(void);
