/* SPDX-License-Identifier: MIT */
#include "wasm_export.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>
#include "probe_blob.h"

static void *ext_alloc(unsigned n)
{
    return heap_caps_aligned_alloc(8, n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

static void *ext_resize(void *p, unsigned n)
{
    if (!n) {
        free(p);
        return NULL;
    }
    void *q = ext_alloc(n);
    if (q && p) {
        size_t old = heap_caps_get_allocated_size(p);
        memcpy(q, p, old < n ? old : n);
        free(p);
    }
    return q;
}

void fakepod_aot_probe(void)
{
    RuntimeInitArgs init = {0};
    init.mem_alloc_type = Alloc_With_Allocator;
    init.mem_alloc_option.allocator.malloc_func = ext_alloc;
    init.mem_alloc_option.allocator.realloc_func = ext_resize;
    init.mem_alloc_option.allocator.free_func = free;
    if (!wasm_runtime_full_init(&init)) {
        ESP_LOGE("s31_aot", "Runtime init failed");
        return;
    }
    char error[256] = {0};
    uint8_t *input = ext_alloc(sizeof(probe_blob));
    if (!input) {
        wasm_runtime_destroy();
        return;
    }
    memcpy(input, probe_blob, sizeof(probe_blob));
    wasm_module_t module = wasm_runtime_load(input, sizeof(probe_blob), error, sizeof(error));
    if (!module) {
        ESP_LOGE("s31_aot", "AOT load failed: %s", error);
        free(input);
        wasm_runtime_destroy();
        return;
    }
    wasm_module_inst_t instance =
        wasm_runtime_instantiate(module, 32768, 65536, error, sizeof(error));
    if (!instance)
        ESP_LOGE("s31_aot", "AOT instantiate failed: %s", error);
    else {
        wasm_exec_env_t env = wasm_runtime_create_exec_env(instance, 32768);
        wasm_function_inst_t fi = wasm_runtime_lookup_function(instance, "probe_i32"),
                             ff = wasm_runtime_lookup_function(instance, "probe_f32");
        uint32_t integer = 7, argument;
        float value = 6.f;
        memcpy(&argument, &value, 4);
        bool a = env && fi && wasm_runtime_call_wasm(env, fi, 1, &integer);
        bool b = env && ff && wasm_runtime_call_wasm(env, ff, 1, &argument);
        memcpy(&value, &argument, 4);
        ESP_LOGI("s31_aot", "AOT integer=%lu float=%.2f result=%s", (unsigned long)integer,
                 (double)value, a && b && integer == 70 && value == 4.25f ? "PASS" : "FAIL");
        if (env)
            wasm_runtime_destroy_exec_env(env);
        wasm_runtime_deinstantiate(instance);
    }
    wasm_runtime_unload(module);
    free(input);
    wasm_runtime_destroy();
}
