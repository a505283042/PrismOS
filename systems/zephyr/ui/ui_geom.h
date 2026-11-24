/* SPDX-License-Identifier: MIT */
#ifndef ASTER_UI_GEOM_H_
#define ASTER_UI_GEOM_H_
#include "aster_port_util.h"
/* Minimum logical UI height, independent of devicetree. Runtime layouts
 * obtain geometry from the display adapter. Smaller layouts require a new
 * quick-panel and gesture layout before lowering this contract. */
#define ASTER_UI_MIN_HEIGHT 240
#endif
