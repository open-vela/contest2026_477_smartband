/* SPDX-License-Identifier: Apache-2.0 */
#include <nuttx/config.h>
#include "openvela_ui_coach.h"
#ifdef CONFIG_EXAMPLES_AI_AGENT_VELA
#include "openvela_ui_style.h"
#include "tools/mooncat_app_service.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>

/* Widgets live here; ticking and disk I/O belong to the service. */
static lv_obj_t *launcher,*panel,*summary,*card,*body;
static lv_timer_t *timer;
static struct mc_state snapshot;
static uint32_t card_id,card_revision,card_phase,pending_ticket,pending_op;
static int last_error,workout_pending=-1,pending_workout=-1;
#define UI_REMIND_45_MIN 1001
#define UI_TOGGLE_DND 1002
static const char *feedback(int error)
{
    switch(error) {
    case MC_OK: case MC_DUPLICATE:return "Your pace, your choice.";
    case MC_BUSY:return "One moment...";
    case MC_CLOCK_CHANGED:return "Set the date to pause for today.";
    case MC_STORAGE:return "Could not save. Please try again.";
    case MC_TOO_EARLY:return "A little longer, then confirm.";
    case MC_FULL:return "Reminders are full. Try again later.";
    default:return "This reminder changed. Try again.";
    }
}
static void set_text(lv_obj_t *label,const char *text)
{ if(label && strcmp(lv_label_get_text(label),text)) lv_label_set_text(label,text); }
static void close_card(void)
{
    if(card && lv_obj_is_valid(card)) lv_obj_delete(card);
    card=NULL;body=NULL;card_id=card_revision=card_phase=0;
}
static int submit(struct mc_command *c,int remind)
{
    int result;
    if(pending_ticket) return MC_BUSY;
    result=mooncat_app_submit(c,remind,&pending_ticket);
    if(result==MC_OK) {pending_op=c->op;last_error=MC_BUSY;}
    else last_error=result;
    return result;
}
static void action(lv_event_t *event)
{
    struct mc_command c={0};
    if(pending_ticket) return;
    c.op=(uint32_t)(uintptr_t)lv_event_get_user_data(event);
    c.id=card_id;c.revision=card_revision;c.delay=300;
    if(c.op==UI_TOGGLE_DND) c.op=snapshot.dnd?MC_DND_OFF:MC_DND_ON;
    if(c.op==UI_REMIND_45_MIN) {c.op=MC_SCHEDULE;c.delay=2700;c.duration=60;}
    if(c.op==MC_SCHEDULE && !c.duration) {c.delay=20;c.duration=20;}
    submit(&c,c.op==MC_SCHEDULE);
}
static lv_obj_t *button(lv_obj_t *parent,const char *text,int x,int y,int width,uint32_t op)
{
    lv_obj_t *b=lv_button_create(parent),*label;
    mooncat_button_style(b);lv_obj_set_size(b,width,28);lv_obj_set_pos(b,x,y);
    label=lv_label_create(b);lv_label_set_text(label,text);lv_obj_center(label);
    lv_obj_add_event_cb(b,action,LV_EVENT_CLICKED,(void *)(uintptr_t)op);return b;
}
static void toggle_panel(lv_event_t *event)
{
    (void)event;
    if(panel) {lv_obj_delete(panel);panel=NULL;summary=NULL;return;}
    panel=lv_obj_create(lv_layer_top());mooncat_surface(panel);
    lv_obj_set_size(panel,282,218);lv_obj_center(panel);
    summary=lv_label_create(panel);lv_obj_set_width(summary,260);lv_obj_set_pos(summary,0,0);
    button(panel,"Focus",0,76,82,MC_FOCUS_ON);
    button(panel,"Unwind",88,76,82,MC_FOCUS_OFF);
    button(panel,"Rest 45m",176,76,82,UI_REMIND_45_MIN);
    button(panel,"Quiet",0,112,82,UI_TOGGLE_DND);
    button(panel,"Resume",88,112,82,MC_RESUME_TODAY);
    button(panel,"Try 20s",176,112,82,MC_SCHEDULE);
    lv_obj_t *close=lv_button_create(panel),*label=lv_label_create(close);
    mooncat_button_style(close);lv_obj_set_size(close,258,28);lv_obj_set_pos(close,0,156);
    lv_label_set_text(label,"Back to the stars");lv_obj_center(label);
    lv_obj_add_event_cb(close,toggle_panel,LV_EVENT_CLICKED,NULL);
}
static void update(lv_timer_t *unused)
{
    const struct mc_task *task;char text[240],next_text[48];uint32_t now,next=0;int result;
    (void)unused;
    if(pending_ticket && mooncat_app_result(pending_ticket,&result)==MC_OK) {
        pending_ticket=0;last_error=result;
        if(result==MC_OK || result==MC_DUPLICATE) {
            if(pending_workout>=0 && workout_pending==pending_workout) workout_pending=-1;
            if(pending_op>=MC_START && pending_op!=MC_ACK_SHOWN) close_card();
        } else if(pending_op==MC_ACK_SHOWN) close_card();
        pending_workout=-1;
    }
    result=mooncat_app_cached(&snapshot);now=snapshot.last_now;
    if(result!=MC_OK && result!=MC_BUSY) {last_error=result;close_card();}
    if(workout_pending>=0 && !pending_ticket) {
        struct mc_command c={0};c.op=workout_pending?MC_WORKOUT_ON:MC_WORKOUT_OFF;
        if(submit(&c,0)==MC_OK) pending_workout=workout_pending;
    }
    for(size_t i=0;i<MC_TASK_CAPACITY;i++) if(snapshot.tasks[i].phase==MC_SCHEDULED &&
        (!next || snapshot.tasks[i].due<next)) next=snapshot.tasks[i].due;
    if(summary) {
        if(next) snprintf(next_text,sizeof(next_text),"Next rest: %lus",
                          (unsigned long)(next>now?next-now:0));
        else snprintf(next_text,sizeof(next_text),"No rest scheduled");
        snprintf(text,sizeof(text),"A moment for you\nFocus %s  |  Quiet %s\n%s\n%s",
                 snapshot.focus?"on":"off",snapshot.dnd?"on":"off",
                 next_text,feedback(last_error));
        set_text(summary,text);
    }
    if(result!=MC_OK || workout_pending>=0) return;
    task=mc_next_card(&snapshot);
    if(!task) {close_card();return;}
    if(pending_ticket) return;
    if(card_id!=task->id || card_phase!=task->phase || !card || !lv_obj_is_valid(card)) {
        close_card();card=lv_obj_create(lv_layer_top());mooncat_surface(card);
        lv_obj_set_size(card,270,190);lv_obj_center(card);
        body=lv_label_create(card);lv_obj_set_width(body,248);lv_obj_set_pos(body,0,0);
        card_id=task->id;card_phase=task->phase;
        if(task->phase==MC_RECOVERING) {
            button(card,"Feeling better",0,126,120,MC_COMPLETE);
            button(card,"Cancel",128,126,120,MC_CANCEL);
        } else {
            button(card,"Take a break",0,94,120,MC_START);
            button(card,"Later 5m",128,94,120,MC_SNOOZE);
            button(card,"Pause today",0,130,248,MC_PAUSE_TODAY);
        }
    }
    card_revision=task->revision;
    if(card_phase==MC_RECOVERING)
        snprintf(text,sizeof(text),"Breathe under the stars\nStretch gently, at your pace.\n%lus remaining\n%s",
            (unsigned long)(task->recovery_end>now?task->recovery_end-now:0),feedback(last_error));
    else snprintf(text,sizeof(text),"Time for a little pause\nYour %lus break is ready.\n%s",
                  (unsigned long)task->duration,feedback(last_error));
    set_text(body,text);
    if(task->phase==MC_QUEUED) {
        struct mc_command ack={0};ack.op=MC_ACK_SHOWN;ack.id=card_id;ack.revision=card_revision;
        submit(&ack,0); /* Widget acknowledgement, not optical scanout. */
    }
}
int openvela_ui_coach_start(void)
{
    if(timer) return 0;
    if(mooncat_app_worker_start()!=MC_OK) return -1;
    launcher=lv_button_create(lv_layer_top());mooncat_button_style(launcher);
    lv_obj_set_size(launcher,54,22);lv_obj_align(launcher,LV_ALIGN_BOTTOM_MID,70,-4);
    lv_obj_t *label=lv_label_create(launcher);lv_label_set_text(label,"Rest");lv_obj_center(label);
    lv_obj_add_event_cb(launcher,toggle_panel,LV_EVENT_CLICKED,NULL);
    timer=lv_timer_create(update,200,NULL);
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
    /* Service continues; collect any pending ticket on UI restart. */
}
void openvela_ui_coach_workout(int active) {workout_pending=active?1:0;}
#else
int openvela_ui_coach_start(void) {return 0;}
void openvela_ui_coach_stop(void) {}
void openvela_ui_coach_workout(int active) {(void)active;}
#endif
