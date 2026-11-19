/* SPDX-License-Identifier: MIT */
#ifndef ASTER_DEPLOY_H
#define ASTER_DEPLOY_H
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    ASTER_DEPLOY_IDLE,
    ASTER_DEPLOY_RECEIVE,
    ASTER_DEPLOY_VERIFY,
    ASTER_DEPLOY_RECEIVED,
    ASTER_DEPLOY_CHECK,
    ASTER_DEPLOY_COPY,
    ASTER_DEPLOY_SYNC,
    ASTER_DEPLOY_COMMIT,
    ASTER_DEPLOY_DONE,
    ASTER_DEPLOY_ERROR
} aster_deploy_phase;

typedef struct {
    uint32_t generation;
    aster_deploy_phase phase, failed_phase;
    uint64_t done, total, since_ms;
    int error;
    char file[128];
} aster_deploy_snapshot;

/* Initialize once before producer/UI tasks. Notifications never touch LVGL. */
int aster_deploy_init(void);
void aster_deploy_begin(const char *file, uint64_t total, uint64_t offset);
void aster_deploy_update(aster_deploy_phase phase, uint64_t done, uint64_t total, const char *file);
void aster_deploy_finish(int error);
bool aster_deploy_get(aster_deploy_snapshot *out);
void aster_deploy_dismiss(uint32_t generation);
#endif
