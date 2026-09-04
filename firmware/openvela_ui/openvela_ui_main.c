#include <nuttx/config.h>

#include <lvgl/lvgl.h>
#include <signal.h>
#include <unistd.h>
#include <uv.h>

#include "openvela_ui.h"
#include "openvela_ui_sync.h"
#include "openvela_ui_timesync.h"

static void run_ui_loop(uv_loop_t *loop, lv_nuttx_result_t *result)
{
    lv_nuttx_uv_t uv_info;
    lv_nuttx_uv_init_t uv_init_flags;
    void *data;

    uv_loop_init(loop);
    lv_memset(&uv_info, 0, sizeof(uv_info));
    uv_info.loop = loop;
    uv_info.disp = result->disp;
    uv_info.indev = result->indev;
#ifdef CONFIG_UINPUT_TOUCH
    uv_info.uindev = result->utouch_indev;
#endif

    /*
     * /dev/lcd0 does not provide a real POLLPRI VSYNC event.  Enabling the
     * generic VSYNC helper therefore falls back to a 33 ms timer before
     * every refresh, reducing a 16/17 ms animation timer to about 17 Hz
     * after rendering and SPI transfer.  The LCD path already refreshes on
     * its writable poll event, so leave VSYNC disabled for this application.
     */
    uv_init_flags = (lv_nuttx_uv_init_t)(
        LV_NUTTX_UV_INIT_ALL & ~LV_NUTTX_UV_INIT_VSYNC);
    data = lv_nuttx_uv_init_partial(&uv_info, uv_init_flags);
    uv_run(loop, UV_RUN_DEFAULT);
    lv_nuttx_uv_deinit(&data);
}

int main(int argc, FAR char *argv[])
{
    lv_nuttx_dsc_t info;
    lv_nuttx_result_t result;
    uv_loop_t ui_loop;

    LV_UNUSED(argc);
    LV_UNUSED(argv);
    lv_memset(&info, 0, sizeof(info));
    lv_memset(&result, 0, sizeof(result));
    lv_memset(&ui_loop, 0, sizeof(ui_loop));

    if (lv_is_initialized()) {
        LV_LOG_ERROR("LVGL is already initialized");
        return -1;
    }

    lv_init();
    lv_nuttx_dsc_init(&info);
    lv_nuttx_init(&info, &result);
    if (result.disp == NULL) {
        LV_LOG_ERROR("openvela_ui display initialization failed");
        lv_deinit();
        return 1;
    }

    openvela_ui_create();
    openvela_ui_timesync_start();
    openvela_ui_sync_start();
    run_ui_loop(&ui_loop, &result);

    openvela_ui_sync_stop();
    lv_nuttx_deinit(&result);
    lv_deinit();
    return 0;
}
