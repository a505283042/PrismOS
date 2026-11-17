"""Exercise the actual LVGL preprocessor that arbitrates mixer strokes."""
import unittest
from test_waveshare import ROOT,run_c

class SliderAxis(unittest.TestCase):
 def test_vertical_never_reaches_slider_and_horizontal_remains_captured(self):
  source=(ROOT/'systems/zephyr/ui/launcher/flux.c').read_text(encoding='utf-8')
  guard=source[source.index('struct slider_axis_state'):source.index('static void slider_changed')]
  run_c(r"""
#include <assert.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#define MAX(a,b) ((a)>(b)?(a):(b))
typedef struct {int x,y;} lv_point_t;
typedef int lv_event_code_t;
typedef struct {int unused;} lv_indev_t;
typedef struct {int code;void *data;bool stopped;} lv_event_t;
enum {LV_INDEV_TYPE_POINTER,LV_INDEV_TYPE_ENCODER,LV_EVENT_PRESSED,LV_EVENT_PRESSING,LV_EVENT_RELEASED,LV_EVENT_PRESS_LOST,LV_EVENT_CLICKED};
static lv_indev_t pointer;static lv_point_t point;static int type=LV_INDEV_TYPE_POINTER;
static lv_indev_t *lv_indev_active(void){return &pointer;}
static int lv_indev_get_type(lv_indev_t *p){(void)p;return type;}
static void lv_indev_get_point(lv_indev_t *p,lv_point_t *out){(void)p;*out=point;}
static void *lv_event_get_user_data(lv_event_t *e){return e->data;}
static int lv_event_get_code(lv_event_t *e){return e->code;}
static void lv_event_stop_processing(lv_event_t *e){e->stopped=true;}
static int as(int n){return n;}
"""+guard+r"""
static bool feed(int kind,int event,int x,int y){
 point=(lv_point_t){x,y};lv_event_t e={event,&slider_axes[kind],false};slider_axis_guard(&e);return e.stopped;
}
static bool power_feed(int event,int x,int y){
 point=(lv_point_t){x,y};lv_event_t e={event,&power_button_strokes[0],false};power_button_guard(&e);return e.stopped;
}
int main(void){
 assert(!feed(0,LV_EVENT_PRESSED,100,100));
 assert(feed(0,LV_EVENT_PRESSING,102,97));
 assert(feed(0,LV_EVENT_PRESSING,112,75)); /* horizontal jitter during scroll */
 assert(feed(0,LV_EVENT_PRESSING,145,20));
 assert(feed(0,LV_EVENT_RELEASED,145,20));
 assert(!feed(0,LV_EVENT_PRESSED,100,100));
 assert(feed(0,LV_EVENT_PRESSING,115,102));
 assert(feed(0,LV_EVENT_PRESSING,115,102)); /* stationary timer repeat is not confirmation */
 assert(!feed(0,LV_EVENT_PRESSING,130,103));
 assert(!feed(0,LV_EVENT_PRESSING,140,130)); /* retain horizontal ownership */
 assert(!feed(0,LV_EVENT_RELEASED,140,130));
 assert(!feed(1,LV_EVENT_PRESSED,100,100));
 assert(feed(1,LV_EVENT_RELEASED,101,60)); /* fast swipe with no intermediate frame */
 assert(!feed(1,LV_EVENT_PRESSED,100,100));
 assert(!feed(1,LV_EVENT_RELEASED,101,101)); /* intentional tap still selects */
 assert(!feed(1,LV_EVENT_PRESSED,100,100));
 assert(feed(1,LV_EVENT_PRESSING,120,120)); /* ambiguous diagonal favors scrolling */
 assert(feed(1,LV_EVENT_PRESS_LOST,120,120));
 assert(!feed(0,LV_EVENT_PRESSED,100,100));
 assert(feed(0,LV_EVENT_PRESSING,120,103)); /* one early horizontal noise sample */
 assert(feed(0,LV_EVENT_PRESSING,121,70));
 assert(feed(0,LV_EVENT_RELEASED,121,40));
 assert(!feed(0,LV_EVENT_PRESSED,100,100));
 assert(feed(0,LV_EVENT_PRESSING,110,108));
 assert(feed(0,LV_EVENT_RELEASED,110,108)); /* ambiguous release must not jump */
 type=LV_INDEV_TYPE_ENCODER;assert(!feed(1,LV_EVENT_RELEASED,120,120));
 type=LV_INDEV_TYPE_POINTER;
 assert(!power_feed(LV_EVENT_PRESSED,10,10));
 assert(!power_feed(LV_EVENT_PRESSING,12,15));
 assert(!power_feed(LV_EVENT_RELEASED,20,60));
 assert(power_feed(LV_EVENT_CLICKED,20,60));
 assert(!power_feed(LV_EVENT_PRESSED,10,10));
 assert(!power_feed(LV_EVENT_RELEASED,11,11));
 assert(!power_feed(LV_EVENT_CLICKED,11,11));
 assert(!power_feed(LV_EVENT_PRESSED,10,10));
 assert(!power_feed(LV_EVENT_PRESS_LOST,10,10));
 assert(power_feed(LV_EVENT_CLICKED,10,10));
 return 0;
}
""")
