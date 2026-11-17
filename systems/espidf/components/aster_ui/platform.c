/* SPDX-License-Identifier: MIT */
#include "aster_platform.h"
#include "aster_idf_runtime.h"
#include "graphics.h"
#include "aster_mem.h"
#include "driver/ledc.h"
#include "sdkconfig.h"
#include <string.h>
int aster_plat_display_count(void) {return aster_idf_display_count();}
int aster_plat_display_orientation_get(void) {return ASTER_ORIENT_PORTRAIT;}
int aster_plat_display_orientation_set(int orientation)
{return orientation==ASTER_ORIENT_PORTRAIT?0:ASTER_ENOTSUP;}
int aster_plat_display_write_at_ex(const uint8_t *data,uint32_t x,uint32_t y,uint32_t w,uint32_t h,uint32_t pitch,bool swapped)
{
    if(!swapped)return aster_plat_display_write_at(data,x,y,w,h,pitch);
    if(!data || !w || !h || w>480 || h>480 || pitch<w)return ASTER_EINVAL;
    /* Restore the APP snapshot in one transfer. One DMA submission per row
     * serializes hundreds of interrupts even though the whole image is ready. */
    uint16_t *converted=aster_psram_alloc((size_t)w*h*2);
    if(converted) {
        for(uint32_t row=0;row<h;row++)for(uint32_t col=0;col<w;col++) {
            const uint8_t *p=data+((size_t)row*pitch+col)*2;
            converted[(size_t)row*w+col]=((uint16_t)p[0]<<8)|p[1];
        }
        int result=aster_plat_display_write_at((uint8_t *)converted,x,y,w,h,w);
        aster_psram_free(converted);return result;
    }
    /* Bounded fallback under memory pressure. */
    uint16_t line[480];
    for(uint32_t row=0;row<h;row++) {
        const uint8_t *p=data+(size_t)row*pitch*2;
        for(uint32_t col=0;col<w;col++)line[col]=((uint16_t)p[col*2]<<8)|p[col*2+1];
        int r=aster_plat_display_write_at((uint8_t *)line,x,y+row,w,1,w);if(r)return r;
    }
    return 0;
}
bool aster_plat_display_glass_frame(const uint8_t **data,uint32_t *w,uint32_t *h,uint32_t *pitch)
{
    static uint8_t *copy;static size_t capacity;
    aster_display_info_t info;
    if(aster_plat_display_get_info(0,&info))return false;
    const uint8_t *pixels=aster_idf_capture();if(!pixels)return false;
    size_t bytes=(size_t)info.width*info.height*2;
    if(bytes>capacity){aster_psram_free(copy);copy=aster_psram_alloc(bytes);capacity=copy?bytes:0;}
    if(!copy)return false;
    for(size_t i=0;i<bytes;i+=2){copy[i]=pixels[i+1];copy[i+1]=pixels[i];}
    *data=copy;*w=*pitch=info.width;*h=info.height;return true;
}
int aster_plat_input_poll(aster_touch_evt_t *out,int capacity)
{
    if(!capacity)return 0;
    aster_event_t event;if(!aster_idf_touch_event(&event))return 0;
    *out=(aster_touch_evt_t){.x=event.u.touch.x,.y=event.u.touch.y,.action=event.u.touch.action,.slot=event.u.touch.slot,
        .contact_mask=aster_idf_touch_mask()};return 1;
}
int aster_plat_input_keys_poll(aster_key_evt_t *out,int capacity) {(void)out;(void)capacity;return 0;}
int aster_plat_backlight_set(bool on,uint8_t level)
{return aster_idf_board_backlight(on,level);}
