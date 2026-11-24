/* SPDX-License-Identifier: MIT */
#include "aster_player_service.h"
#include <aster/error.h>
#include <errno.h>
#include <string.h>
static const aster_player_ops_t *backend;

int aster_player_service_register(const aster_player_ops_t *ops)
{
    if (!ops || !ops->info || !ops->control)
        return -EINVAL;
    if (backend)
        return -EBUSY;
    backend = ops;
    return 0;
}

int aster_player_service_available(void)
{
    return backend != NULL;
}

static int stable_error(int error)
{
    switch (error) {
    case 0:
        return ASTER_OK;
    case -EINVAL:
        return ASTER_EINVAL;
    case -ENOTSUP:
        return ASTER_ENOTSUP;
    case -ENOMEM:
        return ASTER_ENOMEM;
    case -ENODEV:
        return ASTER_ENODEV;
    case -EBUSY:
        return ASTER_EBUSY;
    case -ENOENT:
        return ASTER_ENOENT;
    case -EAGAIN:
        return ASTER_EAGAIN;
    case -ENOTCONN:
        return ASTER_ESTATE;
    case -ETIMEDOUT:
        return ASTER_ETIMEDOUT;
    default:
        return ASTER_EIO;
    }
}

int aster_player_service_info(aster_player_info_t *out)
{
    if (!out)
        return ASTER_EINVAL;
    if (!backend)
        return ASTER_ENOTSUP;
    aster_player_info_t info = {.size = sizeof(info), .version = 1};
    int e = backend->info(&info);
    if (e)
        return stable_error(e);
    info.error = stable_error(info.error);
    memcpy(out, &info, sizeof(info));
    return 0;
}

int aster_player_service_control(const aster_player_request_t *request)
{
    if (!request || request->size < sizeof(*request) || request->version != 1 ||
        request->command < ASTER_PLAYER_PLAY_FILE || request->command > ASTER_PLAYER_SET_QUEUE_MODE)
        return ASTER_EINVAL;
    if ((request->command == ASTER_PLAYER_PLAY_FILE ||
         request->command == ASTER_PLAYER_BT_CONNECT) &&
        (!request->path[0] || !memchr(request->path, 0, sizeof(request->path))))
        return ASTER_EINVAL;
    if (!backend)
        return ASTER_ENOTSUP;
    return stable_error(backend->control(request));
}

int aster_player_service_bt_list(aster_player_bt_list_t *out)
{
    if (!out)
        return ASTER_EINVAL;
    if (!backend || !backend->bt_list)
        return ASTER_ENOTSUP;
    aster_player_bt_list_t list = {.size = sizeof(list), .version = 1};
    int e = backend->bt_list(&list);
    if (e)
        return stable_error(e);
    if (list.count > ASTER_PLAYER_BT_DEVICES_MAX)
        return ASTER_EIO;
    list.error = stable_error(list.error);
    memcpy(out, &list, sizeof(list));
    return 0;
}
