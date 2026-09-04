#include "mooncat_coach_policy.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static struct mooncat_coach_observation_s baseline(void)
{
    struct mooncat_coach_observation_s observation = {
        .source = MOONCAT_DATA_SOURCE_SIMULATED,
        .valid = true,
        .battery_percent = 80,
    };

    return observation;
}
int main(void)
{
    struct mooncat_coach_observation_s observation = baseline();
    struct mooncat_coach_decision_s decision;
    char notification[160];

    decision = mooncat_coach_evaluate(&observation);
    assert(decision.action == MOONCAT_COACH_NONE);
    assert(strcmp(decision.reason_id, "no_intervention") == 0);

    observation.inactivity_minutes = 45;
    decision = mooncat_coach_evaluate(&observation);
    assert(decision.action == MOONCAT_COACH_STRETCH);
    assert(decision.cooldown_seconds == 2700);

    observation.inactivity_minutes = 91;
    decision = mooncat_coach_evaluate(&observation);
    assert(decision.action == MOONCAT_COACH_WALK);
    assert(decision.suggested_duration_seconds == 180);
    assert(mooncat_coach_format_notification(&observation, &decision,
                                              notification,
                                              sizeof(notification)) == 0);
    assert(strstr(notification, "[DEMO]") == notification);
    assert(strstr(notification, "91 minutes") != NULL);

    /* Higher stress deterministically outranks inactivity and sleep debt. */
    observation.stress_score = 80;
    observation.sleep_debt_minutes = 240;
    decision = mooncat_coach_evaluate(&observation);
    assert(decision.action == MOONCAT_COACH_BREATHE);
    assert(decision.priority == MOONCAT_PRIORITY_HIGH);

    observation.do_not_disturb = true;
    decision = mooncat_coach_evaluate(&observation);
    assert(decision.action == MOONCAT_COACH_NONE);
    assert(strcmp(decision.reason_id, "do_not_disturb") == 0);

    observation.do_not_disturb = false;
    observation.source = MOONCAT_DATA_SOURCE_LIVE;
    decision = mooncat_coach_evaluate(&observation);
    assert(decision.action == MOONCAT_COACH_NONE);
    assert(strcmp(decision.reason_id, "source_not_verified") == 0);

    observation.source = MOONCAT_DATA_SOURCE_SIMULATED;
    observation.workout_active = true;
    decision = mooncat_coach_evaluate(&observation);
    assert(strcmp(decision.reason_id, "workout_active") == 0);

    observation.workout_active = false;
    observation.battery_percent = 5;
    decision = mooncat_coach_evaluate(&observation);
    assert(strcmp(decision.reason_id, "battery_critical") == 0);

    assert(mooncat_coach_evaluate(NULL).action == MOONCAT_COACH_NONE);
    puts("mooncat_coach_policy: all deterministic cases passed");
    return 0;
}
