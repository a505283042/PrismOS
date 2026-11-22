/* SPDX-License-Identifier: MIT */
#include "files.h"
#include "imgdec.h"
#include "aster_idf_runtime.h"
#include "esp_timer.h"
#include "hifi_cover_accel.h"
#include "sdkconfig.h"
#include <aster/aster.h>
#include <string.h>

static void *guest(wasm_exec_env_t env, uint32_t offset, uint32_t size)
{
    wasm_module_inst_t instance = wasm_runtime_get_module_inst(env);
    uint64_t end;
    if (!offset || !size || !wasm_runtime_get_app_addr_range(instance, offset, NULL, &end) ||
        (uint64_t)offset + size > end)
        return NULL;
    return wasm_runtime_addr_app_to_native(instance, offset);
}

static int32_t probe(wasm_exec_env_t env, uint32_t path, uint32_t length, uint32_t output,
                     uint32_t size)
{
    if (size < 8)
        return ASTER_EINVAL;
    size_t count = size < sizeof(aster_img_info_t) ? size : sizeof(aster_img_info_t);
    void *destination = guest(env, output, count);
    if (!destination)
        return ASTER_EINVAL;
    char real[256];
    int r = aster_idf_resolve_path(env, path, length, false, real);
    if (r)
        return r;
    aster_imgdec_info_t info = {0};
    r = aster_imgdec_probe(real, &info, 480, 480);
    aster_img_info_t value = {.size = sizeof(value),
                              .version = 1,
                              .fmt = info.fmt,
                              .src_w = info.src_w,
                              .src_h = info.src_h,
                              .file_size = info.file_size,
                              .out_w = info.out_w,
                              .out_h = info.out_h,
                              .flags = (info.unsupported ? 1u : 0u) | (info.too_big ? 2u : 0u)};
    memcpy(value.why, info.why, sizeof(value.why));
    value.why[sizeof(value.why) - 1] = 0;
    memcpy(destination, &value, count);
    return r;
}

static int32_t render(wasm_exec_env_t env, uint32_t path, uint32_t length, uint32_t offset,
                      uint32_t size)
{
    if (size < sizeof(aster_img_render_args_t))
        return ASTER_EINVAL;
    void *source = guest(env, offset, sizeof(aster_img_render_args_t));
    if (!source)
        return ASTER_EINVAL;
    aster_img_render_args_t args;
    memcpy(&args, source, sizeof(args));

    /* Cover V2 is strictly opt-in. It keeps the V1 function import and
     * prefix, so old firmware returns EINVAL and the APP can fall back. */
    if (args.version == ASTER_IMG_COVER_VERSION) {
        if (size < sizeof(aster_img_cover_render_args_t) ||
            args.size != sizeof(aster_img_cover_render_args_t))
            return ASTER_EINVAL;
        aster_img_cover_render_args_t v2;
        void *v2_addr = guest(env, offset, sizeof(v2));
        if (!v2_addr)
            return ASTER_EINVAL;
        memcpy(&v2, v2_addr, sizeof(v2));
        if (v2.reserved || v2.reserved2 || v2.flags & ~ASTER_IMG_F_NEAREST ||
            !v2.cap_w || !v2.cap_h || v2.cap_w > 480 || v2.cap_h > 480 ||
            (v2.src_offset && !v2.src_length))
            return ASTER_EINVAL;
        const uint64_t required = (uint64_t)v2.cap_w * v2.cap_h * 2u;
        if (v2.dst_span < required || v2.dim_span < required ||
            (v2.dst_off & 1u) || (v2.dim_off & 1u) ||
            !((uint64_t)v2.dst_off + required <= v2.dim_off ||
              (uint64_t)v2.dim_off + required <= v2.dst_off))
            return ASTER_EINVAL;
        uint16_t *original = guest(env, v2.dst_off, (uint32_t)required);
        uint16_t *dim = guest(env, v2.dim_off, (uint32_t)required);
        if (!original || !dim)
            return ASTER_EINVAL;
        char real[256];
        int r = aster_idf_resolve_path(env, path, length, false, real);
        if (r)
            return r;
        v2.out_w = v2.out_h = v2.result_flags = 0;
        r = aster_hifi_cover_render(real, v2.src_offset, v2.src_length,
                                    original, dim, v2.cap_w, v2.cap_h,
                                    (v2.flags & ASTER_IMG_F_NEAREST) != 0,
                                    &v2.out_w, &v2.out_h, &v2.result_flags);
        memcpy(v2_addr, &v2, sizeof(v2));
        return r;
    }

    /* Byte-for-byte legacy V1 path (BMP, PNG and software TJpgDec). */
    if (args.size < sizeof(args) || args.version != 1 || args.reserved ||
        args.flags & ~ASTER_IMG_F_NEAREST || !args.cap_w || !args.cap_h)
        return ASTER_EINVAL;
    uint64_t required = (uint64_t)args.cap_w * args.cap_h * 2;
    if (required > UINT32_MAX || (args.dst_span && args.dst_span < required))
        return ASTER_EINVAL;
    void *destination = guest(env, args.dst_off, (uint32_t)required);
    if (!destination)
        return ASTER_EINVAL;
    char real[256];
    int r = aster_idf_resolve_path(env, path, length, false, real);
    if (r)
        return r;
    args.dst_span = required;
    args.out_w = args.out_h = 0;
    uint32_t previous_ms = (uint32_t)(esp_timer_get_time() / 1000);
    r = aster_imgdec_render_monitored(real, destination, args.cap_w, args.cap_h, &args.out_w,
                                      &args.out_h, (args.flags & ASTER_IMG_F_NEAREST) != 0,
                                      aster_idf_image_progress, &previous_ms);
    memcpy(source, &args, sizeof(args));
    return r;
}

static NativeSymbol symbols[] = {{"aster_img_probe", (void *)probe, "(iiii)i", NULL},
                                 {"aster_img_render", (void *)render, "(iiii)i", NULL}};

NativeSymbol *aster_idf_image_symbols(uint32_t *count)
{
    *count = 2;
    return symbols;
}
