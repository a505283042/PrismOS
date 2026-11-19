/* SPDX-License-Identifier: MIT */
/* Shared WAMR boundary. Included after host buf/ctx/permission/sandbox helpers. */
#include "aster_player_service.h"
#include <errno.h>

static int32_t n_player_get_info(wasm_exec_env_t env, uint32_t off, uint32_t size)
{
    if (size < sizeof(aster_player_info_t))
        return ASTER_EINVAL;
    void *out = buf(env, off, sizeof(aster_player_info_t));
    if (!out)
        return ASTER_EINVAL;
    uint32_t header[2];
    memcpy(header, out, sizeof(header));
    if (header[0] < sizeof(aster_player_info_t) || header[1] != 1)
        return ASTER_EINVAL;
    aster_player_info_t info;
    int r = aster_player_service_info(&info);
    if (!r) {
        memcpy(out, &info, sizeof(info));
    }
    return r;
}

static int32_t n_player_control(wasm_exec_env_t env, uint32_t off, uint32_t size)
{
    if (size < sizeof(aster_player_request_t))
        return ASTER_EINVAL;
    const void *in = buf(env, off, sizeof(aster_player_request_t));
    if (!in)
        return ASTER_EINVAL;
    aster_player_request_t request;
    memcpy(&request, in, sizeof(request));
    if (request.size < sizeof(request) || request.version != 1)
        return ASTER_EINVAL;
    if (request.command == ASTER_PLAYER_PLAY_FILE) {
        aster_app_ctx_t *ctx = ctx_of(env);
        if (!ctx || !aster_ctx_perm(ctx, ASTER_PERM_FS))
            return ASTER_EPERM;
        if (!memchr(request.path, 0, sizeof(request.path)))
            return ASTER_EINVAL;
        char mapped[sizeof(request.path)];
        int r = sandbox_map(ctx, request.path, mapped, sizeof(mapped));
        if (r)
            return r == -EPERM ? ASTER_EPERM : ASTER_EINVAL;
        memset(request.path, 0, sizeof(request.path));
        memcpy(request.path, mapped, strlen(mapped) + 1);
    }
    return aster_player_service_control(&request);
}

static int32_t n_player_bt_list(wasm_exec_env_t env, uint32_t off, uint32_t size)
{
    if (size < sizeof(aster_player_bt_list_t))
        return ASTER_EINVAL;
    void *out = buf(env, off, sizeof(aster_player_bt_list_t));
    if (!out)
        return ASTER_EINVAL;
    uint32_t header[2];
    memcpy(header, out, sizeof(header));
    if (header[0] < sizeof(aster_player_bt_list_t) || header[1] != 1)
        return ASTER_EINVAL;
    aster_player_bt_list_t list;
    int r = aster_player_service_bt_list(&list);
    if (!r) {
        memcpy(out, &list, sizeof(list));
    }
    return r;
}
