#ifndef __PACKAGES_DEMOS_OPENVELA_UI_OPENVELA_UI_TIMESYNC_H
#define __PACKAGES_DEMOS_OPENVELA_UI_OPENVELA_UI_TIMESYNC_H

#include <nuttx/config.h>

#ifdef CONFIG_OPENVELA_UI_HTTP_TIMESYNC
int openvela_ui_timesync_start(void);
#else
static inline int openvela_ui_timesync_start(void)
{
    return 0;
}
#endif

#endif
