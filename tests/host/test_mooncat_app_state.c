/* SPDX-License-Identifier: Apache-2.0 */
#include "mooncat_app_state.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static struct mc_state s, saved;
static int fail_save;
static int save(const struct mc_state *next,void *cookie)
{(void)cookie;if(fail_save)return -1;saved=*next;return 0;}
static int command(uint32_t op,uint32_t id,uint32_t rev,uint32_t delay,uint32_t now)
{
    struct mc_command c={0};c.op=op;c.id=id;c.revision=rev;c.delay=delay;c.duration=20;c.request_id=1;
    return mc_apply(&s,&c,now,86400,save,NULL);
}
static void init(void) {mc_init(&s,100);fail_save=0;}
static void schedule(void) {assert(command(MC_SCHEDULE,0,0,20,100)==MC_OK);}
static void show(void) {
    assert(mc_tick(&s,120,save,NULL)==MC_OK);assert(s.tasks[0].phase==MC_QUEUED);
    assert(command(MC_ACK_SHOWN,1,2,0,120)==MC_OK);assert(s.shown_count==1);
}
static void run(int scenario)
{
    struct mc_command c={0};struct mc_state before;unsigned i;
    init();
    switch(scenario) {
    case 1: /* Latest context, not the schedule-time snapshot. */
        schedule();assert(command(MC_DND_ON,0,0,0,110)==MC_OK);
        assert(mc_tick(&s,120,save,NULL)==MC_OK);
        assert(s.tasks[0].phase==MC_SCHEDULED && s.tasks[0].reason==MC_REASON_DND && s.tasks[0].due==420);
        assert(command(MC_DND_OFF,0,0,0,200)==MC_OK);
        assert(command(MC_WORKOUT_ON,0,0,0,410)==MC_OK);
        assert(mc_tick(&s,420,save,NULL)==MC_OK && s.tasks[0].reason==MC_REASON_WORKOUT);
        assert(command(MC_WORKOUT_OFF,0,0,0,430)==MC_OK);
        assert(mc_tick(&s,720,save,NULL)==MC_OK && s.tasks[0].phase==MC_QUEUED);break;
    case 2: /* Dedup and stale cards never count completion twice. */
        schedule();assert(command(MC_SCHEDULE,0,0,20,101)==MC_DUPLICATE);show();
        assert(command(MC_START,1,2,0,121)==MC_STALE);
        assert(command(MC_START,1,3,0,121)==MC_OK);
        assert(command(MC_START,1,3,0,122)==MC_DUPLICATE);
        assert(command(MC_COMPLETE,1,4,0,140)==MC_TOO_EARLY);
        assert(command(MC_COMPLETE,1,4,0,141)==MC_OK);
        assert(command(MC_COMPLETE,1,4,0,142)==MC_DUPLICATE);
        assert(s.completed_count==1 && s.tasks[0].phase==MC_DONE);break;
    case 3:
        schedule();show();assert(command(MC_SNOOZE,1,3,300,121)==MC_OK);
        assert(s.tasks[0].due==421 && s.tasks[0].revision==4);
        assert(command(MC_SNOOZE,1,3,300,122)==MC_DUPLICATE);
        assert(mc_tick(&s,421,save,NULL)==MC_OK);
        assert(command(MC_PAUSE_TODAY,1,5,0,421)==MC_OK);
        c.op=MC_SCHEDULE;c.request_id=2;c.delay=10;c.duration=20;
        assert(mc_apply(&s,&c,422,86400,save,NULL)==MC_OK);
        assert(mc_tick(&s,432,save,NULL)==MC_OK && s.tasks[1].reason==MC_REASON_PAUSED);
        assert(command(MC_RESUME_TODAY,0,0,0,433)==MC_OK && !s.pause_until);
        assert(mc_tick(&s,732,save,NULL)==MC_OK && s.tasks[1].phase==MC_QUEUED);break;
    case 4:
        schedule();show();before=s;fail_save=1;
        assert(command(MC_START,1,3,0,120)==MC_STORAGE && !memcmp(&before,&s,sizeof(s)));
        fail_save=0;assert(command(MC_START,1,3,0,120)==MC_OK);s=saved;
        assert(mc_restore(&s,200,save,NULL)==MC_OK);
        assert(!s.focus && !s.workout && s.tasks[0].phase==MC_SCHEDULED && s.tasks[0].reason==MC_REASON_RESTART);
        assert(s.completed_count==0 && command(MC_COMPLETE,1,4,0,200)==MC_STALE);break;
    case 5:
        schedule();assert(mc_tick(&s,4000,save,NULL)==MC_OK && s.tasks[0].phase==MC_EXPIRED);
        assert(mc_tick(&s,3000,save,NULL)==MC_CLOCK_CHANGED);
        assert(s.last_now==3000 && s.completed_count==0);s.version=99;
        assert(mc_restore(&s,3001,save,NULL)==MC_INVALID);break;
    case 6:
        c.op=MC_SCHEDULE;c.delay=20;c.duration=20;
        for(i=1;i<=MC_TASK_CAPACITY;++i){c.request_id=i;assert(mc_apply(&s,&c,100,86400,save,NULL)==MC_OK);}
        c.request_id=99;before=s;assert(mc_apply(&s,&c,100,86400,save,NULL)==MC_FULL);
        assert(!memcmp(&before,&s,sizeof(s)));
        assert(mc_tick(&s,120,save,NULL)==MC_OK && mc_next_card(&s)->id==1);
        assert(command(MC_CANCEL,1,2,0,120)==MC_OK && mc_next_card(&s)->id==2);
        assert(mc_apply(&s,&c,120,86400,save,NULL)==MC_OK && s.tasks[0].id==9);break;
    case 7:
        assert(command(MC_FOCUS_ON,0,0,0,100)==MC_OK);schedule();show();
        assert(s.focus_seconds==20);assert(command(MC_START,1,3,0,120)==MC_OK);
        assert(mc_tick(&s,130,save,NULL)==MC_OK && s.focus_seconds==20);
        assert(command(MC_COMPLETE,1,4,0,140)==MC_OK && s.focus_seconds==20);
        assert(mc_tick(&s,150,save,NULL)==MC_OK && s.focus_seconds==30);
        assert(command(MC_FOCUS_OFF,0,0,0,160)==MC_OK && s.focus_seconds==40);
        assert(mc_tick(&s,10000,save,NULL)==MC_OK && s.focus_seconds==40);break;
    case 8:
        schedule();show();assert(command(MC_DND_ON,0,0,0,121)==MC_OK);
        assert(command(MC_START,1,3,0,121)==MC_BUSY);
        assert(mc_tick(&s,121,save,NULL)==MC_OK && !mc_next_card(&s));
        assert(command(MC_SNOOZE,1,3,300,121)==MC_STALE);
        before=s;s.tasks[0].duration=0;assert(mc_validate(&s)==MC_INVALID);s=before;
        s.tasks[1]=s.tasks[0];assert(mc_validate(&s)==MC_INVALID);break;
    case 9:
        assert(command(MC_SCHEDULE,0,0,0,100)==MC_INVALID);
        assert(command(MC_SCHEDULE,0,0,86401,100)==MC_INVALID);
        assert(command(MC_SCHEDULE,0,0,20,UINT32_MAX-10)==MC_INVALID);
        schedule();show();before=s;fail_save=1;
        assert(command(MC_PAUSE_TODAY,1,3,0,120)==MC_STORAGE);
        assert(!memcmp(&before,&s,sizeof(s)));break;
    case 10:
        schedule();show();assert(command(MC_PAUSE_TODAY,1,3,0,120)==MC_OK);
        c.op=MC_SCHEDULE;c.request_id=2;c.delay=10;c.duration=20;
        assert(mc_apply(&s,&c,86395,172800,save,NULL)==MC_OK);
        assert(mc_tick(&s,86405,save,NULL)==MC_OK && s.tasks[1].phase==MC_QUEUED);break;
    case 11:
        schedule();show();assert(command(MC_START,1,3,0,120)==MC_OK);
        assert(mc_restore(&s,10000,save,NULL)==MC_OK);
        assert(s.tasks[0].phase==MC_EXPIRED && s.tasks[0].reason==MC_REASON_OVERDUE);
        assert(!s.completed_count && !mc_next_card(&s));break;
    default: assert(0);
    }
}
int main(int argc,char **argv)
{ assert(argc==2);run(atoi(argv[1]));puts("app state scenario passed");return 0; }
