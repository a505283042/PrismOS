/* SPDX-License-Identifier: MIT */
/* ESP32-S31 Stage C diagnostic: production rendering stays untouched.
 * Compare cache-warm CPU memcpy with the public ESP-IDF DMA2D backend's
 * same-format RGB565 2D copy. Do not infer watts from elapsed microseconds.
 * Only invoke with media stopped and no running APP (enforced by UI command).
 */
#include "sdkconfig.h"
#if defined(CONFIG_IDF_TARGET_ESP32S31)
#include "stage_c_bench.h"
#include "ppa_fill.h"
#include "esp_async_color_convert.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdatomic.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

enum { COPY_MAX_EDGE=448, ALIGN_BYTES=64, POWER_SECONDS=8 };
static atomic_bool running = ATOMIC_VAR_INIT(false);
static const char *TAG="stagec";
typedef struct {
    uint16_t *src;
    uint16_t *dst;
    async_color_convert_handle_t dma;
} bench_ctx_t;

static esp_err_t dma_copy(bench_ctx_t *ctx, unsigned edge)
{
    const async_color_convert_request_t request = {
        .src_buffer=ctx->src, .src_stride=edge, .src_height=edge,
        .dst_buffer=ctx->dst, .dst_stride=edge, .dst_height=edge,
        .copy_width=edge, .copy_height=edge,
        .src_color_format=ESP_COLOR_FOURCC_RGB16,
        .dst_color_format=ESP_COLOR_FOURCC_RGB16,
    };
    /* ESP-IDF 6.1's public blocking wrapper handles cache maintenance
     * and waits for complete DMA2D output. It must not target WASM memory. */
    return esp_color_convert_blocking(ctx->dma,&request,-1);
}

static bool equal_pixels(const bench_ctx_t *ctx, unsigned edge)
{
    return memcmp(ctx->src,ctx->dst,(size_t)edge*edge*2u)==0;
}

static void benchmark_latency(bench_ctx_t *ctx)
{
    static const unsigned edges[]={64,128,256,448};
    static const unsigned repeats[]={80,50,25,12};
    printf("STAGEC BENCH begin fill_ready=%d dma2d=%d memory=PSRAM\n",
           aster_idf_ppa_fill_ready()?1:0,ctx->dma?1:0);
    for (unsigned case_id=0;case_id<sizeof(edges)/sizeof(edges[0]);case_id++) {
        unsigned edge=edges[case_id], reps=repeats[case_id];
        size_t size=(size_t)edge*edge*2u;
        uint64_t cpu_us=0,dma_us=0;
        uint32_t cpu_min=UINT32_MAX,dma_min=UINT32_MAX;
        bool cpu_ok=true,dma_ok=ctx->dma!=NULL;
        memset(ctx->dst,0,size);
        memcpy(ctx->dst,ctx->src,size);
        cpu_ok=equal_pixels(ctx,edge);
        if (ctx->dma) {
            memset(ctx->dst,0,size);
            esp_err_t status=dma_copy(ctx,edge);
            dma_ok=status==ESP_OK && equal_pixels(ctx,edge);
            if (!dma_ok)
                ESP_LOGW(TAG,"DMA2D warmup failed edge=%u err=%s",
                         edge,esp_err_to_name(status));
        }
        if (!cpu_ok || !dma_ok) {
            printf("STAGEC BENCH edge=%u bytes=%u verdict=%s cpu_ok=%d dma_ok=%d\n",
                   edge,(unsigned)size,ctx->dma?"FAIL":"DMA_UNAVAILABLE",
                   cpu_ok?1:0,dma_ok?1:0);
            if (ctx->dma && !dma_ok) break;
            continue;
        }
        /* Alternate test order across repetitions to reduce systematic
         * thermal and cache-order bias; verification stays outside timers. */
        for(unsigned i=0;i<reps;i++) {
            if (i&1u) {
                int64_t start=esp_timer_get_time();
                esp_err_t status=dma_copy(ctx,edge);
                uint32_t elapsed=(uint32_t)(esp_timer_get_time()-start);
                if (status!=ESP_OK) {dma_ok=false;break;}
                dma_us+=elapsed;if(elapsed<dma_min)dma_min=elapsed;
            }
            int64_t start=esp_timer_get_time();
            memcpy(ctx->dst,ctx->src,size);
            uint32_t elapsed=(uint32_t)(esp_timer_get_time()-start);
            cpu_us+=elapsed;if(elapsed<cpu_min)cpu_min=elapsed;
            if (!(i&1u)) {
                start=esp_timer_get_time();
                esp_err_t status=dma_copy(ctx,edge);
                elapsed=(uint32_t)(esp_timer_get_time()-start);
                if (status!=ESP_OK) {dma_ok=false;break;}
                dma_us+=elapsed;if(elapsed<dma_min)dma_min=elapsed;
            }
        }
        if (dma_ok) dma_ok=equal_pixels(ctx,edge);
        printf("STAGEC BENCH edge=%u bytes=%u reps=%u cpu_avg_us=%" PRIu64
               " dma2d_avg_us=%" PRIu64 " cpu_min_us=%" PRIu32
               " dma2d_min_us=%" PRIu32 " pixel_match=%d\n",
               edge,(unsigned)size,reps,cpu_us/reps,dma_us/reps,
               cpu_min,dma_min,dma_ok?1:0);
        if (!dma_ok) break;
        vTaskDelay(pdMS_TO_TICKS(25));
    }
    puts("STAGEC BENCH done; production copies remain CPU");
}

