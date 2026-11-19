/* SPDX-License-Identifier: MIT */
#include "aster_services.h"
#include "aster_app_mgr.h"
#include "aster_sysinfo.h"
#include "aster_kv.h"
#include "aster_storage.h"
#include "aster_runtime.h"
#include <errno.h>
#include <string.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(aster_port, LOG_LEVEL_INF);

static void port_log(unsigned level, const char *message)
{
    if (level <= 1) {
        LOG_ERR("%s", message);
    } else if (level == 2) {
        LOG_WRN("%s", message);
    } else {
        LOG_INF("%s", message);
    }
}

_Static_assert(ASTER_SERVICE_DESCRIPTION_MAX == ASTER_APP_DESCRIPTION_MAX,
               "description contract drift");
_Static_assert(ASTER_SERVICE_MAX_APPS == ASTER_APP_MAX_RECS, "registry capacity drift");

static void snapshot(aster_service_app_t *out, const aster_app_rec_t *in)
{
    memset(out, 0, sizeof(*out));
    memcpy(out->id, in->id, sizeof(out->id));
    memcpy(out->name, in->name, sizeof(out->name));
    memcpy(out->description, in->description, sizeof(out->description));
    memcpy(out->version, in->version, sizeof(out->version));
    memcpy(out->category, in->category, sizeof(out->category));
    out->state = in->state;
    out->has_icon = in->has_icon;
    out->has_aot = in->has_aot;
}

static bool app_snapshot(int index, aster_service_app_t *out)
{
    if (!out)
        return false;
    memset(out, 0, sizeof(*out));
    aster_app_rec_t native;
    if (!aster_app_mgr_snapshot(index, &native))
        return false;
    snapshot(out, &native);
    return true;
}

static bool app_find(const char *id, aster_service_app_t *out)
{
    if (!out || !id)
        return false;
    memset(out, 0, sizeof(*out));
    aster_app_rec_t native;
    if (!aster_app_mgr_find_snapshot(id, &native))
        return false;
    snapshot(out, &native);
    return true;
}

static int system_info(aster_system_info_t *out)
{
    if (!out)
        return -EINVAL;
    aster_sysinfo_get(out);
    return 0;
}

const aster_services_ops_t aster_services_backend = {
    .size = sizeof(aster_services_ops_t),
    .version = ASTER_SERVICES_API_VERSION,
    .name = "firmware",
    .system_info = system_info,
    .feature = aster_sysinfo_has_feature,
    .app_count = aster_app_mgr_count,
    .app_snapshot = app_snapshot,
    .app_find = app_find,
    .app_start = aster_runtime_launch_async,
    .app_queue_install = aster_app_mgr_queue_install,
    .app_uninstall = aster_app_mgr_uninstall,
    .registry_callback = aster_app_mgr_set_change_cb,
    .files_lock = aster_app_mgr_files_lock,
    .files_unlock = aster_app_mgr_files_unlock,
    .storage_mounted = aster_storage_mounted,
    .app_code_dir = aster_storage_app_code_dir,
    .kv_get = aster_kv_get,
    .kv_set = aster_kv_set,
    .kv_delete = aster_kv_delete,
    .log = port_log};
