/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdbool.h>
/* Stage C is diagnostics-only. No caller may use this for production copies.
 * 0 = latency/accuracy comparison; 1 = CPU power window; 2 = DMA2D power
 * window; 3 = idle baseline. Starts a low-priority worker, never runs inline
 * on the UI or serial console thread. */
int aster_idf_stage_c_start(int mode);
/* Queried by the UI Benchmark gate: no latency/power run may overlap the
 * launcher benchmark, otherwise PSRAM contention corrupts comparisons. */
bool aster_idf_stage_c_running(void);
