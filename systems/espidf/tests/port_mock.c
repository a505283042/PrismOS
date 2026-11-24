/* SPDX-License-Identifier: MIT */
/* Test-only host adapters. No radio, audio or real panel is represented. */
#include "aster_os.h"
#include "aster_services.h"
#include <lvgl.h>
const aster_os_ops_t aster_os_backend = {
    .size = sizeof(aster_os_ops_t), .version = ASTER_OS_API_VERSION, .name = "idf-test"};
const aster_services_ops_t aster_services_backend = {.size = sizeof(aster_services_ops_t),
                                                     .version = ASTER_SERVICES_API_VERSION,
                                                     .name = "idf-test"};

const lv_font_t *aster_system_font(unsigned px)
{
    (void)px;
    return LV_FONT_DEFAULT;
}
