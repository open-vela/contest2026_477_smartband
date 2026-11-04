/* SPDX-License-Identifier: Apache-2.0 */
#include "openvela_ui_storage.h"
#include <pthread.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>
#include <syslog.h>
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t wake;
static pthread_t thread;
static int started,stopping,condition_ready,last_error;
static void *pending;
static int (*writer)(void *);
static void *storage_worker(void *unused)
{
    (void)unused;
    pthread_mutex_lock(&lock);
    for(;;) {
        void *data;int result,retry;struct timespec deadline;
        while(!pending && !stopping) pthread_cond_wait(&wake,&lock);
        if(!pending && stopping) break;
        data=pending;pending=NULL;
        pthread_mutex_unlock(&lock);
        result=writer(data);
        pthread_mutex_lock(&lock);last_error=result;
        retry=result && !pending;
        if(!result || pending) free(data);
        else pending=data;
        if(retry && stopping) break;
        if(result && !stopping) {
            syslog(LOG_ERR,"openvela_ui: sport save failed; snapshot retained\n");
            clock_gettime(CLOCK_MONOTONIC,&deadline);deadline.tv_sec++;
            pthread_cond_timedwait(&wake,&lock,&deadline);
        }
    }
    pthread_mutex_unlock(&lock);return NULL;
}
int openvela_ui_storage_submit(void *data,int (*write_snapshot)(void *))
{
    int result=0;pthread_attr_t attr;pthread_condattr_t condattr;
    if(!data || !write_snapshot) return -EINVAL;
    pthread_mutex_lock(&lock);
    if(stopping || (writer && writer!=write_snapshot)) {pthread_mutex_unlock(&lock);return -EBUSY;}
    if(!condition_ready) {
        result=pthread_condattr_init(&condattr);
        if(!result) {
            result=pthread_condattr_setclock(&condattr,CLOCK_MONOTONIC);
            if(!result) result=pthread_cond_init(&wake,&condattr);
            pthread_condattr_destroy(&condattr);
        }
        if(!result) condition_ready=1;
    }
    if(!result && !started) {
        writer=write_snapshot;
        result=pthread_attr_init(&attr);
        if(!result) {
            result=pthread_attr_setstacksize(&attr,16384);
            if(!result) result=pthread_create(&thread,&attr,storage_worker,NULL);
            pthread_attr_destroy(&attr);
        }
        if(!result) started=1;
    }
    if(!result) {free(pending);pending=data;pthread_cond_signal(&wake);}
    pthread_mutex_unlock(&lock);return -result;
}
int openvela_ui_storage_stop(void)
{
    int result;
    pthread_mutex_lock(&lock);
    if(!started) {pthread_mutex_unlock(&lock);return 0;}
    stopping=1;pthread_cond_signal(&wake);pthread_mutex_unlock(&lock);
    pthread_join(thread,NULL);
    pthread_mutex_lock(&lock);started=stopping=0;result=last_error;
    pthread_mutex_unlock(&lock);return result;
}
