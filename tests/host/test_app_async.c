#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdatomic.h>
#include <time.h>
#include <unistd.h>
static _Atomic uint64_t test_mono=100;
static _Atomic time_t test_wall;
static pthread_mutex_t io_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t io_wake=PTHREAD_COND_INITIALIZER;
static int io_entered,io_release;
static _Atomic int io_fail;
uint64_t mooncat_test_monotonic(void){return test_mono;}
static time_t test_time(time_t *out){if(out)*out=test_wall;return test_wall;}
static int test_fsync(int fd){
 pthread_mutex_lock(&io_lock);io_entered=1;pthread_cond_signal(&io_wake);
 while(!io_release){pthread_cond_wait(&io_wake,&io_lock);}
 pthread_mutex_unlock(&io_lock);return io_fail ? -1 : fsync(fd);
}
#define MOONCAT_APP_TEST_CLOCK 1
#define MOONCAT_APP_NO_AUTOSTART 1
#define time test_time
#define fsync test_fsync
#include "mooncat_app_service.c"
#undef time
#undef fsync
static void pause_worker(void){struct timespec delay={0,10000000};nanosleep(&delay,NULL);}
static int collect(uint32_t ticket){
 int result;
 for(int i=0;i<300;i++){if(mooncat_app_result(ticket,&result)==MC_OK)return result;pause_worker();}
 assert(!"request timed out");return -1;
}
static struct mc_state wait_state(uint32_t phase,uint32_t now){
 struct mc_state s;
 for(int i=0;i<300;i++){
  if(mooncat_app_cached(&s)==MC_OK && s.tasks[0].phase==phase && s.last_now>=now)return s;
  pause_worker();
 }
 assert(!"independent tick timed out");return s;
}
int main(void){
 struct mc_state s;struct mc_command c={0};uint32_t tickets[8],ticket;
 assert(mooncat_app_worker_start()==MC_OK);
 pthread_mutex_lock(&io_lock);while(!io_entered){pthread_cond_wait(&io_wake,&io_lock);}pthread_mutex_unlock(&io_lock);
 /* Initialization fsync remains blocked until all UI-facing calls return. */
 assert(mooncat_app_cached(&s)==MC_BUSY);
 for(int i=0;i<8;i++){c.op=i&1?MC_DND_OFF:MC_DND_ON;assert(mooncat_app_submit(&c,0,&tickets[i])==MC_OK);}
 assert(mooncat_app_submit(&c,0,&ticket)==MC_FULL);
 pthread_mutex_lock(&io_lock);io_release=1;pthread_cond_signal(&io_wake);pthread_mutex_unlock(&io_lock);
 for(int i=0;i<8;i++)assert(collect(tickets[i])==MC_OK);
 c.op=MC_SCHEDULE;c.delay=5;c.duration=5;
 assert(mooncat_app_submit(&c,1,&ticket)==MC_OK && collect(ticket)==MC_OK);
 test_mono=106;s=wait_state(MC_QUEUED,7);
 assert(s.tasks[0].due==6); /* Date is still 1970; no UI poll drives this. */
 test_wall=1800000000;
 assert(mooncat_app_poll(&s)==MC_OK && s.last_now==7);
 c.op=MC_ACK_SHOWN;c.id=s.tasks[0].id;c.revision=s.tasks[0].revision;
 assert(mooncat_app_command(&c,&s)==MC_OK);
 c.op=MC_START;c.revision=s.tasks[0].revision;
 assert(mooncat_app_command(&c,&s)==MC_OK && s.tasks[0].recovery_end==12);
 test_wall=0;test_mono=110;c.op=MC_COMPLETE;c.revision=s.tasks[0].revision;
 assert(mooncat_app_command(&c,&s)==MC_TOO_EARLY);
 test_mono=111;assert(mooncat_app_command(&c,&s)==MC_OK);
 io_fail=1;c.op=MC_FOCUS_ON;
 assert(mooncat_app_submit(&c,0,&ticket)==MC_OK && collect(ticket)==MC_STORAGE);
 assert(mooncat_app_snapshot(&s)==MC_OK && !s.focus);
 io_fail=0;
 assert(mooncat_app_submit(&c,0,&ticket)==MC_OK && collect(ticket)==MC_OK);
 /* Persist a future task then reboot without a trusted clock. */
 assert(mooncat_app_remind(20,5,&s)==MC_OK);
 mooncat_app_worker_stop();initialized=clock_ready=last_poll=0;test_mono=500;
 assert(mooncat_app_worker_start()==MC_OK);
 for(int i=0;i<30;i++)pause_worker();
 assert(mooncat_app_snapshot(&s)==MC_OK && !s.focus && s.tasks[1].due==32 && s.last_now==12);
 test_mono=519;
 for(int i=0;i<30;i++)pause_worker();
 assert(mooncat_app_snapshot(&s)==MC_OK && s.tasks[1].phase==MC_SCHEDULED);
 test_mono=520;
 for(int i=0;i<30;i++)pause_worker();
 assert(mooncat_app_snapshot(&s)==MC_OK && s.tasks[1].phase==MC_QUEUED);
 mooncat_app_worker_stop();return 0;
}
