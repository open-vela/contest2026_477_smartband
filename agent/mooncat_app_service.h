/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "mooncat_app_state.h"
/* One shared service, with an independent worker in the NuttX flat build.
 * No LVGL, network, message-bus or cron calls while holding its mutex. */
int mooncat_app_command(const struct mc_command *command, struct mc_state *snapshot);
/* User-facing reminder: duplicates an active task with the same delay/duration.
 * Explicit schedule/request_id remains the API for retries across completion. */
int mooncat_app_remind(uint32_t delay, uint32_t duration, struct mc_state *snapshot);
int mooncat_app_poll(struct mc_state *snapshot);
int mooncat_app_snapshot(struct mc_state *snapshot);
/* UI transport: no filesystem work or state mutex waits on the caller.
 * Completed slots remain reserved until collected; overload returns MC_FULL. */
int mooncat_app_worker_start(void);
void mooncat_app_worker_stop(void);
int mooncat_app_submit(const struct mc_command *command, int remind, uint32_t *ticket);
int mooncat_app_result(uint32_t ticket, int *result);
int mooncat_app_cached(struct mc_state *snapshot);
