/* SPDX-License-Identifier: Apache-2.0 */
#include "mooncat_app_service.h"
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#ifndef MOONCAT_APP_DIRECTORY
#define MOONCAT_APP_DIRECTORY "/data/etc/mooncat"
#endif
#define STATE_FILE MOONCAT_APP_DIRECTORY "/app-state-v1.txt"
#define TEMP_FILE MOONCAT_APP_DIRECTORY "/app-state-v1.tmp"
#define STATE_FIELDS(X) X(version) X(next_id) X(last_now) X(focus) X(workout) X(dnd) X(pause_until) X(focus_seconds) X(shown_count) X(completed_count)
#define TASK_FIELDS(X) X(id) X(revision) X(request_id) X(due) X(duration) X(phase) X(reason) X(recovery_end) X(last_op) X(last_revision) X(requested_delay)
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static struct mc_state state, disk_state;
static int initialized;
static uint32_t last_poll;
static uint32_t logical_base;
static uint64_t monotonic_base;
static int clock_ready;
static uint32_t wall_now(void);
static void publish_state(int result);

static uint64_t monotonic_seconds(void)
{
#ifdef MOONCAT_APP_TEST_CLOCK
    extern uint64_t mooncat_test_monotonic(void);
    return mooncat_test_monotonic();
#else
    struct timespec t;
    if(clock_gettime(CLOCK_MONOTONIC,&t)) return 0;
    return (uint64_t)t.tv_sec;
#endif
}
static uint32_t app_now(void)
{
    uint64_t now=monotonic_seconds(), elapsed;
    if(!clock_ready) return wall_now() ? wall_now() : 1;
    elapsed=now>=monotonic_base ? now-monotonic_base : 0;
    return elapsed>UINT32_MAX-logical_base ? UINT32_MAX : logical_base+(uint32_t)elapsed;
}

