/* SPDX-License-Identifier: Apache-2.0 */
#include "tools/tool_mooncat_coach.h"
#include "mooncat_app_service.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { APP_TOOL_REMIND = 14 }; /* Tool-only operation; never sent to mc_apply. */
static int number(cJSON *root,const char *name,uint32_t *out)
{
    cJSON *item=cJSON_GetObjectItemCaseSensitive(root,name); double x;
    if(!cJSON_IsNumber(item)) return 0;
    x=item->valuedouble;
    if(!(x>=0 && x<=UINT32_MAX) || x!=(double)(uint32_t)x) return 0;
    *out=(uint32_t)x;return 1;
}
int tool_mooncat_app_execute(const char *input,char *output,size_t output_size)
{
    static const char *ops[]={"list","schedule","focus_on","focus_off","dnd_on","dnd_off",
        "workout_on","workout_off","resume_today","start","snooze","pause_today","complete","cancel","remind"};
    struct mc_command command={0}; struct mc_state snapshot={0};
    cJSON *root,*op,*result,*tasks; char *json; size_t i; int status=MC_INVALID,known=0;
    if(!output || !output_size) return -1;
    /* Bound the reply before any persistent side effect. */
    if(output_size<2048) {snprintf(output,output_size,"{\"ok\":false,\"status\":\"output_too_small\"}");return -1;}
    root=cJSON_ParseWithOpts(input ? input : "{}",NULL,1);
    op=cJSON_GetObjectItemCaseSensitive(root,"op");
    if(cJSON_IsObject(root) && cJSON_IsString(op)) {
        for(i=0;i<sizeof(ops)/sizeof(ops[0]);++i) if(!strcmp(op->valuestring,ops[i])) {command.op=(uint32_t)i;known=1;break;}
    }
    if(known) {
        int valid=1; cJSON *field;
        /* Reject extra/duplicate keys so a misspelled parameter is never ignored. */
        cJSON_ArrayForEach(field,root) {
            cJSON *next; int allowed=!strcmp(field->string,"op");
            if(command.op==MC_SCHEDULE) allowed|=!strcmp(field->string,"request_id") || !strcmp(field->string,"delay") || !strcmp(field->string,"duration");
            if(command.op>=MC_START && command.op<=MC_CANCEL) allowed|=!strcmp(field->string,"id") || !strcmp(field->string,"revision");
            if(command.op==MC_SNOOZE) allowed|=!strcmp(field->string,"delay");
            if(command.op==APP_TOOL_REMIND) allowed|=!strcmp(field->string,"delay") || !strcmp(field->string,"duration");
            if(!allowed) valid=0;
            for(next=field->next;next;next=next->next) if(!strcmp(field->string,next->string)) valid=0;
        }
        if(command.op==MC_SCHEDULE) valid=valid && number(root,"request_id",&command.request_id) && number(root,"delay",&command.delay) && number(root,"duration",&command.duration);
        if(command.op>=MC_START && command.op<=MC_CANCEL) valid=valid && number(root,"id",&command.id) && number(root,"revision",&command.revision);
        if(command.op==MC_SNOOZE) valid=valid && number(root,"delay",&command.delay);
        if(command.op==APP_TOOL_REMIND) valid=valid && number(root,"delay",&command.delay) && number(root,"duration",&command.duration);
        if(valid) status=command.op==APP_TOOL_REMIND ? mooncat_app_remind(command.delay,command.duration,&snapshot) :
            command.op ? mooncat_app_command(&command,&snapshot) : mooncat_app_snapshot(&snapshot);
    }
    cJSON_Delete(root);
    result=cJSON_CreateObject();
    if(!result) {snprintf(output,output_size,"{\"ok\":false,\"status\":\"allocation_failed_execution_uncertain\"}");return -1;}
    cJSON_AddBoolToObject(result,"ok",status==MC_OK || status==MC_DUPLICATE);
    cJSON_AddStringToObject(result,"status",mc_status_name(status));
    cJSON_AddStringToObject(result,"source","app_event");
    cJSON_AddStringToObject(result,"timebase","app_monotonic");
    cJSON_AddNumberToObject(result,"now",snapshot.last_now);
    cJSON_AddStringToObject(result,"evidence_boundary","user_actions_not_sensor_measurements");
    cJSON_AddNumberToObject(result,"focus_seconds",snapshot.focus_seconds);
    cJSON_AddNumberToObject(result,"shown_count",snapshot.shown_count);
    cJSON_AddNumberToObject(result,"confirmed_completed",snapshot.completed_count);
    cJSON_AddBoolToObject(result,"focus",snapshot.focus);
    cJSON_AddBoolToObject(result,"workout",snapshot.workout);
    cJSON_AddBoolToObject(result,"dnd",snapshot.dnd);
    cJSON_AddNumberToObject(result,"pause_until",snapshot.pause_until);
    tasks=cJSON_AddArrayToObject(result,"tasks");
    for(i=0;i<MC_TASK_CAPACITY;++i) if(snapshot.tasks[i].id) {
        const struct mc_task *t=&snapshot.tasks[i]; cJSON *item=cJSON_CreateObject();
        if(!item || !tasks) {cJSON_Delete(item);cJSON_Delete(result);snprintf(output,output_size,"{\"ok\":false,\"status\":\"allocation_failed_execution_uncertain\"}");return -1;}
        cJSON_AddNumberToObject(item,"id",t->id); cJSON_AddNumberToObject(item,"revision",t->revision);
        cJSON_AddNumberToObject(item,"request_id",t->request_id); cJSON_AddNumberToObject(item,"due",t->due);
        cJSON_AddNumberToObject(item,"phase",t->phase); cJSON_AddNumberToObject(item,"reason",t->reason);
        cJSON_AddNumberToObject(item,"recovery_end",t->recovery_end);
        cJSON_AddNumberToObject(item,"duration",t->duration);
        cJSON_AddNumberToObject(item,"requested_delay",t->requested_delay);
        cJSON_AddItemToArray(tasks,item);
    }
    json=cJSON_PrintUnformatted(result);cJSON_Delete(result);
    if(!json || strlen(json)>=output_size) {free(json);snprintf(output,output_size,"{\"ok\":false,\"status\":\"reply_failed_execution_uncertain\"}");return -1;}
    strcpy(output,json);free(json);return status==MC_OK || status==MC_DUPLICATE ? 0 : -1;
}
