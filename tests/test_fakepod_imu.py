"""A failed first I2C transaction must not disable shaking until reboot."""
import unittest

from test_waveshare import ROOT, run_c


class FakePodImu(unittest.TestCase):
    def test_first_probe_and_later_read_errors_recover(self):
        board = (ROOT / "boards/fakepod_s31/idf/main/board_input.c").read_text()
        configure = board[board.index("static int imu_configure("):board.index("static void imu_init(")]
        read = board[board.index("int fakepod_imu_read("):board.index("esp_err_t fakepod_input_init(")]
        run_c(r"""
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
typedef unsigned TickType_t;
static bool imu = true, imu_lock = true, imu_ready, imu_retry_pending, fail_read, locked;
static unsigned imu_retry_at, imu_read_errors, now, probes, writes;
#define ESP_OK 0
#define pdTRUE 1
#define pdMS_TO_TICKS(ms) (ms)
#define ESP_LOGW(...) ((void)0)
static TickType_t xTaskGetTickCount(void) { return now; }
#define vTaskDelay(ticks) (now += (ticks))
static bool xSemaphoreTake(bool lock, int ticks) {
    assert(lock && !locked && ticks == 0);
    return locked = true;
}
static void xSemaphoreGive(bool lock) { assert(lock && locked); locked = false; }
static int write_register(bool device, uint8_t reg, uint8_t value) {
    assert(device && locked && reg); (void)value; writes++; return 0;
}
static int read_register(bool device, uint8_t reg, void *out, size_t len) {
    assert(device && locked);
    if (reg == 0) {
        assert(len == 1);
        *(uint8_t *)out = ++probes == 1 ? 0xff : 0x05;
        return 0;
    }
    assert(reg == 0x35 && len == 12);
    memset(out, 0, len);
    ((uint8_t *)out)[5] = 0x20;
    return fail_read ? -1 : 0;
}
""" + configure + read + r"""
int main(void) {
    float v[6];
    #define READ() fakepod_imu_read(v,v+1,v+2,v+3,v+4,v+5)
    assert(READ() == -ENODEV && !locked && probes == 1);
    now = 999; assert(READ() == -EAGAIN && probes == 1);
    now++; assert(READ() == 0 && probes == 2 && writes == 5 && imu_ready);
    assert(v[2] > 9.80f && v[2] < 9.81f);
    fail_read = true;
    for (int n = 0; n < 3; ++n) assert(READ() == -EIO && !locked);
    fail_read = false;
    assert(READ() == 0 && imu_ready && writes == 10 && probes == 3);
    return 0;
}
""")