static int save_state(const struct mc_state *s,void *cookie)
{
    size_t i; int failed=0; FILE *f;
    (void)cookie;
    if(mkdir(MOONCAT_APP_DIRECTORY,0700) && errno!=EEXIST) return -1;
    f=fopen(TEMP_FILE,"w"); if(!f) return -1;
    /* Calendar anchor is metadata only. Runtime deadlines use monotonic time. */
    if(fprintf(f,"MCAPP2\n%lu\n",(unsigned long)wall_now())<0) failed=1;
#define WRITE_STATE(field) if(fprintf(f,"%lu\n",(unsigned long)s->field)<0) failed=1;
    STATE_FIELDS(WRITE_STATE)
#undef WRITE_STATE
    for(i=0;i<MC_TASK_CAPACITY;++i) {
#define WRITE_TASK(field) if(fprintf(f,"%lu\n",(unsigned long)s->tasks[i].field)<0) failed=1;
        TASK_FIELDS(WRITE_TASK)
#undef WRITE_TASK
    }
    if(fflush(f) || fsync(fileno(f))) failed=1;
    if(fclose(f)) failed=1;
    if(failed || rename(TEMP_FILE,STATE_FILE)) {unlink(TEMP_FILE); return -1;}
    disk_state=*s;
    return 0;
}
static int save_tick(const struct mc_state *s,void *cookie)
{
    struct mc_state significant=*s;
    significant.last_now=disk_state.last_now;
    significant.focus_seconds=disk_state.focus_seconds;
    /* Only checkpoint accumulated work, never the passage of idle time.
     * Keep transitions and clock rollback durable before committing memory. */
    if(!memcmp(&significant,&disk_state,sizeof(*s)) &&
       s->last_now>=state.last_now && s->last_now>=disk_state.last_now) {
        if(s->focus_seconds==disk_state.focus_seconds ||
           s->last_now-disk_state.last_now<60) return 0;
    }
    return save_state(s,cookie);
}
static int read_number(FILE *f,uint32_t *value)
{
    char line[32],*end; unsigned long number;
    if(!fgets(line,sizeof(line),f) || line[0]<'0' || line[0]>'9') return -1;
    errno=0;number=strtoul(line,&end,10);
    if(errno || number>UINT32_MAX || strcmp(end,"\n")) return -1;
    *value=(uint32_t)number;return 0;
}
static uint32_t wall_now(void)
{ time_t t=time(NULL); return t>=1577836800 && (uint64_t)t<UINT32_MAX-90000 ? (uint32_t)t : 0; }
static uint32_t midnight_after(uint32_t now)
{
    uint32_t wall=wall_now(); time_t t=(time_t)wall; struct tm local;
    if(!wall) return 0;
    if(!localtime_r(&t,&local)) return 0;
    local.tm_hour=0;local.tm_min=0;local.tm_sec=0;local.tm_mday++;local.tm_isdst=-1;
    t=mktime(&local);
    return t>(time_t)wall && (uint64_t)(t-wall)<=UINT32_MAX-now ? now+(uint32_t)(t-wall) : 0;
}
static int ensure_initialized(uint32_t now)
{
    FILE *f; size_t i; char magic[16]; struct mc_state loaded={0}; int bad=0,result;
    uint32_t saved_wall=0,wall=wall_now(); int legacy=0;
    if(!now) return MC_CLOCK_CHANGED;
    if(initialized) return MC_OK;
    f=fopen(STATE_FILE,"r");
    if(!f) {
        if(errno!=ENOENT) return MC_STORAGE;
        mc_init(&state,now);
        if(save_state(&state,NULL)) return MC_STORAGE;
        logical_base=now;monotonic_base=monotonic_seconds();clock_ready=1;
        initialized=1;return MC_OK;
    }
    if(!fgets(magic,sizeof(magic),f)) bad=1;
    else if(!strcmp(magic,"MCAPP1\n")) legacy=1;
    else if(strcmp(magic,"MCAPP2\n") || read_number(f,&saved_wall)) bad=1;
#define READ_STATE(field) if(read_number(f,&loaded.field)) bad=1;
    STATE_FIELDS(READ_STATE)
#undef READ_STATE
    for(i=0;i<MC_TASK_CAPACITY;++i) {
#define READ_TASK(field) if(read_number(f,&loaded.tasks[i].field)) bad=1;
        TASK_FIELDS(READ_TASK)
#undef READ_TASK
    }
    if(fgetc(f)!=EOF || ferror(f)) bad=1;
    fclose(f);
    /* Preserve an invalid record for diagnosis; never silently reset choices. */
    if(bad || mc_validate(&loaded)!=MC_OK) return MC_STORAGE;
    if(legacy && loaded.last_now>=1577836800) saved_wall=loaded.last_now;
    /* Without trustworthy elapsed calendar time, resume remaining delays.
     * Never count powered-off time as focus or invent a date for offline use. */
    now=loaded.last_now ? loaded.last_now : 1;
    if(wall && saved_wall && wall>=saved_wall && wall-saved_wall<=UINT32_MAX-now)
        now+=wall-saved_wall;
    disk_state=loaded;
    result=mc_restore(&loaded,now,save_state,NULL);
    if(result!=MC_OK) return result;
    state=loaded;logical_base=now;monotonic_base=monotonic_seconds();clock_ready=1;
    initialized=1;last_poll=0;return MC_OK;
}
int mooncat_app_command(const struct mc_command *command,struct mc_state *snapshot)
{
    uint32_t now; int result;
#ifndef MOONCAT_APP_NO_AUTOSTART
    mooncat_app_worker_start();
#endif
    pthread_mutex_lock(&lock);result=ensure_initialized(app_now());now=app_now();
    if(result==MC_OK && command && command->op==MC_PAUSE_TODAY && !wall_now()) result=MC_CLOCK_CHANGED;
    if(result==MC_OK) result=mc_apply(&state,command,now,midnight_after(now),save_state,NULL);
    if(snapshot) *snapshot=state;
    publish_state(result==MC_STORAGE ? result : MC_OK);
    pthread_mutex_unlock(&lock);return result;
}
int mooncat_app_poll(struct mc_state *snapshot)
{
    uint32_t now;int result;
    pthread_mutex_lock(&lock);result=ensure_initialized(app_now());now=app_now();
    if(result==MC_OK && now!=last_poll) {
        result=mc_tick(&state,now,save_tick,NULL);
        if(result==MC_OK || result==MC_CLOCK_CHANGED) last_poll=now;
    }
    if(snapshot) *snapshot=state;
    publish_state(result);
    pthread_mutex_unlock(&lock);return result;
}
int mooncat_app_remind(uint32_t delay,uint32_t duration,struct mc_state *snapshot)
{
    struct mc_command c={0};uint32_t now;size_t i;int result;
#ifndef MOONCAT_APP_NO_AUTOSTART
    mooncat_app_worker_start();
#endif
    pthread_mutex_lock(&lock);result=ensure_initialized(app_now());now=app_now();
    if(result==MC_OK) {
        c.op=MC_SCHEDULE;c.delay=delay;c.duration=duration;c.request_id=state.next_id;
        for(i=0;i<MC_TASK_CAPACITY;++i) if(state.tasks[i].request_id==c.request_id) {
            if(c.request_id==UINT32_MAX) {result=MC_FULL;break;}
            ++c.request_id;i=(size_t)-1;
        }
        for(i=0;i<MC_TASK_CAPACITY;++i) {
            const struct mc_task *t=&state.tasks[i];
            if(t->phase>=MC_SCHEDULED && t->phase<=MC_RECOVERING && t->requested_delay==delay && t->duration==duration) {
                result=MC_DUPLICATE;break;
            }
        }
        if(result==MC_OK) result=mc_apply(&state,&c,now,midnight_after(now),save_state,NULL);
    }
    if(snapshot) *snapshot=state;
    publish_state(result==MC_STORAGE ? result : MC_OK);
    pthread_mutex_unlock(&lock);return result;
}
int mooncat_app_snapshot(struct mc_state *snapshot)
{
    int result;pthread_mutex_lock(&lock);result=ensure_initialized(app_now());
    if(snapshot) *snapshot=state;
    publish_state(result);
    pthread_mutex_unlock(&lock);return result;
}

