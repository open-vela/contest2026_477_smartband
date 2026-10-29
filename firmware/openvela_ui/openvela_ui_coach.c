/* SPDX-License-Identifier: Apache-2.0 */
#include <nuttx/config.h>
#include "openvela_ui_coach.h"
#ifdef CONFIG_EXAMPLES_AI_AGENT_VELA
#include <lvgl/lvgl.h>
#include "tools/mooncat_app_service.h"
#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <syslog.h>

/* All functions run on the Native UI thread. State is pulled from the bounded
 * durable task table; no lossy message slot carries actions or acknowledgments. */
static lv_obj_t *launcher,*panel,*summary,*card,*body;
static lv_timer_t *timer;
static struct mc_state snapshot;
static uint32_t card_id,card_revision,card_phase;
static int last_error;
static int workout_pending=-1;
#define UI_REMIND_45_MIN 1001
#define UI_TOGGLE_DND 1002

static void close_card(void)
{
    if(card && lv_obj_is_valid(card)) lv_obj_delete(card);
    card=NULL;body=NULL;card_id=card_revision=card_phase=0;
}
static void action(lv_event_t *event)
{
    struct mc_command c={0}; int result;
    c.op=(uint32_t)(uintptr_t)lv_event_get_user_data(event);
    c.id=card_id;c.revision=card_revision;c.delay=300;
    if(c.op==UI_TOGGLE_DND) c.op=snapshot.dnd?MC_DND_OFF:MC_DND_ON;
    if(c.op==UI_REMIND_45_MIN) {c.op=MC_SCHEDULE;c.delay=2700;c.duration=60;}
    if(c.op==MC_SCHEDULE) {
        if(!c.duration) {c.delay=20;c.duration=20;}
        result=mooncat_app_remind(c.delay,c.duration,&snapshot);
    } else result=mooncat_app_command(&c,&snapshot);
    last_error=result;
    if(result==MC_OK || result==MC_DUPLICATE) {
        if(c.op>=MC_START) close_card();
    } else if(body) lv_label_set_text(body,mc_status_name(result));
}
static lv_obj_t *button(lv_obj_t *parent,const char *text,int x,int y,int width,uint32_t op)
{
    lv_obj_t *b=lv_button_create(parent),*label;
    lv_obj_set_size(b,width,30);lv_obj_set_pos(b,x,y);
    label=lv_label_create(b);lv_label_set_text(label,text);lv_obj_center(label);
    lv_obj_add_event_cb(b,action,LV_EVENT_CLICKED,(void *)(uintptr_t)op);return b;
}
static void toggle_panel(lv_event_t *event)
{
    (void)event;
    if(panel) {lv_obj_delete(panel);panel=NULL;summary=NULL;return;}
    panel=lv_obj_create(lv_layer_top());lv_obj_set_size(panel,308,210);
    lv_obj_center(panel);lv_obj_set_style_pad_all(panel,4,0);
    lv_obj_clear_flag(panel,LV_OBJ_FLAG_SCROLLABLE);
    summary=lv_label_create(panel);lv_obj_set_width(summary,298);lv_obj_set_pos(summary,0,0);
    button(panel,"Focus",0,92,94,MC_FOCUS_ON);
    button(panel,"Stop focus",100,92,94,MC_FOCUS_OFF);
    button(panel,"Rest in 45m",200,92,94,UI_REMIND_45_MIN);
    button(panel,"DND toggle",0,128,94,UI_TOGGLE_DND);
    button(panel,"Resume",100,128,94,MC_RESUME_TODAY);
    button(panel,"Demo 20s",200,128,94,MC_SCHEDULE);
    lv_obj_t *close=lv_button_create(panel),*label=lv_label_create(close);
    lv_obj_set_size(close,294,28);lv_obj_set_pos(close,0,164);
    lv_label_set_text(label,"Close / return to watch");lv_obj_center(label);
    lv_obj_add_event_cb(close,toggle_panel,LV_EVENT_CLICKED,NULL);
}
static void update(lv_timer_t *unused)
{
    const struct mc_task *task; char text[280]; time_t now=time(NULL); int result; size_t i; uint32_t next=0;
    (void)unused;
    if(workout_pending>=0) {
        struct mc_command c={0};c.op=workout_pending ? MC_WORKOUT_ON : MC_WORKOUT_OFF;
        result=mooncat_app_command(&c,NULL);
        if(result==MC_OK) workout_pending=-1; else last_error=result;
    }
    result=mooncat_app_poll(&snapshot);
    if(result!=MC_OK) {last_error=result;close_card();}
    for(i=0;i<MC_TASK_CAPACITY;++i) if(snapshot.tasks[i].phase==MC_SCHEDULED && (!next || snapshot.tasks[i].due<next)) next=snapshot.tasks[i].due;
    if(summary) {
        snprintf(text,sizeof(text),"APP EVENTS | focus %s | DND %s\nWorkout %s | paused %s\nFocus %lus | shown %lu | confirmed %lu\nNext in %lus | %s",
            snapshot.focus?"on":"off",snapshot.dnd?"on":"off",snapshot.workout?"on":"off",
            snapshot.pause_until>(uint32_t)now?"today":"no",(unsigned long)snapshot.focus_seconds,
            (unsigned long)snapshot.shown_count,(unsigned long)snapshot.completed_count,
            (unsigned long)(next>(uint32_t)now?next-(uint32_t)now:0),mc_status_name(last_error));
        lv_label_set_text(summary,text);
    }
    if(result!=MC_OK || workout_pending>=0) return;
    task=mc_next_card(&snapshot);
    if(!task) {close_card();return;}
    if(card_id!=task->id || card_revision!=task->revision || !card || !lv_obj_is_valid(card)) {
        struct mc_command ack={0};
        close_card();
        card=lv_obj_create(lv_layer_top());lv_obj_set_size(card,308,174);lv_obj_center(card);
        lv_obj_set_style_pad_all(card,6,0);lv_obj_clear_flag(card,LV_OBJ_FLAG_SCROLLABLE);
        body=lv_label_create(card);lv_obj_set_width(body,294);lv_obj_set_pos(body,0,0);
        card_id=task->id;card_revision=task->revision;card_phase=task->phase;
        if(task->phase==MC_RECOVERING) {
            button(card,"Confirm done",0,100,144,MC_COMPLETE);
            button(card,"Cancel",150,100,144,MC_CANCEL);
        } else {
            button(card,"Start rest",0,96,144,MC_START);
            button(card,"Later 5m",150,96,144,MC_SNOOZE);
            button(card,"Pause today",0,132,294,MC_PAUSE_TODAY);
        }
        /* Acknowledges widget construction on the UI thread, not LCD scanout. */
        if(task->phase==MC_QUEUED) {
            ack.op=MC_ACK_SHOWN;ack.id=card_id;ack.revision=card_revision;
            result=mooncat_app_command(&ack,&snapshot);
            if(result!=MC_OK) {last_error=result;close_card();return;}
            task=mc_find(&snapshot,card_id);card_revision=task->revision;card_phase=task->phase;
        }
    }
    if(card_phase==MC_RECOVERING) {
        snprintf(text,sizeof(text),"RECOVERY | APP TIMER\nBreathe gently or stretch.\n%lus remaining\nConfirm when you have finished.",
            (unsigned long)(task->recovery_end>(uint32_t)now?task->recovery_end-(uint32_t)now:0));
    } else snprintf(text,sizeof(text),"MOONCAT | APP REMINDER\nTake a %lus recovery break?\nBased on your request.\nNo physical activity detection.",(unsigned long)task->duration);
    lv_label_set_text(body,text);
}
int openvela_ui_coach_start(void)
{
    if(timer) return 0;
    launcher=lv_button_create(lv_layer_top());lv_obj_set_size(launcher,70,26);
    lv_obj_align(launcher,LV_ALIGN_BOTTOM_RIGHT,-4,-2);
    lv_obj_t *label=lv_label_create(launcher);lv_label_set_text(label,"COACH");lv_obj_center(label);
    lv_obj_add_event_cb(launcher,toggle_panel,LV_EVENT_CLICKED,NULL);
    timer=lv_timer_create(update,1000,NULL);
    if(!timer) {lv_obj_delete(launcher);launcher=NULL;return -1;}
    return 0;
}
void openvela_ui_coach_stop(void)
{
    if(timer) lv_timer_delete(timer);
    timer=NULL;close_card();
    if(panel) lv_obj_delete(panel);
    if(launcher) lv_obj_delete(launcher);
    panel=launcher=summary=NULL;
}
void openvela_ui_coach_workout(int active)
{ workout_pending=active?1:0; }
#else
int openvela_ui_coach_start(void) {return 0;}
void openvela_ui_coach_stop(void) {}
void openvela_ui_coach_workout(int active) {(void)active;}
#endif
