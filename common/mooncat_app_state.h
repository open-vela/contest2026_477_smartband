/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdint.h>
#include <stddef.h>

#define MC_TASK_CAPACITY 8
#define MC_STATE_VERSION 1
enum mc_status { MC_OK, MC_DUPLICATE, MC_INVALID, MC_NOT_FOUND, MC_STALE,
    MC_BUSY, MC_FULL, MC_STORAGE, MC_TOO_EARLY, MC_CLOCK_CHANGED };
enum mc_phase { MC_EMPTY, MC_SCHEDULED, MC_QUEUED, MC_SHOWN,
    MC_RECOVERING, MC_DONE, MC_CANCELLED, MC_EXPIRED };
enum mc_op { MC_SCHEDULE = 1, MC_FOCUS_ON, MC_FOCUS_OFF, MC_DND_ON,
    MC_DND_OFF, MC_WORKOUT_ON, MC_WORKOUT_OFF, MC_RESUME_TODAY,
    MC_START, MC_SNOOZE, MC_PAUSE_TODAY, MC_COMPLETE, MC_CANCEL, MC_ACK_SHOWN };
enum mc_reason { MC_REASON_NONE, MC_REASON_DND, MC_REASON_WORKOUT,
    MC_REASON_PAUSED, MC_REASON_RECOVERY, MC_REASON_OVERDUE, MC_REASON_RESTART };
struct mc_task {
    uint32_t id, revision, request_id, due, duration, phase, reason;
    uint32_t recovery_end, last_op, last_revision, requested_delay;
};
struct mc_state {
    uint32_t version, next_id, last_now, focus, workout, dnd, pause_until;
    uint32_t focus_seconds, shown_count, completed_count;
    struct mc_task tasks[MC_TASK_CAPACITY];
};
struct mc_command {
    uint32_t op, id, revision, request_id, delay, duration;
};
/* Caller serializes access. save must atomically replace a complete snapshot.
 * A failed save leaves the caller's state unchanged. NULL save is volatile mode. */
typedef int (*mc_save_fn)(const struct mc_state *, void *);
void mc_init(struct mc_state *s, uint32_t now);
int mc_validate(const struct mc_state *s);
int mc_apply(struct mc_state *s, const struct mc_command *c, uint32_t now,
             uint32_t next_local_midnight, mc_save_fn save, void *cookie);
int mc_tick(struct mc_state *s, uint32_t now, mc_save_fn save, void *cookie);
int mc_restore(struct mc_state *s, uint32_t now, mc_save_fn save, void *cookie);
const struct mc_task *mc_find(const struct mc_state *s, uint32_t id);
const struct mc_task *mc_next_card(const struct mc_state *s);
const char *mc_status_name(int status);
