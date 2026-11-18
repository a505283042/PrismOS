"""Check the FAKE POD capture/restore contract and QSPI byte order."""
import unittest

from test_waveshare import ROOT, run_c


class FakePodDisplay(unittest.TestCase):
    def test_capture_restores_color_and_keeps_partial_neighbors(self):
        source = (ROOT / 'boards/fakepod_s31/idf/main/board_lcd.c').read_text()
        driver = source[source.index('static int transfer_snapshot('):
                        source.index('static esp_err_t set_brightness(')]
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#define FP_LCD_WIDTH 4
#define FP_LCD_HEIGHT 4
#define ESP_OK 0
#define portMAX_DELAY 100
static int panel = 1, completed;
static uint8_t retained[32], wire[32];
static uint8_t *snapshot = retained;
static uint16_t staging[16], *bounce = staging;
static bool panel_blank;
static unsigned transfers;
static void xSemaphoreTake(int semaphore, int timeout)
{
    (void)semaphore; (void)timeout;
}
static int esp_lcd_panel_draw_bitmap(int handle, unsigned x, unsigned y,
                                    unsigned right, unsigned bottom, const void *pixels)
{
    (void)handle;
    const uint8_t *src = pixels;
    for (unsigned row = y; row < bottom; row++, src += (right-x)*2)
        memcpy(wire+(row*FP_LCD_WIDTH+x)*2, src, (right-x)*2);
    transfers++;
    return ESP_OK;
}
''' + driver + r'''
int main(void)
{
    uint8_t app[32], saved[32];
    for (unsigned i = 0; i < sizeof(app); i += 2) {
        app[i] = 0x00; app[i+1] = 0xf8;
    }
    assert(!fakepod_lcd_present(app,0,0,4,4,4));
    memcpy(saved,fakepod_lcd_capture(),sizeof(saved));
    assert(!memcmp(saved,app,sizeof(app)));
    assert(wire[0]==0xf8 && wire[1]==0x00);
    memset(app,0,sizeof(app));
    assert(!fakepod_lcd_present(app,0,0,4,4,4)); /* panel covers the app */
    assert(!fakepod_lcd_present(saved,0,0,4,4,4));
    assert(wire[0]==0xf8 && wire[1]==0x00);
    unsigned before=transfers;
    panel_blank=true;
    const uint8_t blue[]={0x1f,0x00};
    assert(!fakepod_lcd_present(blue,1,1,1,1,1));
    assert(transfers==before);
    assert(!transfer_snapshot(0,0,4,4)); /* wake restages the latest image */
    assert(wire[10]==0x00 && wire[11]==0x1f);
    assert(wire[8]==0xf8 && wire[9]==0x00);
    return 0;
}
''')
