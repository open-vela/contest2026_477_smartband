/* SPDX-License-Identifier: Apache-2.0 */
#include "mooncat_app_state.h"
#include <string.h>
#include <limits.h>

static int active(uint32_t phase) { return phase >= MC_SCHEDULED && phase <= MC_RECOVERING; }
static uint32_t add_sat(uint32_t a, uint32_t b) { return UINT32_MAX - a < b ? UINT32_MAX : a + b; }
void mc_init(struct mc_state *s, uint32_t now)
{ memset(s, 0, sizeof(*s)); s->version = MC_STATE_VERSION; s->next_id = 1; s->last_now = now; }
const struct mc_task *mc_find(const struct mc_state *s, uint32_t id)
{ size_t i; for (i=0; i<MC_TASK_CAPACITY; ++i) if (id && s->tasks[i].id == id) return &s->tasks[i]; return NULL; }
const struct mc_task *mc_next_card(const struct mc_state *s)
{
    size_t i;
    for (i=0; i<MC_TASK_CAPACITY; ++i) if (s->tasks[i].phase == MC_RECOVERING) return &s->tasks[i];
    for (i=0; i<MC_TASK_CAPACITY; ++i) if (s->tasks[i].phase == MC_SHOWN) return &s->tasks[i];
    for (i=0; i<MC_TASK_CAPACITY; ++i) if (s->tasks[i].phase == MC_QUEUED) return &s->tasks[i];
    return NULL;
}
int mc_validate(const struct mc_state *s)
{
    size_t i,j; unsigned recovering=0;
    if (s->version != MC_STATE_VERSION || !s->next_id || s->focus>1 || s->workout>1 || s->dnd>1) return MC_INVALID;
    for (i=0; i<MC_TASK_CAPACITY; ++i) {
        const struct mc_task *t=&s->tasks[i];
        if (!t->id) { struct mc_task empty={0}; if (memcmp(t,&empty,sizeof(empty))) return MC_INVALID; continue; }
        if (t->id>=s->next_id || !t->revision || !t->request_id || !t->due ||
            t->phase<MC_SCHEDULED || t->phase>MC_EXPIRED || t->duration<5 || t->duration>3600 || !t->requested_delay || t->requested_delay>86400 ||
            t->reason>MC_REASON_RESTART || t->last_op>MC_ACK_SHOWN || t->last_revision>=t->revision) return MC_INVALID;
        if (t->phase==MC_RECOVERING) { if (!t->recovery_end) return MC_INVALID; ++recovering; }
        for (j=0;j<i;++j) if (s->tasks[j].id==t->id || s->tasks[j].request_id==t->request_id) return MC_INVALID;
    }
    return recovering>1 ? MC_INVALID : MC_OK;
}
static int commit(struct mc_state *s, struct mc_state *next, mc_save_fn save, void *cookie)
{
    if (mc_validate(next)!=MC_OK) return MC_INVALID;
    if (!memcmp(s,next,sizeof(*s))) return MC_OK;
    if (save && save(next,cookie)) return MC_STORAGE;
    *s=*next; return MC_OK;
}
static void account(struct mc_state *s,uint32_t now)
{
    size_t i; int recovering=0;
    for(i=0;i<MC_TASK_CAPACITY;++i) if(s->tasks[i].phase==MC_RECOVERING) recovering=1;
    /* Wall-clock jumps and time while powered off are never counted as focus. */
    if (s->focus && !s->workout && !recovering && now>=s->last_now && now-s->last_now<=300)
        s->focus_seconds=add_sat(s->focus_seconds,now-s->last_now);
    s->last_now=now;
}
int mc_apply(struct mc_state *s,const struct mc_command *c,uint32_t now,
             uint32_t midnight,mc_save_fn save,void *cookie)
{
    struct mc_state n=*s; struct mc_task *t=NULL; size_t i;
    if (!now || !c || c->op<MC_SCHEDULE || c->op>MC_ACK_SHOWN) return MC_INVALID;
    account(&n,now);
    if (c->op==MC_SCHEDULE) {
        if (!c->request_id || c->delay<1 || c->delay>86400 || c->duration<5 || c->duration>3600 || now>UINT32_MAX-c->delay) return MC_INVALID;
        for (i=0;i<MC_TASK_CAPACITY;++i) if (n.tasks[i].request_id==c->request_id)
            return n.tasks[i].duration==c->duration && n.tasks[i].requested_delay==c->delay ? MC_DUPLICATE : MC_INVALID;
        /* Never evict an active task. Oldest terminal record is the bounded dedup window. */
        for (i=0;i<MC_TASK_CAPACITY;++i) if (!n.tasks[i].id) {t=&n.tasks[i];break;}
        if (!t) for (i=0;i<MC_TASK_CAPACITY;++i) if (!active(n.tasks[i].phase) && (!t || n.tasks[i].id<t->id)) t=&n.tasks[i];
        if (!t) return MC_FULL;
        if (n.next_id==UINT32_MAX) return MC_FULL;
        memset(t,0,sizeof(*t)); t->id=n.next_id++; t->revision=1;
        t->request_id=c->request_id; t->due=now+c->delay; t->duration=c->duration; t->phase=MC_SCHEDULED;t->requested_delay=c->delay;
    } else if (c->op>=MC_START) {
        for(i=0;i<MC_TASK_CAPACITY;++i) if(c->id && n.tasks[i].id==c->id) {t=&n.tasks[i];break;}
        if (!t) return MC_NOT_FOUND;
        if (t->last_op==c->op && t->last_revision==c->revision) return MC_DUPLICATE;
        if (c->revision!=t->revision) return MC_STALE;
        if (!active(t->phase)) return MC_STALE;
        if (t->revision==UINT32_MAX) return MC_FULL;
        switch(c->op) {
        case MC_ACK_SHOWN:
            if(t->phase!=MC_QUEUED) return MC_STALE;
            if(n.workout || n.dnd || n.pause_until>now) return MC_BUSY;
            for(i=0;i<MC_TASK_CAPACITY;++i) if(n.tasks[i].phase==MC_RECOVERING) return MC_BUSY;
            t->phase=MC_SHOWN; n.shown_count=add_sat(n.shown_count,1); break;
        case MC_START:
            if(t->phase!=MC_SHOWN || n.workout || n.dnd || n.pause_until>now) return MC_BUSY;
            for(i=0;i<MC_TASK_CAPACITY;++i) if(n.tasks[i].phase==MC_RECOVERING) return MC_BUSY;
            if(now>UINT32_MAX-t->duration) return MC_INVALID;
            t->phase=MC_RECOVERING; t->recovery_end=now+t->duration; break;
        case MC_SNOOZE:
            if ((t->phase!=MC_SHOWN && t->phase!=MC_QUEUED) || c->delay<1 || c->delay>86400 || now>UINT32_MAX-c->delay) return MC_INVALID;
            t->phase=MC_SCHEDULED; t->due=now+c->delay; break;
        case MC_PAUSE_TODAY:
            if(midnight<=now || midnight-now>90000) return MC_INVALID;
            n.pause_until=midnight; t->phase=MC_CANCELLED; break;
        case MC_COMPLETE:
            if(t->phase!=MC_RECOVERING) return MC_STALE;
            if(now<t->recovery_end) return MC_TOO_EARLY;
            t->phase=MC_DONE; n.completed_count=add_sat(n.completed_count,1); break;
        case MC_CANCEL: t->phase=MC_CANCELLED; break;
        default: return MC_INVALID;
        }
        t->last_op=c->op; t->last_revision=c->revision; ++t->revision;
    }
    switch(c->op) {
    case MC_FOCUS_ON: n.focus=1; break;
    case MC_FOCUS_OFF: n.focus=0; break;
    case MC_DND_ON: n.dnd=1; break;
    case MC_DND_OFF: n.dnd=0; break;
    case MC_WORKOUT_ON:
        n.workout=1;
        for(i=0;i<MC_TASK_CAPACITY;++i) if(n.tasks[i].phase==MC_RECOVERING) {
            if(n.tasks[i].revision==UINT32_MAX) return MC_FULL;
            n.tasks[i].phase=MC_CANCELLED;n.tasks[i].reason=MC_REASON_WORKOUT;++n.tasks[i].revision;
        }
        break;
    case MC_WORKOUT_OFF: n.workout=0; break;
    case MC_RESUME_TODAY: n.pause_until=0; break;
    default: break;
    }
    return commit(s,&n,save,cookie);
}
int mc_tick(struct mc_state *s,uint32_t now,mc_save_fn save,void *cookie)
{
    struct mc_state n=*s; size_t i; uint32_t reason=MC_REASON_NONE;
    if (!now) return MC_INVALID;
    if(now<s->last_now) {n.last_now=now; return commit(s,&n,save,cookie)==MC_OK ? MC_CLOCK_CHANGED : MC_STORAGE;}
    account(&n,now);
    if(n.pause_until>now) reason=MC_REASON_PAUSED;
    else if(n.workout) reason=MC_REASON_WORKOUT;
    else if(n.dnd) reason=MC_REASON_DND;
    else for(i=0;i<MC_TASK_CAPACITY;++i) if(n.tasks[i].phase==MC_RECOVERING) reason=MC_REASON_RECOVERY;
    for(i=0;i<MC_TASK_CAPACITY;++i) {
        struct mc_task *t=&n.tasks[i];
        if(t->phase<MC_SCHEDULED || t->phase>MC_SHOWN) continue;
        if(t->due>now) continue;
        if(t->revision==UINT32_MAX) return MC_FULL;
        if(now-t->due>3600) { t->phase=MC_EXPIRED;t->reason=MC_REASON_OVERDUE;++t->revision; }
        else if(reason) {t->phase=MC_SCHEDULED;t->reason=reason;t->due=add_sat(now,300);++t->revision;}
        else if(t->phase==MC_SCHEDULED) {t->phase=MC_QUEUED;t->reason=0;++t->revision;}
    }
    return commit(s,&n,save,cookie);
}
int mc_restore(struct mc_state *s,uint32_t now,mc_save_fn save,void *cookie)
{
    struct mc_state n=*s; size_t i;
    if(mc_validate(s)!=MC_OK || !now) return MC_INVALID;
    n.focus=0; n.workout=0; n.last_now=now;
    for(i=0;i<MC_TASK_CAPACITY;++i) {
        struct mc_task *t=&n.tasks[i];
        if(t->phase==MC_RECOVERING || t->phase==MC_SHOWN) {
            if(t->revision==UINT32_MAX) return MC_FULL;
            if(now>=t->due && now-t->due>3600) {
                t->phase=MC_EXPIRED;t->reason=MC_REASON_OVERDUE;
            } else {
                t->phase=MC_SCHEDULED;t->due=add_sat(now,60);t->reason=MC_REASON_RESTART;
            }
            ++t->revision;
        }
    }
    return commit(s,&n,save,cookie);
}
const char *mc_status_name(int code)
{
    static const char *names[]={"ok","duplicate","invalid","not_found","stale","busy","queue_full","storage_failed","too_early","clock_changed"};
    return code>=0 && code<=MC_CLOCK_CHANGED ? names[code] : "unknown";
}
