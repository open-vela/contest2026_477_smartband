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

static int save_state(const struct mc_state *s,void *cookie)
{
    size_t i; int failed=0; FILE *f;
    (void)cookie;
    if(mkdir(MOONCAT_APP_DIRECTORY,0700) && errno!=EEXIST) return -1;
    f=fopen(TEMP_FILE,"w"); if(!f) return -1;
    if(fputs("MCAPP1\n",f)<0) failed=1;
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
    /* Checkpoint counting every minute; task and preference changes immediately. */
    if(!memcmp(&significant,&disk_state,sizeof(*s)) && s->last_now>=disk_state.last_now && s->last_now-disk_state.last_now<60) return 0;
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
    time_t t=(time_t)now; struct tm local;
    if(!localtime_r(&t,&local)) return 0;
    local.tm_hour=0;local.tm_min=0;local.tm_sec=0;local.tm_mday++;local.tm_isdst=-1;
    t=mktime(&local);return t>(time_t)now && (uint64_t)t<=UINT32_MAX ? (uint32_t)t : 0;
}
static int ensure_initialized(uint32_t now)
{
    FILE *f; size_t i; char magic[16]; struct mc_state loaded={0}; int bad=0,result;
    if(!now) return MC_CLOCK_CHANGED;
    if(initialized) return MC_OK;
    f=fopen(STATE_FILE,"r");
    if(!f) {
        if(errno!=ENOENT) return MC_STORAGE;
        mc_init(&state,now);
        if(save_state(&state,NULL)) return MC_STORAGE;
        initialized=1;return MC_OK;
    }
    if(!fgets(magic,sizeof(magic),f) || strcmp(magic,"MCAPP1\n")) bad=1;
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
    disk_state=loaded;
    result=mc_restore(&loaded,now,save_state,NULL);
    if(result!=MC_OK) return result;
    state=loaded; initialized=1; return MC_OK;
}
int mooncat_app_command(const struct mc_command *command,struct mc_state *snapshot)
{
    uint32_t now; int result;
    pthread_mutex_lock(&lock);now=wall_now();result=ensure_initialized(now);
    if(result==MC_OK) result=mc_apply(&state,command,now,midnight_after(now),save_state,NULL);
    if(snapshot) *snapshot=state;
    pthread_mutex_unlock(&lock);return result;
}
int mooncat_app_poll(struct mc_state *snapshot)
{
    uint32_t now;int result;
    pthread_mutex_lock(&lock);now=wall_now();result=ensure_initialized(now);
    if(result==MC_OK && now!=last_poll) {
        result=mc_tick(&state,now,save_tick,NULL);
        if(result==MC_OK || result==MC_CLOCK_CHANGED) last_poll=now;
    }
    if(snapshot) *snapshot=state;
    pthread_mutex_unlock(&lock);return result;
}
int mooncat_app_remind(uint32_t delay,uint32_t duration,struct mc_state *snapshot)
{
    struct mc_command c={0};uint32_t now;size_t i;int result;
    pthread_mutex_lock(&lock);now=wall_now();result=ensure_initialized(now);
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
    pthread_mutex_unlock(&lock);return result;
}
int mooncat_app_snapshot(struct mc_state *snapshot)
{
    int result;pthread_mutex_lock(&lock);result=ensure_initialized(wall_now());
    if(snapshot) *snapshot=state;
    pthread_mutex_unlock(&lock);return result;
}
