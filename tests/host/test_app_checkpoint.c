/* Exercise the real service with a clock and fsync fault injection. */
#include <assert.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <stdint.h>

static time_t test_now = 1800000000;
static time_t wall_offset;
static unsigned sync_calls;
static int fail_sync;
static time_t test_time(time_t *out)
{ if(out) *out=test_now+wall_offset; return test_now+wall_offset; }
uint64_t mooncat_test_monotonic(void) {return test_now;}
#define MOONCAT_APP_TEST_CLOCK 1
#define MOONCAT_APP_NO_AUTOSTART 1
static int test_fsync(int fd)
{ ++sync_calls; return fail_sync ? -1 : fsync(fd); }
#define time test_time
#define fsync test_fsync
#include "mooncat_app_service.c"
#undef time
#undef fsync

int main(void)
{
    struct mc_state actual, before;
    struct mc_command command={0};
    unsigned count;
    assert(mooncat_app_poll(&actual)==MC_OK);
    assert(sync_calls==1);
    for(unsigned i=0;i<86400;i++) {
        ++test_now;
        assert(mooncat_app_poll(&actual)==MC_OK);
    }
    assert(sync_calls==1 && actual.last_now==(uint32_t)test_now);
    puts("24h idle: 0 additional fsync calls");

    command.op=MC_FOCUS_ON;
    assert(mooncat_app_command(&command,&actual)==MC_OK);
    count=sync_calls;
    for(unsigned i=0;i<59;i++) {
        ++test_now;
        assert(mooncat_app_poll(&actual)==MC_OK);
    }
    assert(sync_calls==count && actual.focus_seconds==59);
    ++test_now;
    assert(mooncat_app_poll(&actual)==MC_OK);
    assert(sync_calls==count+1 && disk_state.focus_seconds==60);

    /* Failed checkpoints must not commit; retry the same second succeeds. */
    for(unsigned i=0;i<59;i++) {
        ++test_now;
        assert(mooncat_app_poll(&actual)==MC_OK);
    }
    before=actual;
    ++test_now;fail_sync=1;
    assert(mooncat_app_poll(&actual)==MC_STORAGE);
    assert(!memcmp(&actual,&before,sizeof(actual)));
    assert(disk_state.focus_seconds==60);
    fail_sync=0;
    assert(mooncat_app_poll(&actual)==MC_OK);
    assert(actual.focus_seconds==120 && disk_state.focus_seconds==120);

    /* Turning focus off and due transitions are immediately persisted. */
    ++test_now;command.op=MC_FOCUS_OFF;count=sync_calls;
    assert(mooncat_app_command(&command,&actual)==MC_OK);
    assert(sync_calls==count+1 && disk_state.focus_seconds==121 && !disk_state.focus);
    assert(mooncat_app_remind(20,60,&actual)==MC_OK);
    count=sync_calls;
    for(unsigned i=0;i<19;i++) {
        ++test_now;
        assert(mooncat_app_poll(&actual)==MC_OK);
    }
    assert(sync_calls==count);
    ++test_now;fail_sync=1;before=actual;
    assert(mooncat_app_poll(&actual)==MC_STORAGE);
    assert(!memcmp(&actual,&before,sizeof(actual)));
    assert(disk_state.tasks[0].phase==MC_SCHEDULED);
    fail_sync=0;
    assert(mooncat_app_poll(&actual)==MC_OK);
    assert(disk_state.tasks[0].phase==MC_QUEUED);

    /* Calendar rollback never rewinds monotonic reminders or forces idle I/O. */
    test_now+=30;assert(mooncat_app_poll(&actual)==MC_OK);
    count=sync_calls;wall_offset-=86400;++test_now;
    assert(mooncat_app_poll(&actual)==MC_OK);
    assert(sync_calls==count && actual.last_now==(uint32_t)test_now);
    assert(actual.focus_seconds==121);
    /* Restore must preserve tasks/preferences without counting powered-off time. */
    command.op=MC_DND_ON;
    assert(mooncat_app_command(&command,&actual)==MC_OK);
    initialized=0;clock_ready=0;test_now+=10;
    assert(mooncat_app_snapshot(&actual)==MC_OK);
    assert(actual.dnd && !actual.focus && actual.focus_seconds==121);
    assert(actual.tasks[0].phase==MC_QUEUED);
    puts("focus checkpoints, immediate transitions, retry, rollback and restart passed");
    return 0;
}