/* State and I/O lock is never taken by the UI-facing functions below. */
#define REQUEST_CAPACITY 8
static pthread_mutex_t cache_lock=PTHREAD_MUTEX_INITIALIZER;
static struct mc_state cached_state;
static int cached_result=MC_BUSY;
static pthread_mutex_t request_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t request_wake;
static int request_wake_ready;
static pthread_t worker;
static int worker_started,worker_stopping;
static uint32_t next_ticket=1;
static struct app_request {
    struct mc_command command;
    uint32_t ticket;
    int remind,phase,result;
} requests[REQUEST_CAPACITY];

static void publish_state(int result)
{
    pthread_mutex_lock(&cache_lock);
    cached_state=state;cached_result=result;
    pthread_mutex_unlock(&cache_lock);
}
int mooncat_app_cached(struct mc_state *snapshot)
{
    int result;
    if(pthread_mutex_trylock(&cache_lock)) return MC_BUSY;
    if(snapshot) *snapshot=cached_state;
    result=cached_result;pthread_mutex_unlock(&cache_lock);return result;
}
static void *app_worker(void *unused)
{
    (void)unused;
    for(;;) {
        int selected=-1,result;struct app_request request;struct timespec deadline;
        mooncat_app_poll(NULL);
        pthread_mutex_lock(&request_lock);
        if(worker_stopping) {pthread_mutex_unlock(&request_lock);break;}
        for(int i=0;i<REQUEST_CAPACITY;i++)
            if(requests[i].phase==1 && (selected<0 || requests[i].ticket<requests[selected].ticket)) selected=i;
        if(selected<0) {
            clock_gettime(CLOCK_MONOTONIC,&deadline);deadline.tv_nsec+=200000000;
            if(deadline.tv_nsec>=1000000000) {deadline.tv_sec++;deadline.tv_nsec-=1000000000;}
            pthread_cond_timedwait(&request_wake,&request_lock,&deadline);
            pthread_mutex_unlock(&request_lock);continue;
        }
        request=requests[selected];requests[selected].phase=2;
        pthread_mutex_unlock(&request_lock);
        result=request.remind ? mooncat_app_remind(request.command.delay,request.command.duration,NULL)
                              : mooncat_app_command(&request.command,NULL);
        pthread_mutex_lock(&request_lock);
        requests[selected].result=result;requests[selected].phase=3;
        pthread_mutex_unlock(&request_lock);
    }
    return NULL;
}
int mooncat_app_worker_start(void)
{
    int result=0;pthread_attr_t attr;pthread_condattr_t condattr;
    pthread_mutex_lock(&request_lock);
    if(!request_wake_ready) {
        result=pthread_condattr_init(&condattr);
        if(!result) {
            result=pthread_condattr_setclock(&condattr,CLOCK_MONOTONIC);
            if(!result) result=pthread_cond_init(&request_wake,&condattr);
            pthread_condattr_destroy(&condattr);
        }
        if(result) {pthread_mutex_unlock(&request_lock);return MC_BUSY;}
        request_wake_ready=1;
    }
    if(!worker_started) {
        worker_stopping=0;
        result=pthread_attr_init(&attr);
        if(!result) {
            result=pthread_attr_setstacksize(&attr,16384);
            if(!result) result=pthread_create(&worker,&attr,app_worker,NULL);
            pthread_attr_destroy(&attr);
        }
        if(!result) worker_started=1;
    }
    pthread_mutex_unlock(&request_lock);return result ? MC_BUSY : MC_OK;
}
void mooncat_app_worker_stop(void)
{
    pthread_mutex_lock(&request_lock);
    if(!worker_started) {pthread_mutex_unlock(&request_lock);return;}
    worker_stopping=1;pthread_cond_signal(&request_wake);
    pthread_mutex_unlock(&request_lock);pthread_join(worker,NULL);
    pthread_mutex_lock(&request_lock);worker_started=0;
    memset(requests,0,sizeof(requests));pthread_mutex_unlock(&request_lock);
}
int mooncat_app_submit(const struct mc_command *command,int remind,uint32_t *ticket)
{
    int result=MC_FULL;
    if(!command || !ticket) return MC_INVALID;
    if(pthread_mutex_trylock(&request_lock)) return MC_BUSY;
    if(!worker_started || worker_stopping) result=MC_BUSY;
    else if(next_ticket!=UINT32_MAX) for(int i=0;i<REQUEST_CAPACITY;i++) if(!requests[i].phase) {
        requests[i].command=*command;requests[i].remind=remind;
        requests[i].ticket=next_ticket++;requests[i].phase=1;
        *ticket=requests[i].ticket;result=MC_OK;pthread_cond_signal(&request_wake);break;
    }
    pthread_mutex_unlock(&request_lock);return result;
}
int mooncat_app_result(uint32_t ticket,int *result)
{
    int status=MC_NOT_FOUND;
    if(!ticket || !result) return MC_INVALID;
    if(pthread_mutex_trylock(&request_lock)) return MC_BUSY;
    for(int i=0;i<REQUEST_CAPACITY;i++) if(requests[i].ticket==ticket && requests[i].phase) {
        status=MC_BUSY;
        if(requests[i].phase==3) {*result=requests[i].result;requests[i].phase=0;status=MC_OK;}
        break;
    }
    pthread_mutex_unlock(&request_lock);return status;
}
