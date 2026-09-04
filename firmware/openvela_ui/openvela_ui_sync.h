#ifndef OPENVELA_UI_SYNC_H
#define OPENVELA_UI_SYNC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Daily health/activity synchronization runs in its own worker thread.  The
 * UI only submits completed measurements; no LVGL object is ever accessed by
 * the worker.
 */
int openvela_ui_sync_start(void);
void openvela_ui_sync_stop(void);

void openvela_ui_sync_record_heart_rate(uint16_t bpm);
void openvela_ui_sync_record_blood_pressure(uint16_t systolic,
                                             uint16_t diastolic,
                                             uint16_t pulse);

#ifdef __cplusplus
}
#endif

#endif /* OPENVELA_UI_SYNC_H */