static void benchmark_power(bench_ctx_t *ctx,int mode)
{
    const unsigned edge=COPY_MAX_EDGE;
    const size_t size=(size_t)edge*edge*2u;
    const char *label=mode==1?"cpu":mode==2?"dma2d":"idle";
    if (mode==2 && !ctx->dma) {
        puts("STAGEC POWER dma2d unavailable; not measured");
        return;
    }
    if (mode!=3) {
        /* Explicitly verify a full-sized copy before the power interval. */
        memset(ctx->dst,0,size);
        esp_err_t status=mode==2?dma_copy(ctx,edge):ESP_OK;
        if (mode==1) memcpy(ctx->dst,ctx->src,size);
        if (status!=ESP_OK || !equal_pixels(ctx,edge)) {
            printf("STAGEC POWER aborted mode=%s pixel_check=FAIL err=%d\n",
                   label,(int)status);
            return;
        }
    }
    printf("STAGEC POWER begin mode=%s duration_s=%u bytes_per_copy=%u "
           "fill_ready=%d (external current meter required)\n",
           label,POWER_SECONDS,(unsigned)size,
           aster_idf_ppa_fill_ready()?1:0);
    int64_t start=esp_timer_get_time();
    int64_t stop=start+POWER_SECONDS*1000000LL;
    uint64_t completed=0;
    esp_err_t status=ESP_OK;
    while(esp_timer_get_time()<stop) {
        if (mode==1) memcpy(ctx->dst,ctx->src,size);
        else if (mode==2) {
            status=dma_copy(ctx,edge);
            if (status!=ESP_OK) break;
        } else vTaskDelay(pdMS_TO_TICKS(10));
        completed++;
        /* Low priority worker yields. Keep serial and UI responsive. */
        if ((completed&31u)==0u) taskYIELD();
    }
    uint64_t elapsed=(uint64_t)(esp_timer_get_time()-start);
    bool correct=(mode==3) || equal_pixels(ctx,edge);
    printf("STAGEC POWER end mode=%s elapsed_us=%" PRIu64
           " copies=%" PRIu64 " total_bytes=%" PRIu64
           " pixel_match=%d err=%d; current_ma=EXTERNAL_ONLY\n",
           label,elapsed,mode==3?0:completed,
           mode==3?0:completed*(uint64_t)size,
           correct?1:0,(int)status);
}

static void bench_worker(void *arg)
{
    const int mode=(int)(intptr_t)arg;
    bench_ctx_t ctx={0};
    size_t bytes=(size_t)COPY_MAX_EDGE*COPY_MAX_EDGE*2u;
    ctx.src=heap_caps_aligned_alloc(ALIGN_BYTES,bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    ctx.dst=heap_caps_aligned_alloc(ALIGN_BYTES,bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if (!ctx.src || !ctx.dst) {
        ESP_LOGW(TAG,"PSRAM allocation failed (%u bytes per buffer)",(unsigned)bytes);
        goto finish;
    }
    for (size_t i=0;i<bytes/sizeof(uint16_t);i++)
        ctx.src[i]=(uint16_t)((i*479u+((i>>5)*83u))&65535u);
    memset(ctx.dst,0,bytes);
    if (mode!=3) {
        async_color_convert_config_t config={
            .backlog=1,.dma_burst_size=64,.intr_priority=0
        };
        esp_err_t status=esp_async_color_convert_install_dma2d(&config,&ctx.dma);
        if (status!=ESP_OK)
            ESP_LOGW(TAG,"DMA2D unavailable: %s",esp_err_to_name(status));
    }
    if (mode==0) benchmark_latency(&ctx);
    else benchmark_power(&ctx,mode);
finish:
    if (ctx.dma) {
        esp_err_t status=esp_async_color_convert_uninstall(ctx.dma);
        if (status!=ESP_OK)
            ESP_LOGW(TAG,"DMA2D uninstall: %s",esp_err_to_name(status));
    }
    heap_caps_free(ctx.dst);
    heap_caps_free(ctx.src);
    atomic_store(&running,false);
    vTaskDelete(NULL);
}

int aster_idf_stage_c_start(int mode)
{
    if (mode<0 || mode>3) return -1;
    bool expected=false;
    if (!atomic_compare_exchange_strong(&running,&expected,true)) return -2;
    if (xTaskCreate(bench_worker,"stage_c_bench",6144,(void *)(intptr_t)mode,
                    tskIDLE_PRIORITY+1,NULL)!=pdPASS) {
        atomic_store(&running,false);
        return -3;
    }
    return 0;
}
#endif /* CONFIG_IDF_TARGET_ESP32S31 */
