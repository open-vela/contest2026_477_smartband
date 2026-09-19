#pragma once

/* Called only by the Native UI task, after lv_init()/before lv_deinit(). */
int openvela_ui_remote_start(void);
void openvela_ui_remote_stop(void);
