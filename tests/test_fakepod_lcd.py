"""Check the production CO5300 presenter against a strict mock panel."""

import unittest
from test_waveshare import ROOT, run_c


class FakepodLCD(unittest.TestCase):
    def test_odd_partial_windows_preserve_neighbours(self):
        source = (ROOT / 'boards/fakepod_s31/idf/main/board_lcd.c').read_text(encoding='utf-8')
        present = source[
            source.index('static int transfer_snapshot(') : source.index(
                'const uint8_t *fakepod_lcd_capture'
            )
        ]
        run_c(
            r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#define FP_LCD_WIDTH 460
#define FP_LCD_HEIGHT 460
#define ESP_OK 0
#define portMAX_DELAY 0
static int panel=1,completed;
static bool panel_blank;
static uint8_t backing[460*460*2],glass[460*460*2];
static uint8_t *snapshot=backing;
static uint16_t bounce_storage[460*16],*bounce=bounce_storage;
static uint16_t expected[460*460],input[467*460];
static void xSemaphoreTake(int s,int t){(void)s;(void)t;}
static int esp_lcd_panel_draw_bitmap(int p,int x,int y,int right,int bottom,const void *data){
 assert(p==1 && !(x&1) && !(y&1) && !(right&1) && !(bottom&1));
 assert(x>=0 && y>=0 && right<=460 && bottom<=460 && bottom-y<=16);
 for(int row=y;row<bottom;row++)
  memcpy(glass+((size_t)row*460+x)*2,(const uint8_t *)data+(row-y)*(right-x)*2,(right-x)*2);
 return ESP_OK;
}
'''
            + present
            + r'''
static void update(unsigned x,unsigned y,unsigned w,unsigned h,unsigned pitch){
 for(unsigned row=0;row<h;row++)for(unsigned col=0;col<pitch;col++)input[row*pitch+col]=(uint16_t)rand();
 for(unsigned row=0;row<h;row++)for(unsigned col=0;col<w;col++)expected[(y+row)*460+x+col]=input[row*pitch+col];
 assert(fakepod_lcd_present((uint8_t *)input,x,y,w,h,pitch)==0);
 for(unsigned i=0;i<460*460;i++){
  uint16_t pixel=(uint16_t)(glass[i*2]<<8)|glass[i*2+1];assert(pixel==expected[i]);
 }
 assert(memcmp(expected,snapshot,sizeof(expected))==0);
}
int main(void){
 update(0,0,460,460,467);
 update(1,1,1,1,3);update(459,459,1,1,1);update(0,459,459,1,460);
 for(unsigned i=0;i<100;i++){
  unsigned x=rand()%460,y=rand()%460,w=1+rand()%(460-x),h=1+rand()%(460-y);
  update(x,y,w,h,w+7);
 }
 assert(fakepod_lcd_present((uint8_t *)input,459,0,2,1,2)<0);
 assert(fakepod_lcd_present((uint8_t *)input,0,459,1,2,1)<0);
 assert(fakepod_lcd_present((uint8_t *)input,0,0,0,1,1)<0);
 return 0;
}
'''
        )


if __name__ == '__main__':
    unittest.main()
