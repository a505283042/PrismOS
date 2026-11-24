#ifndef ASTER_DEPLOY_UI_H
#define ASTER_DEPLOY_UI_H
#include <stdbool.h>
/* UI owner only; true consumes the idle pump while this overlay is present. */
bool aster_deploy_ui_tick(void);
void aster_deploy_ui_close(void);
#endif
