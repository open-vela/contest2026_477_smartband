/* Single consumer, bounded producers. Keep the head until atomic rename succeeds.
 * Accepted RAM samples survive service stop/start, not power removal. */
static void sync_queue_health(const struct sync_health_event_s *event)
{
    size_t tail;
    pthread_mutex_lock(&g_sync.lock);
    if(!g_sync.started || g_sync.stopping || g_sync.queue_count==SYNC_HEALTH_QUEUE_SIZE) {
        g_sync.rejected++;
        pthread_mutex_unlock(&g_sync.lock);
        syslog(LOG_WARNING,"openvela_ui: health queue unavailable; rejected newest sample\n");
        return;
    }
    tail=(g_sync.queue_head+g_sync.queue_count)%SYNC_HEALTH_QUEUE_SIZE;
    g_sync.queue[tail]=*event;g_sync.queue_count++;
    pthread_cond_signal(&g_sync.condition);
    pthread_mutex_unlock(&g_sync.lock);
}
static bool sync_flush_health(time_t wall,uint64_t monotonic)
{
    for(unsigned i=0;i<SYNC_HEALTH_QUEUE_SIZE;i++) {
        struct sync_health_event_s event;char day[SYNC_DAY_SIZE];int result;
        pthread_mutex_lock(&g_sync.lock);
        if(!g_sync.queue_count) {pthread_mutex_unlock(&g_sync.lock);return true;}
        if(monotonic<g_sync.health_retry_at) {pthread_mutex_unlock(&g_sync.lock);return false;}
        event=g_sync.queue[g_sync.queue_head];
        if(!sync_day_for_time(event.measured_at,day)) {
            if(!sync_day_for_time(wall,day)) {pthread_mutex_unlock(&g_sync.lock);return false;}
            event.measured_at=wall;g_sync.queue[g_sync.queue_head]=event;
        }
        pthread_mutex_unlock(&g_sync.lock);
        result=sync_persist_health(&event);
        pthread_mutex_lock(&g_sync.lock);
        if(result<0) {
            g_sync.persist_failures++;
            g_sync.health_backoff=g_sync.health_backoff ? g_sync.health_backoff*2 : 1;
            if(g_sync.health_backoff>30) g_sync.health_backoff=30;
            g_sync.health_retry_at=monotonic+g_sync.health_backoff;
            pthread_mutex_unlock(&g_sync.lock);
            syslog(LOG_ERR,"openvela_ui: health persistence failed; head retained for retry\n");
            return false;
        }
        g_sync.queue_head=(g_sync.queue_head+1)%SYNC_HEALTH_QUEUE_SIZE;
        g_sync.queue_count--;g_sync.health_backoff=0;g_sync.health_retry_at=0;
        pthread_mutex_unlock(&g_sync.lock);
    }
    return true;
}
